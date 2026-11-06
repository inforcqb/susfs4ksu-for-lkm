#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Positive/negative cases for tools/layering_check.py."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import layering_check as lc  # noqa: E402


class LayeringTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="layer_test_", dir=ROOT / "tools"))

    def tearDown(self) -> None:
        for p in self.tmp.iterdir():
            p.unlink()
        self.tmp.rmdir()

    def write(self, text: str) -> Path:
        p = self.tmp / "x.c"
        p.write_text(text, encoding="utf-8")
        return p

    def test_comments_strings_and_if0_ignored(self) -> None:
        p = self.write(
            "/* #include <linux/namei.h> */\n"
            "#if 0\n#include <linux/namei.h>\nvfs_mkdir();\n#endif\n"
            'const char *s = "vfs_mkdir";\n'
            "// #include <linux/path.h>\n"
        )
        self.assertEqual(lc.check_file(p, {"layer": "F"}), [])

    def test_real_violations_found(self) -> None:
        p = self.write("#include <linux/namei.h>\nvfs_mkdir();\n")
        rules = {v["rule"] for v in lc.check_file(p, {"layer": "F"})}
        self.assertIn("F_INCLUDE", rules)
        self.assertIn("F_SYMBOL", rules)

    def test_path_is_posix(self) -> None:
        p = self.write("// nothing\n")
        v = lc.check_file(p, {"layer": "F"})
        self.assertEqual(v, [])
        self.assertNotIn("\\", lc.rel_posix(p))

    def test_if0_else(self) -> None:
        text = "#if 0\n#include <linux/namei.h>\n#else\nok\n#endif\n"
        self.assertNotIn("namei", lc.remove_if0(text))


if __name__ == "__main__":
    unittest.main()
