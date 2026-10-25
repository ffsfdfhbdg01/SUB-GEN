# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests for tools/gen_params.py: PARAMETERS.md §2 -> src/params/ParamSpecs.inc."""
import contextlib
import io
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
REPO = TOOLS.parent
sys.path.insert(0, str(TOOLS))

import gen_params  # noqa: E402


class TestGenParams(unittest.TestCase):
    def setUp(self):
        self.specs = gen_params.parse((REPO / "docs" / "PARAMETERS.md").read_text(encoding="utf-8"))
        self.by_id = {s.id: s for s in self.specs}

    def test_parses_141_ids(self):
        ids = [s.id for s in self.specs]
        self.assertEqual(len(ids), 141)
        self.assertEqual(len(set(ids)), 141)
        self.assertTrue(all(i.isascii() and len(i) <= 24 for i in ids))
        self.assertEqual(ids[:3], ["bypass", "quality", "out_trim"])
        # the EQ pattern expands band by band, with the band defaults
        self.assertEqual(ids[ids.index("eq1_on"):ids.index("eq1_on") + 6],
                         ["eq1_on", "eq1_type", "eq1_freq", "eq1_gain", "eq1_q", "eq2_on"])
        self.assertEqual((self.by_id["eq1_type"].default, self.by_id["eq1_freq"].default, self.by_id["eq1_q"].default),
                         (1.0, 40.0, 0.71))
        self.assertEqual(self.by_id["eq5_type"].default, 2.0)  # High Shelf
        self.assertEqual(self.by_id["eq3_freq"].name, "Band 3 Freq")

    def test_rows_read_the_document_notation(self):
        s = self.by_id["sub_level"]
        self.assertEqual((s.type, s.min, s.max, s.default, s.scale, s.smoothing, s.automatable, s.unit),
                         ("Float", -60.0, 18.0, -6.0, "LinDbOff", "G20", True, "dB"))
        self.assertEqual(self.by_id["gate_range"].scale, "LinDb")  # a depth: -60 dB is not Off
        self.assertEqual(self.by_id["sub2_level"].scale, "LinDbOff")
        q = self.by_id["quality"]
        self.assertEqual((q.type, q.choices, q.default, q.smoothing, q.automatable),
                         ("Choice", ["Live", "Mix", "Print"], 1.0, "X40", False))
        self.assertEqual(self.by_id["sub_hp_slope"].choices, ["12 dB/oct", "24 dB/oct", "48 dB/oct", "96 dB/oct"])
        self.assertEqual(self.by_id["sub_hp_slope"].default, 1.0)
        self.assertEqual(self.by_id["comp_attack"].min, 0.1)
        self.assertEqual(self.by_id["comp_attack"].scale, "Log")
        self.assertEqual(self.by_id["sub1_level"].name, "Sub -1 Level")  # ASCII names for hosts
        self.assertEqual((self.by_id["sub_pol"].type, self.by_id["sub_pol"].choices), ("Bool", ["0°", "180°"]))
        self.assertEqual(self.by_id["boost_follow"].default, 1.0)
        self.assertEqual([i for i, s in self.by_id.items() if not s.automatable], ["quality", "filt_phase", "align_ms"])

    def test_check_detects_a_stale_file(self):
        tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, tmp)
        out = tmp / "ParamSpecs.inc"
        params = REPO / "docs" / "PARAMETERS.md"
        self.assertEqual(gen_params.main(["--params", str(params), "--out", str(out)]), 0)
        self.assertEqual(gen_params.main(["--params", str(params), "--out", str(out), "--check"]), 0)
        out.write_text(out.read_text(encoding="utf-8").replace("bypass", "bypas", 1), encoding="utf-8")
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(gen_params.main(["--params", str(params), "--out", str(out), "--check"]), 1)

    def test_the_committed_file_is_current(self):
        self.assertEqual(gen_params.main(["--check"]), 0)


if __name__ == "__main__":
    unittest.main()
