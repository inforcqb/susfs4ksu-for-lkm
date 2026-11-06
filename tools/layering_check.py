#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Report layering violations for the kernel tree.

Phase 0 is deliberately report-only: current code still has known violations.
`--strict` is for the later phases, once the per-file budget reaches zero.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')

RULES = {
    "F": {
        "includes": [
            "linux/namei.h", "linux/path.h", "linux/dcache.h",
            "linux/kprobes.h", "susfs_kprobe.h",
        ],
        "symbols": [
            "vfs_mkdir", "filename_parentat", "lookup_one_qstr_excl",
            "done_path_create", "security_path_mkdir",
        ],
    },
    "L": {
        "includes": [
            "asm/", "linux/kprobes.h", "mount.h", "linux/mount.h",
            "linux/namei.h", "linux/path.h", "linux/dcache.h",
            "susfs_create_vfs.h",
        ],
        "symbols": [
            "mnt_want_write", "inode_lock_nested", "vfs_mkdir",
            "filename_parentat", "lookup_one_qstr_excl",
            "struct filename", "struct path", "struct mount", "struct pt_regs",
        ],
    },
    "P": {
        "includes": ["susfs_create_policy.h"],
        "symbols": ["susfs_create_name_hidden", "susfs_create_decide"],
    },
    "D": {
        "includes": [],
        "symbols": ["current_uid", "current_cred", "sus_path_gate",
                    "sus_path_inode_hidden", "sus_path_user_rules_present"],
    },
}


def load_json(path: Path) -> dict:
    with path.open(encoding="utf-8") as f:
        return json.load(f)


def rel_posix(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def remove_if0(text: str) -> str:
    """Drop inactive #if 0 blocks; keep #else of a 0 branch."""
    out: list[str] = []
    stack: list[bool] = []
    for line in text.splitlines(keepends=True):
        s = line.strip()
        if s.startswith("#if"):
            parent = all(stack)
            zero = bool(re.match(r"#if\s+0(?:\s|$)", s))
            stack.append(not zero)
            if not (parent and stack[-1]):
                continue
        elif s.startswith("#else") and stack:
            stack[-1] = not stack[-1]
            continue
        elif s.startswith("#elif") and stack:
            # Conservative: an elif of a 0 branch is treated as active only
            # when the enclosing branch is active.
            stack[-1] = True
            continue
        elif s.startswith("#endif"):
            if stack:
                stack.pop()
            continue
        if stack and not all(stack):
            continue
        out.append(line)
    return "".join(out)


def strip_comments_strings(text: str) -> str:
    """Blank comments and string/char literals, preserving newlines."""
    out: list[str] = []
    i = 0
    state = "code"
    while i < len(text):
        c = text[i]
        n = text[i + 1] if i + 1 < len(text) else ""
        if state == "code":
            if c == "/" and n == "*":
                state, i = "block", i + 2
                continue
            if c == "/" and n == "/":
                state, i = "line", i + 2
                continue
            if c in ('"', "'"):
                state, i = "string", i + 1
                out.append(" ")
                continue
            out.append(c)
            i += 1
        elif state == "block":
            if c == "*" and n == "/":
                state, i = "code", i + 2
                continue
            if c == "\n":
                out.append("\n")
            i += 1
        elif state == "line":
            if c == "\n":
                out.append("\n")
                state = "code"
            i += 1
        else:  # string/char
            if c == "\\":
                i += 2
                continue
            if c == "\n":
                out.append("\n")
                state = "code"
                i += 1
                continue
            if c in ('"', "'"):
                state = "code"
                i += 1
                continue
            i += 1
    return "".join(out)


def strip_comments_keep_strings(text: str) -> str:
    """Remove C comments but keep string literals (for #include paths)."""
    out: list[str] = []
    i = 0
    state = "code"
    while i < len(text):
        c = text[i]
        n = text[i + 1] if i + 1 < len(text) else ""
        if state == "code":
            if c == "/" and n == "*":
                state, i = "block", i + 2
                continue
            if c == "/" and n == "/":
                state, i = "line", i + 2
                continue
            out.append(c)
            i += 1
        elif state == "block":
            if c == "*" and n == "/":
                state, i = "code", i + 2
                continue
            if c == "\n":
                out.append("\n")
            i += 1
        else:
            if c == "\n":
                out.append("\n")
                state = "code"
            i += 1
    return "".join(out)


def check_file(path: Path, meta: dict) -> list[dict]:
    layer = meta.get("layer")
    if layer not in RULES:
        return []
    raw = path.read_text(encoding="utf-8", errors="replace")
    active = remove_if0(raw)
    clean = strip_comments_strings(active)
    include_text = strip_comments_keep_strings(active)
    allowed_inc = set(meta.get("allowed_includes", []))
    allowed_sym = set(meta.get("allowed_symbols", []))
    out: list[dict] = []

    for line in include_text.splitlines():
        m = INCLUDE_RE.match(line)
        if not m:
            continue
        inc = m.group(1)
        for bad in RULES[layer]["includes"]:
            if inc == bad or (bad.endswith("/") and inc.startswith(bad)) or inc.startswith(bad):
                if inc not in allowed_inc:
                    out.append({"file": rel_posix(path),
                                "rule": f"{layer}_INCLUDE", "symbol": inc})
                break

    for sym in RULES[layer]["symbols"]:
        if sym in allowed_sym:
            continue
        if re.search(rf"(?<![A-Za-z0-9_]){re.escape(sym)}(?![A-Za-z0-9_])", clean):
            out.append({"file": rel_posix(path),
                        "rule": f"{layer}_SYMBOL", "symbol": sym})
    return out


def key(v: dict) -> tuple[str, str, str]:
    return (v.get("file", ""), v.get("rule", ""), v.get("symbol", ""))


def parse_allowlist(path: Path) -> tuple[set[tuple[str, str, str]], list[str]]:
    allowed: set[tuple[str, str, str]] = set()
    problems: list[str] = []
    today = date.today()
    for e in load_json(path).get("entries", []):
        missing = [k for k in ("file", "rule", "symbol", "owner") if not e.get(k)]
        if missing:
            problems.append(f"allowlist entry missing {','.join(missing)}: {e}")
            continue
        exp = e.get("expires")
        if exp:
            try:
                if date.fromisoformat(exp) < today:
                    problems.append(f"allowlist expired: {e['file']} {e['rule']} {e['symbol']}")
                    continue
            except ValueError:
                problems.append(f"allowlist bad expires: {e}")
                continue
        allowed.add(key(e))
    return allowed, problems


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--strict", action="store_true",
                    help="fail on new violations, expired allowlist or budget overflow")
    ap.add_argument("--manifest", default="tools/layer_manifest.json")
    ap.add_argument("--baseline", default="tools/layer_baseline.json")
    ap.add_argument("--allowlist", default="tools/layer_allowlist.json")
    args = ap.parse_args()

    manifest = load_json(ROOT / args.manifest)
    baseline = {key(v) for v in load_json(ROOT / args.baseline).get("violations", [])}
    allow, allow_problems = parse_allowlist(ROOT / args.allowlist)

    violations: list[dict] = []
    for rel, meta in manifest.get("files", {}).items():
        p = ROOT / rel
        if p.exists():
            violations.extend(check_file(p, meta))

    actual = {f"kernel/{p.name}" for p in (ROOT / "kernel").glob("*.[ch]")}
    listed = set(manifest.get("files", {}))
    missing = sorted(actual - listed)

    new = [v for v in violations if key(v) not in baseline and key(v) not in allow]
    budgets: dict[str, int] = {}
    for v in new:
        budgets[v["file"]] = budgets.get(v["file"], 0) + 1
    over = [(f, n, manifest["files"][f]["budget"])
            for f, n in budgets.items()
            if f in manifest["files"] and n > manifest["files"][f].get("budget", 0)]
    stale = sorted(baseline - {key(v) for v in violations} - allow)

    print(f"layering: {len(violations)} violation(s), "
          f"{len(new)} not in baseline/allowlist")
    for v in sorted(new, key=key)[:40]:
        print(f"  {v['file']}: {v['rule']} {v['symbol']}")
    if len(new) > 40:
        print(f"  ... {len(new) - 40} more")
    if stale:
        print(f"layering: {len(stale)} stale baseline entries")
    if missing:
        print(f"layering: {len(missing)} kernel file(s) not in manifest:")
        for f in missing[:10]:
            print(f"  {f}")
    for p in allow_problems:
        print(f"layering: {p}")
    for f, n, b in over:
        print(f"layering: budget exceeded {f}: {n} > {b}")

    fail = bool(args.strict and (new or missing or allow_problems or over))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
