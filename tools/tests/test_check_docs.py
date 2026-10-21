# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests for tools/check_docs.py (SPEC_BUILD_RELEASE §11)."""
import contextlib
import io
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
DOCS = TOOLS.parent / "docs"
sys.path.insert(0, str(TOOLS))

import check_docs  # noqa: E402


class TestCheckDocs(unittest.TestCase):
    def _docs_with(self, spec_text: str) -> Path:
        tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, tmp)
        for name in ("PARAMETERS.md", "TEST_PLAN.md", "SPEC_IO_UI.md"):
            shutil.copy(DOCS / name, tmp / name)
        (tmp / "SPEC.md").write_text(spec_text, encoding="utf-8")
        return tmp

    def test_registry_has_141_ids(self):
        ids = check_docs.registry_ids((DOCS / "PARAMETERS.md").read_text(encoding="utf-8"))
        self.assertEqual(len(ids), 141)
        self.assertEqual(len(set(ids)), 141)
        self.assertIn("eq5_q", ids)
        self.assertNotIn("eqN_q", ids)

    def test_unknown_backtick_id_is_reported(self):
        docs = self._docs_with("Uses `conf_gate`, `x_b` and `no_such_param`.\n")
        problems = check_docs.check(docs, TOOLS / "docs_allowlist.txt")
        self.assertEqual([p for p in problems if "no_such_param" in p and "SPEC.md" in p], problems)
        self.assertEqual(len(problems), 1)

    def test_unknown_test_id_is_reported(self):
        docs = self._docs_with("Covered by T-DIV-01 and T-FOO-99.\n")
        problems = check_docs.check(docs, TOOLS / "docs_allowlist.txt")
        self.assertEqual(len(problems), 1)
        self.assertIn("T-FOO-99", problems[0])

    def test_test_id_ranges_and_lists_are_expanded(self):
        docs = self._docs_with("T-DIV-01…09, T-STAB-01/02/04 and T-DIV-01…12.\n")
        problems = check_docs.check(docs, TOOLS / "docs_allowlist.txt")
        self.assertEqual(sorted(p.split()[-1] for p in problems), ["T-DIV-10", "T-DIV-11", "T-DIV-12"])

    def test_current_docs_pass(self):
        self.assertEqual(check_docs.check(DOCS, TOOLS / "docs_allowlist.txt"), [])

    def test_main_exit_codes(self):
        self.assertEqual(check_docs.main(["--docs", str(DOCS)]), 0)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(check_docs.main(["--docs", str(self._docs_with("`no_such_param`\n"))]), 1)
        self.assertIn("no_such_param", out.getvalue())


if __name__ == "__main__":
    unittest.main()
