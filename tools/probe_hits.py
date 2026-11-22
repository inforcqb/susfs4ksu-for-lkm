#!/usr/bin/env python3
"""Count kprobe hits for kernel symbols through tracefs.

This is the repeatable version of the manual probe used for vfs_open/path_openat:

    python tools/probe_hits.py --symbols vfs_open,path_openat,do_dentry_open

The trigger runs as root inside a shell, after the probes are enabled.  A zero
count for a symbol that is called on the trigger path means the out-of-line body
was not executed (normally: LTO inlined every call), not that kprobe failed to
install: an install failure is reported separately from the error log.
"""
import argparse
import base64
import json
import re
import shlex
import subprocess
import sys

DEFAULT_SYMBOLS = "vfs_open,path_openat,do_dentry_open"
DEFAULT_TRIGGER = "cat /system/build.prop >/dev/null 2>&1; ls /system >/dev/null 2>&1"
EVENT_PREFIX = "susfs_probe_"


def adb_base(serial):
    cmd = ["adb"]
    if serial:
        cmd += ["-s", serial]
    return cmd


def run_root(serial, script, timeout=60):
    """Run one shell script as root.  Pass it as a single su -c argument."""
    cmd = adb_base(serial) + ["shell", "su -c " + shlex.quote(script)]
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    if p.returncode != 0:
        raise RuntimeError(f"adb/su failed: {p.returncode}\n{p.stdout}\n{p.stderr}")
    return p.stdout + p.stderr


def event_name(symbol):
    return EVENT_PREFIX + re.sub(r"[^A-Za-z0-9_]", "_", symbol)


def build_script(symbols, trigger, rounds):
    # The trigger is base64'd so quoting in the host shell cannot change it.
    trig = base64.b64encode(trigger.encode()).decode()
    add = "\n".join(
        f'echo "p:{event_name(s)} {s}" >> kprobe_events'
        for s in symbols
    )
    enable = "\n".join(
        f'echo 1 > events/kprobes/{event_name(s)}/enable'
        for s in symbols
    )
    disable = "\n".join(
        f'echo 0 > events/kprobes/{event_name(s)}/enable 2>/dev/null || true'
        for s in symbols
    )
    remove = "\n".join(
        f'echo "-:{event_name(s)}" >> kprobe_events 2>/dev/null || true'
        for s in symbols
    )
    counts = "\n".join(
        f'n=$(grep -c ": {event_name(s)}:" trace || true); echo "COUNT {s} $n"'
        for s in symbols
    )
    # Remove leftovers from an interrupted previous run before adding.
    pre_remove = remove
    per_round = f"""
: > trace
echo 1 > tracing_on
echo {trig} | base64 -d | sh >/dev/null 2>&1
echo 0 > tracing_on
{counts}
"""
    return f"""
set -e
cd /sys/kernel/tracing 2>/dev/null || {{
    mkdir -p /sys/kernel/debug/tracing
    mount -t tracefs tracefs /sys/kernel/debug/tracing 2>/dev/null || true
    cd /sys/kernel/debug/tracing
}}
test -e kprobe_events
echo 0 > tracing_on
{pre_remove}
{add}
{enable}
{per_round * rounds}
{disable}
{remove}
echo 0 > tracing_on
: > trace
"""


def parse_counts(out, symbols):
    got = {}
    for line in out.splitlines():
        m = re.match(r"COUNT (\S+) (\d+)$", line.strip())
        if m:
            got[m.group(1)] = int(m.group(2))
    missing = [s for s in symbols if s not in got]
    if missing:
        raise RuntimeError(f"counts missing for {missing}:\n{out}")
    return got


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--serial", help="adb serial; default is the only device")
    ap.add_argument("--symbols", default=DEFAULT_SYMBOLS,
                    help="comma-separated probe points")
    ap.add_argument("--trigger", default=DEFAULT_TRIGGER,
                    help="root shell command that performs the opens")
    ap.add_argument("--rounds", type=int, default=1, help="repeat trigger N times")
    ap.add_argument("--json", action="store_true", help="print JSON")
    args = ap.parse_args(argv)

    symbols = [s.strip() for s in args.symbols.split(",") if s.strip()]
    if not symbols:
        ap.error("no symbols")
    if args.rounds < 1:
        ap.error("--rounds must be >= 1")

    script = build_script(symbols, args.trigger, args.rounds)
    try:
        out = run_root(args.serial, script)
    except Exception as e:
        print(f"probe failed: {e}", file=sys.stderr)
        return 2
    try:
        counts = parse_counts(out, symbols)
    except Exception as e:
        print(f"probe failed: {e}", file=sys.stderr)
        # A failed kprobe install leaves the reason in error_log.
        try:
            err = run_root(args.serial, "cd /sys/kernel/tracing; cat error_log 2>/dev/null | tail -20")
            if err.strip():
                print(err, file=sys.stderr)
        except Exception:
            pass
        return 2

    result = {
        "symbols": counts,
        "trigger": args.trigger,
        "rounds": args.rounds,
    }
    # Ratio is useful for the path_openat -> vfs_open question.
    if "path_openat" in counts and "vfs_open" in counts and counts["path_openat"]:
        result["vfs_over_path"] = round(counts["vfs_open"] / counts["path_openat"], 4)
    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        for s in symbols:
            print(f"{s:24s} {counts[s]}")
        if "vfs_over_path" in result:
            print(f"{'vfs_open/path_openat':24s} {result['vfs_over_path']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
