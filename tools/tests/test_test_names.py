# SPDX-License-Identifier: AGPL-3.0-or-later
"""The C++ test names are ASCII: catch_discover_tests hands each name to the test executable on its command line, and on
Windows that passes through the ANSI code page, so a name with "§" matched no test there (CI, 2026-11-03)."""
import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
TEST_CASE = re.compile(r'TEST_CASE\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)')


class TestTestNames(unittest.TestCase):
    def test_every_test_name_is_ascii(self):
        files = sorted((REPO / "tests").glob("**/*.cpp"))
        self.assertTrue(files)
        found = 0
        for path in files:
            for m in TEST_CASE.finditer(path.read_text(encoding="utf-8")):
                found += 1
                name = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1)))
                with self.subTest(file=path.name, name=name[:60]):
                    self.assertTrue(name.isascii(), f"{path.name}: {name}")
        self.assertGreater(found, 200)


if __name__ == "__main__":
    unittest.main()
