#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Generate one kbuild translation unit per public header (no prior includes)."""

import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("output", type=Path)
    args = ap.parse_args()
    headers = json.loads((ROOT / "tools/layer_manifest.json").read_text())[
        "header_contract"]["standalone"]
    args.output.mkdir(parents=True, exist_ok=True)
    objects = []
    for header in headers:
        if Path(header).name != header or not (ROOT / "kernel" / header).is_file():
            raise ValueError(f"invalid standalone header: {header}")
        stem = "check_" + Path(header).stem
        # The header must supply all its own types and annotations. module.h
        # comes afterwards only to give modpost its license declaration.
        (args.output / (stem + ".c")).write_text(
            f'#include "{header}"\n#include <linux/module.h>\n'
            'MODULE_LICENSE("GPL");\n', encoding="utf-8")
        objects.append(stem + ".o")
    (args.output / "Makefile").write_text(
        'ccflags-y += -Werror -I"' + (ROOT / "kernel").as_posix() + '"\n'
        + "obj-m := " + " ".join(objects) + "\n", encoding="utf-8")
    print(f"header smoke: {len(headers)} independent translation units in {args.output}")


if __name__ == "__main__":
    main()
