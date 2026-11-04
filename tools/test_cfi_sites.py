# SPDX-License-Identifier: GPL-2.0-only
"""Host test: tools/cfi_sites.py must see every CFI-checked indirect call, and only those.

Two layers, no shell involved:

  synthetic  builds ET_REL aarch64 objects byte by byte, so every detector boundary (the
             trap window, a stack local that looks like a check, a trap borrowed from the
             next function, reloc type, attribution) is testable with no cross toolchain;
  codegen    if a cross clang is present, compiles real KCFI sources and checks that the
             detector's count equals the trap count in the disassembly.

Cases marked KNOWN are the defects the review found and that this test does not yet fix: a
site at i+8, the stack local + BUG() false positive, the cross-function trap, an
address-taken slowpath.  They pass on today's behaviour and print CHANGED if a later fix
moves them, so the record cannot rot silently.
"""
import argparse
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent

# --- ELF writing (just enough for an ET_REL the tool accepts) -------------------------
SHT_NULL, SHT_PROGBITS, SHT_SYMTAB, SHT_STRTAB, SHT_RELA = 0, 1, 2, 3, 4
SHF_ALLOC_EXEC = 0x6
ST_INFO = (1 << 4) | 2                     # STB_GLOBAL | STT_FUNC
R_AARCH64_CALL26, R_AARCH64_ABS64 = 283, 257


class Elf:
    """Minimal ELF64 little-endian relocatable writer."""

    def __init__(self):
        self.secs = []                     # index 0 (the null section) is implied

    def add(self, name, typ, flags=0, link=0, info=0, entsize=0, data=b""):
        self.secs.append(dict(name=name, typ=typ, flags=flags, link=link, info=info,
                              entsize=entsize, data=data))
        return len(self.secs)              # its section index

    def build(self, path):
        secs = [dict(name="", typ=SHT_NULL, flags=0, link=0, info=0, entsize=0, data=b"")]
        secs += self.secs
        names, noff = b"\0", {}
        for s in secs[1:]:
            noff[id(s)] = len(names)
            names += s["name"].encode() + b"\0"
        shstr_name = len(names)
        names += b".shstrtab\0"
        secs.append(dict(name=".shstrtab", typ=SHT_STRTAB, flags=0, link=0, info=0,
                         entsize=0, data=names))
        shstrndx = len(secs) - 1

        blob, layout = bytearray(64), [0]
        for s in secs[1:]:
            while len(blob) % 8:
                blob += b"\0"
            layout.append(len(blob))
            blob += s["data"]
        while len(blob) % 8:
            blob += b"\0"
        shoff = len(blob)
        for i, s in enumerate(secs):
            blob += struct.pack("<IIQQQQIIQQ",
                                shstr_name if i == shstrndx else noff.get(id(s), 0), s["typ"],
                                s["flags"], 0, layout[i], len(s["data"]), s["link"], s["info"],
                                8, s["entsize"])
        blob[:64] = struct.pack("<16sHHIQQQIHHHHHH", b"\x7fELF\x02\x01\x01" + bytes(9), 1, 183,
                                1, 0, 0, shoff, 0, 64, 0, 0, 64, len(secs), shstrndx)
        path.write_bytes(bytes(blob))


class Obj:
    """An object under construction: sections of words, their functions, their relocs."""

    def __init__(self):
        self.elf, self.sections, self.relocs = Elf(), [], []
        self.strtab = bytearray(b"\0")

    def section(self, name=".text"):
        self.sections.append(dict(name=name, data=bytearray(), funcs=[]))
        return self

    def code(self, *words):
        sec = self.sections[-1]
        sec["data"] += struct.pack("<%dI" % len(words), *words)
        return len(sec["data"]) - 4 * len(words)

    def quad(self):
        sec = self.sections[-1]
        sec["data"] += b"\0" * 8
        return len(sec["data"]) - 8

    def func(self, name, off, size):
        self.sections[-1]["funcs"].append((name, off, size))
        return self

    def reloc(self, off, sym, typ=R_AARCH64_CALL26):
        self.relocs.append((len(self.sections) - 1, off, sym, typ))
        return self

    def write(self, path, symtab=True):
        for sec in self.sections:
            sec["idx"] = self.elf.add(sec["name"], SHT_PROGBITS, SHF_ALLOC_EXEC,
                                      data=bytes(sec["data"]))
        if not symtab:
            self.elf.build(path)
            return
        undef = []
        for _sec, _off, sym, _typ in self.relocs:
            if sym not in undef:
                undef.append(sym)
        syms = [(self.string(n), ST_INFO, 0, 0, 0, 0) for n in undef]
        for sec in self.sections:
            syms += [(self.string(n), ST_INFO, 0, sec["idx"], off, size)
                     for n, off, size in sec["funcs"]]
        strtab = self.elf.add(".strtab", SHT_STRTAB, data=bytes(self.strtab))
        symtab = self.elf.add(".symtab", SHT_SYMTAB, link=strtab, info=1, entsize=24,
                              data=b"\0" * 24 + b"".join(struct.pack("<IBBHQQ", *s)
                                                         for s in syms))
        # one .rela per section that needs one, so a reloc can never land on the wrong one
        for idx, sec in enumerate(self.sections):
            rows = [r for r in self.relocs if r[0] == idx]
            if rows:
                body = b"".join(struct.pack("<QQq", off, ((undef.index(sym) + 1) << 32) | typ, 0)
                                for _i, off, sym, typ in rows)
                self.elf.add(".rela" + sec["name"], SHT_RELA, link=symtab, info=sec["idx"],
                             entsize=24, data=body)
        self.elf.build(path)

    def string(self, s):
        off = len(self.strtab)
        self.strtab += s.encode() + b"\0"
        return off


# --- the aarch64 words the detectors care about --------------------------------------
def ldur(rt, rn):
    return 0xB85FC000 | (rn << 5) | rt           # ldur Wt, [Xn, #-4]


def cmp_rr(rn, rm):
    return 0x6B00001F | (rm << 16) | (rn << 5)   # subs wzr, Wn, Wm  (cmp Wn, Wm)


def brk(imm=0x8228):
    return 0xD4200000 | (imm << 5)               # brk #imm16


def blr(rn):
    return 0xD63F0000 | (rn << 5)


NOP, RET = 0xD503201F, 0xD65F03C0
BL = 0x94000000
SLOWPATH = "__cfi_slowpath_diag"


# --- running the tool ----------------------------------------------------------------
def run(tool, obj, allow=""):
    p = subprocess.run([sys.executable, str(tool), "--allow", allow, str(obj)],
                       capture_output=True, text=True)
    sites = {}
    for line in p.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 4 and parts[0] in ("ok", "FAIL"):
            sites[parts[1]] = line
    return p.returncode, p.stdout + p.stderr, sorted(sites)


class Report:
    def __init__(self):
        self.n = self.fail = self.known = 0

    def check(self, name, got, want, detail="", known=False):
        self.n += 1
        if got == want:
            if known:
                self.known += 1
                print("KNOWN   %-22s %s" % (name, detail))
            else:
                print("ok      %-22s %s" % (name, detail))
        elif known:
            self.known += 1
            print("CHANGED %-22s %s  [was %r - is it fixed now?]" % (name, detail, want))
        else:
            self.fail += 1
            print("FAIL    %-22s %s  [want %r, got %r]" % (name, detail, want, got))

    def done(self):
        print("\n%d checks: %d failed, %d documented (KNOWN)" % (self.n, self.fail, self.known))
        return 1 if self.fail else 0


def synthetic(tool, tmp, rep):
    """Detector boundaries, from hand-built ELF objects."""

    def build(name, sections, symtab=True):
        o = Obj()
        for sec_name, words, funcs in sections:
            o.section(sec_name)
            start = o.code(*words) if words else 0
            for fname, foff, fsize in funcs:
                o.func(fname, start + foff, fsize)
        path = tmp / (name + ".o")
        o.write(path, symtab=symtab)
        return path

    def call(name, words, allow=""):
        return run(tool, build(name, [(".text", words, [("site", 0, 4 * len(words))])]), allow)

    # 1. a KCFI check: ldur / cmp / b.eq / brk / blr
    path = build("kcfi", [(".text", [ldur(16, 8), NOP, cmp_rr(16, 17), NOP, brk(), blr(8)],
                           [("site", 0, 24)])])
    rep.check("kcfi_site", run(tool, path)[2], ["site"], "ldur+cmp+brk+blr -> site x1")
    rep.check("kcfi_allow", run(tool, path, "site")[0], 0, "--allow site -> exit 0")
    rc, out, _ = run(tool, path, "other")
    rep.check("kcfi_denied", (rc, "::error::" in out), (1, True),
              "a site outside the allow-list fails the gate")
    rep.check("allow_exact", run(tool, path, "sit")[0], 1, "the allow-list matches names exactly")

    # 2. two checks in one function: the detector's count, not just the name
    path = build("two", [(".text", [ldur(16, 8), cmp_rr(16, 17), brk(), blr(8)] * 2,
                          [("site", 0, 32)])])
    rep.check("kcfi_count", "ldur=2" in run(tool, path, "site")[1], True, "two checks -> ldur=2")

    # 3. the same without a trap (the 6.12 stack-local shape): must not be reported
    rc, _out, sites = call("notrap", [ldur(16, 8), NOP, cmp_rr(16, 17), blr(8)])
    rep.check("no_trap", (rc, sites), (1, []), "no brk -> no site, \"proves nothing\"")

    # 4. the trap window: found at i+7, and not when it is further away
    rep.check("trap_at_i+7", call("trap7", [ldur(16, 8), cmp_rr(16, 17)] + [NOP] * 5
                                  + [brk(), blr(8)])[2], ["site"], "brk 7 words after the load")
    rep.check("trap_at_i+8", call("trap8", [ldur(16, 8), cmp_rr(16, 17)] + [NOP] * 6
                                  + [brk(), blr(8)])[2], [], "brk 8 words after the load: missed",
              known=True)

    # 5. a compared 4-byte stack local followed by BUG(): a false positive today
    rep.check("stack_local_fp", call("stacklocal", [ldur(8, 29), cmp_rr(8, 1), brk(1), RET])[2],
              ["site"], "ldur [x29,#-4]+cmp+brk, no call at all", known=True)

    # 6. the window must not grow either: a distant trap is still a stack local
    rep.check("stack_local_far", call("stackfar", [ldur(8, 29), cmp_rr(8, 1)] + [NOP] * 7
                                      + [brk(1), RET])[2], [],
              "a [x29,#-4] compare with a far trap is not a check")

    # 7. the trap of the NEXT function is credited to this one
    o = Obj().section()
    o.code(ldur(8, 29), cmp_rr(8, 10), RET)
    o.func("fn_a", 0, 12)
    o.code(brk(1), RET)
    o.func("fn_b", 12, 8)
    path = tmp / "cross.o"
    o.write(path)
    rep.check("cross_function_brk", run(tool, path)[2], ["fn_a"], "fn_b's brk credits fn_a",
              known=True)

    # 8. reloc detector: a call is a call, an address-taken quad is not
    o = Obj().section()
    off = o.code(BL, RET)
    o.func("reloc_call", 0, 8).reloc(off, SLOWPATH)
    path = tmp / "reloc.o"
    o.write(path)
    rep.check("reloc_call", run(tool, path)[2], ["reloc_call"], "bl __cfi_slowpath_diag")

    o = Obj().section()
    off = o.quad()
    o.code(RET)
    o.func("reloc_data", 0, 12)
    o.reloc(off, SLOWPATH, R_AARCH64_ABS64)
    path = tmp / "relocdata.o"
    o.write(path)
    rep.check("reloc_data_fp", run(tool, path)[2], ["reloc_data"],
              "address-taken slowpath is not a call", known=True)

    # 9. the CFI runtime itself is filtered out
    o = Obj().section()
    off = o.code(BL, RET)
    o.func("__cfi_check", 0, 8).reloc(off, SLOWPATH)
    path = tmp / "runtime.o"
    o.write(path)
    rc, _out, sites = run(tool, path)
    rep.check("cfi_runtime_filtered", (rc, sites), (1, []), "__cfi_check is skipped")

    # 10. no symbol table at all: attribution falls back to the section
    path = build("nosym", [(".text", [ldur(16, 8), cmp_rr(16, 17), brk(), blr(8)],
                            [("site", 0, 16)])], symtab=False)
    rep.check("no_symtab", run(tool, path)[2], ["<.text>"], "no .symtab -> <.text>")

    # 11. a zero-size function cannot be attributed; the gate still fails (fail-closed)
    path = build("zerosize", [(".text", [ldur(16, 8), cmp_rr(16, 17), brk(), blr(8)],
                               [("zero", 0, 0)])])
    rc, _out, sites = run(tool, path)
    rep.check("zero_size_func", (rc, sites), (1, ["<.text>"]), "size 0 -> <.text>, exit 1")

    # 12. attribution works across executable sections
    path = build("inittext", [(".text", [RET], [("hot", 0, 4)]),
                              (".init.text", [ldur(16, 8), cmp_rr(16, 17), brk(), blr(8)],
                               [("init_module", 0, 16)])])
    rc, _out, sites = run(tool, path, "init_module")
    rep.check("init_text_allow", (rc, sites), (0, ["init_module"]), ".init.text resolved")

    # 13/14/15. failure modes
    path = tmp / "notelf.bin"
    path.write_bytes(b"hi\n")
    rc, out, _ = run(tool, path)
    rep.check("not_elf", (rc, "not an ELF file" in out), (1, True), "clean error, exit 1")
    path = tmp / "empty.o"
    path.write_bytes(b"")
    rc, out, _ = run(tool, path)
    rep.check("empty_file", (rc, "ERROR" in out), (1, True), "reported, not raised")
    p = subprocess.run([sys.executable, str(tool), str(tmp / "kcfi.o")],
                       capture_output=True, text=True)
    rep.check("allow_required", (p.returncode, "--allow was not given" in p.stderr + p.stdout),
              (1, True), "missing --allow -> exit 1")


CODEGEN_C = r"""
extern void t0(void); extern void t1(int); extern void t2(char*);
void (*(*g0)(void))(void);
void (*(*g1)(void))(int);
__attribute__((noinline)) void many_calls(int n) {
    for (int i = 0; i < n; i++) { g0()(); g1()(1); (void)&t0; (void)&t1; (void)&t2; }
}
__attribute__((noinline, no_sanitize("kcfi"))) void plain(void) { g0()(); }
"""


def codegen(tool, tmp, rep):
    """Real compiler output, if a cross clang is available."""
    clang = shutil.which("clang")
    if not clang:
        print("SKIP    codegen               no clang on PATH")
        return
    src, obj = tmp / "kcfi.c", tmp / "kcfi_real.o"
    src.write_text(CODEGEN_C)
    p = subprocess.run([clang, "--target=aarch64-linux-gnu", "-fsanitize=kcfi",
                        "-fno-sanitize-ignorelist", "-O2", "-fno-omit-frame-pointer",
                        "-c", str(src), "-o", str(obj)], capture_output=True, text=True)
    if p.returncode:
        print("SKIP    codegen               clang cannot build aarch64 KCFI: %s"
              % (p.stderr.strip().splitlines() or ["?"])[0])
        return
    rc, out, _sites = run(tool, obj, "many_calls,plain")
    line = next((l for l in out.splitlines()
                 if l.split()[:1] in (["ok"], ["FAIL"]) and "many_calls" in l), "")
    count = int(line.split("ldur=")[1].rstrip(")")) if "ldur=" in line else 0
    rep.check("codegen_sites", (rc, "plain" in out, count > 0), (0, False, True),
              "real KCFI detected in many_calls (x%d), nocfi caller absent" % count)
    objdump = shutil.which("aarch64-linux-gnu-objdump") or shutil.which("llvm-objdump")
    if not objdump:
        print("SKIP    codegen_vs_disasm      no aarch64 objdump")
        return
    flags = ["-d"] if "llvm-objdump" in objdump else ["-d"]
    disasm = subprocess.run([objdump] + flags + [str(obj)], capture_output=True, text=True).stdout
    # only the KCFI traps, so a BUG() in the source could never balance the count by accident
    traps = sum(1 for l in disasm.splitlines() if "\tbrk\t#0x822" in l)
    rep.check("codegen_vs_disasm", count, traps, "detector count == KCFI trap count in the disassembly")


def main():
    ap = argparse.ArgumentParser(description="boundary tests for cfi_sites.py")
    ap.add_argument("--tool", default=str(HERE / "cfi_sites.py"))
    ap.add_argument("--keep", action="store_true", help="keep the generated objects")
    args = ap.parse_args()
    tool = Path(args.tool)
    if not tool.exists():
        sys.exit("no such tool: %s" % tool)
    with tempfile.TemporaryDirectory(prefix="cfi_sites_test.") as directory:
        tmp = Path(directory)
        print("tool:    %s\nobjects: %s\n" % (tool, tmp))
        rep = Report()
        synthetic(tool, tmp, rep)
        codegen(tool, tmp, rep)
        rc = rep.done()
        if args.keep:
            keep = Path(tempfile.mkdtemp(prefix="cfi_sites_kept."))
            for f in tmp.iterdir():
                shutil.copy(f, keep / f.name)
            print("kept: %s" % keep)
    if rc:
        return rc
    print("cfi sites test ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
