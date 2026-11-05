# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests for the experiment result recorder."""
import json
import tempfile
import unittest
from pathlib import Path

import numpy as np

from experiments._common import Recorder


class TestRecorder(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.rec = Recorder("t_common", out_dir=Path(self.tmp.name) / "out", img_dir=Path(self.tmp.name) / "img",
                            verbose=False)

    def test_recorder_check_ops(self):
        r = self.rec
        self.assertTrue(r.check("le", 1.0, "<=", 2.0))
        self.assertTrue(r.check("le-equal", 2.0, "<=", 2.0))
        self.assertFalse(r.check("le-fail", 3.0, "<=", 2.0))
        self.assertTrue(r.check("ge", 2.0, ">=", 2.0))
        self.assertFalse(r.check("lt-equal", 2.0, "<", 2.0))
        self.assertTrue(r.check("gt", 2.0, ">", 1.0))
        self.assertTrue(r.check("eq-tol", 1.0000001, "==", 1.0, tol=1e-6))
        self.assertFalse(r.check("eq-fail", 1.1, "==", 1.0, tol=1e-6))

    def test_recorder_nan_never_passes(self):
        # A NaN measurement must be a failure for every operator, never a silent pass.
        for op in ("<=", ">=", "<", ">", "=="):
            self.assertFalse(self.rec.check("nan-" + op, float("nan"), op, 1.0))

    def test_recorder_save_writes_json_with_summary(self):
        self.rec.check("ok", np.float64(1.0), "<=", 2.0, unit="dB")
        self.rec.check("bad", 5.0, "<=", 2.0, unit="dB")
        self.rec.info("note", np.array([1.0, 2.0]))
        path = self.rec.save()
        self.assertEqual(path.name, "t_common.json")
        data = json.loads(path.read_text())
        self.assertEqual(data["name"], "t_common")
        self.assertEqual(data["summary"], {"passed": 1, "failed": 1})
        self.assertEqual(data["checks"][0]["unit"], "dB")
        self.assertEqual(data["info"]["note"], [1.0, 2.0])

    def test_fig_path_naming_and_directory(self):
        p = self.rec.fig_path("spectrum")
        self.assertEqual(p.name, "t_common_spectrum.png")
        self.assertTrue(p.parent.is_dir())


if __name__ == "__main__":
    unittest.main()
