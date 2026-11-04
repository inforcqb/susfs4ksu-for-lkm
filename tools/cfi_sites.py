#!/usr/bin/env python3
"""cfi_sites - which functions in a built .ko perform a CFI-checked indirect call?

The module calls every runtime-resolved kernel symbol (kernel/symbol_resolver.c) through a
function pointer, and the compiler instruments that call site.  A type-id mismatch is a
panic, not an error return ("CFI failure (target: kallsyms_on_each_symbol+0x0/0x1e4)", the
bug fixed in c2a5575), so such a call has to sit in a `__nocfi` function.  Source review
cannot assert that: LTO re-instruments a `static __nocfi` helper that was inlined into a
caller without the attribute.  So assert it on the artifact, with two detectors unioned:

  reloc  a relocation against the CFI runtime entry point (`__cfi_slowpath*` /
         `__cfi_check_fail`): the mismatch slow path of LLVM CFI (5.10/5.15).
  ldur   `ldur Wt, [Xn, #-4]` of the callee's type id, plus a 32-bit `cmp` on Wt and the
         `brk` of the mismatch path: KCFI (6.1+), whose mismatch is a trap and therefore
         relocates nothing - the reloc detector alone would pass those variants vacuously.

`--allow` names the functions whose instrumented calls are safe by construction: calls
through a table of MODULE functions, whose type ids the compiler emitted itself.
"""
import argparse, struct, sys
from collections import defaultdict

SHF_EXEC, SHT_SYMTAB, SHT_RELA, SHT_NOBITS, STT_FUNC = 0x4, 2, 4, 8, 2
CFI_ENTRY = ('__cfi_slowpath', '__cfi_check_fail')
LDUR = 0xB85FC000, 0xFFFFFC00   # `ldur Wt, [Xn, #-4]`: 32-bit unscaled load, imm9 = -4
CMP = 0x6B00001F, 0xFF20001F    # `subs wzr, Wn, Wm` (`cmp Wn, Wm`), shift/imm6 ignored
BRK = 0xD4200000, 0xFFE0001F    # `brk #imm16`, the KCFI mismatch trap (`brk #0x8228`)
WINDOW = 8                      # how far past the load to look: 7 instructions


def word(data, i):
    return struct.unpack_from('<I', data, i * 4)[0]


def cmp_reads(w, reg):
    """Is w a 32-bit `cmp Wn, Wm` that reads W@reg?"""
    return w & CMP[1] == CMP[0] and reg in ((w >> 5) & 0x1F, (w >> 16) & 0x1F)


class Ko:
    """An ET_REL aarch64 object: its sections, its .symtab and its relocations."""

    def __init__(self, path):
        self.b = b = open(path, 'rb').read()
        if b[:4] != b'\x7fELF':
            raise ValueError('not an ELF file')
        if b[4:6] != b'\x02\x01':
            raise ValueError('expected ELF64 little-endian')
        shoff, = struct.unpack_from('<Q', b, 0x28)
        shentsize, shnum, shstrndx = struct.unpack_from('<HHH', b, 0x3a)
        names = ('name', 'typ', 'flags', 'addr', 'off', 'size', 'link', 'info', 'align', 'entsize')
        self.secs = [dict(zip(names, struct.unpack_from('<IIQQQQIIQQ', b, shoff + i * shentsize)))
                     for i in range(shnum)]

        def string(o, strtab):      # NUL-terminated string at offset o of a string table
            return b[strtab + o:b.index(b'\0', strtab + o)].decode()

        for i, sec in enumerate(self.secs):
            sec['sname'], sec['idx'] = string(sec['name'], self.secs[shstrndx]['off']), i

        # function ranges per executable section, sorted for the offset -> function lookup
        self.syms, self.funcs = [], defaultdict(list)
        symtab = next((s for s in self.secs if s['typ'] == SHT_SYMTAB), None)
        if symtab:
            strtab = self.secs[symtab['link']]['off']
            body = b[symtab['off']:symtab['off'] + symtab['size'] // 24 * 24]
            self.syms = [(string(n, strtab), info & 0xf, value, size, shndx)
                         for n, info, _other, shndx, value, size in struct.iter_unpack('<IBBHQQ', body)]
            for name, typ, value, size, shndx in self.syms:
                if typ == STT_FUNC and size and shndx < len(self.secs) and \
                        self.secs[shndx]['flags'] & SHF_EXEC:
                    self.funcs[shndx].append((value, value + size, name))
            for ranges in self.funcs.values():
                ranges.sort(key=lambda r: r[0])

    def holder(self, sec, off):
        for lo, hi, name in self.funcs.get(sec, ()):
            if lo <= off < hi:
                return name

    def reloc_sites(self):
        """{function: count} for every relocation against a CFI runtime entry point."""
        hits = defaultdict(int)
        for rel in self.secs:
            if rel['typ'] != SHT_RELA or not self.secs[rel['info']]['flags'] & SHF_EXEC:
                continue            # .debug_info and friends mention these symbols too
            body = self.b[rel['off']:rel['off'] + rel['size'] // 24 * 24]
            for r_off, r_info, _addend in struct.iter_unpack('<QQq', body):
                si = r_info >> 32
                if si < len(self.syms) and self.syms[si][0].startswith(CFI_ENTRY):
                    sec = self.secs[rel['info']]
                    hits[self.holder(rel['info'], r_off) or '<%s>' % sec['sname']] += 1
        return hits

    def ldur_sites(self):
        """{function: count} for the type-id loads that are really CFI checks.

        A bare `ldur Wt, [Xn, #-4]` is not enough: with frame pointers a 4-byte stack local
        at [x29, #-4] looks identical (on 6.12 that made every sus_path hook a false
        positive), so require the cmp on the loaded register and the mismatch trap:

            ldur w16, [x8, #-4]     ; the callee's type id
            movk w17, #0x944f       ; the type id expected here (a movz/movk pair)
            cmp  w16, w17
            b.eq 1f
              brk #0x8228
            1: blr x8
        """
        hits = defaultdict(int)
        for sec in self.secs:
            if not sec['flags'] & SHF_EXEC or sec['typ'] == SHT_NOBITS:
                continue
            data, n = self.b[sec['off']:sec['off'] + sec['size']], sec['size'] // 4
            for i in range(n):
                w = word(data, i)
                if w & LDUR[1] != LDUR[0]:
                    continue
                rt, win = w & 0x1F, range(i + 1, min(i + WINDOW, n))
                if any(cmp_reads(word(data, j), rt) for j in win) and \
                        any(word(data, j) & BRK[1] == BRK[0] for j in win):
                    hits[self.holder(sec['idx'], i * 4) or '<%s>' % sec['sname']] += 1
        return hits


def main():
    ap = argparse.ArgumentParser(description='CFI-checked indirect call sites in a .ko')
    ap.add_argument('kos', nargs='+')
    ap.add_argument('--allow', default=None, help='comma-separated functions whose instrumented calls are '
                                                  'safe by construction; use --allow "" to allow nothing')
    args = ap.parse_args()
    if args.allow is None:
        print('::error:: --allow was not given: pass the allow-list explicitly (--allow "" allows '
              'nothing).  Without the option this check cannot tell an expected site from a stray one, '
              'and an unset CI variable used to read as "everything is expected"')
        return 1
    allow = {x.strip() for x in args.allow.split(',') if x.strip()}

    bad = 0
    for path in args.kos:
        try:
            ko = Ko(path)
        except Exception as e:                                  # noqa: BLE001 - report and continue
            print('%s: ERROR %s' % (path, e))
            bad += 1
            continue
        # the CFI runtime's own `__cfi_check` calls the slow path - that IS the check
        reloc = {k: v for k, v in ko.reloc_sites().items() if not k.startswith('__cfi')}
        ldur = {k: v for k, v in ko.ldur_sites().items() if not k.startswith('__cfi')}
        sites = sorted(set(reloc) | set(ldur))
        print('== %s' % path)
        print('   reloc detector : %s' % (', '.join('%s x%d' % kv for kv in sorted(reloc.items())) or '(none)'))
        print('   ldur  detector : %s' % (', '.join('%s x%d' % kv for kv in sorted(ldur.items())) or '(none)'))
        if not sites:
            print('   ::error:: no instrumentation found by either detector - this artifact does not '
                  'look like it was built with CFI enabled, so this check proves nothing')
            bad += 1
            continue
        for s in sites:
            print('   %s %s (reloc=%d ldur=%d)' % ('ok  ' if s in allow else 'FAIL', s,
                                                   reloc.get(s, 0), ldur.get(s, 0)))
        outside = [s for s in sites if s not in allow]
        if outside:
            print('   ::error:: %s not in the allow-list: a checked indirect call in these functions '
                  'panics at run time (make the enclosing function __nocfi, or add it to the allow-list '
                  'with a reason)' % ', '.join(outside))
            bad += 1
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
