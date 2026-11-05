# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests for the golden-data exporter (proto/tools/export_golden.py) and its .sgg format."""
import shutil
import tempfile
import unittest
from pathlib import Path

import numpy as np

from tools import export_golden

REPO = Path(__file__).resolve().parents[2]


class TestSgg(unittest.TestCase):
    def test_sgg_roundtrip(self):
        arrays = {"sine": np.sin(np.arange(17) / 3.0), "empty": np.zeros(0), "żółw_µ": np.array([1e-320, -0.0, 3.5])}
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "x.sgg"
            export_golden.write_sgg(path, arrays)
            back = export_golden.read_sgg(path)
        self.assertEqual(list(back), list(arrays))
        for name, a in arrays.items():
            self.assertEqual(back[name].dtype, np.float64)
            np.testing.assert_array_equal(back[name], a)
            self.assertEqual(np.signbit(back[name]).tolist(), np.signbit(a).tolist())

    def test_sgg_layout_is_little_endian(self):
        data = export_golden.encode_sgg({"ab": np.array([1.0])})
        self.assertEqual(data, b"SGG1" + (1).to_bytes(4, "little") + (2).to_bytes(2, "little") + b"ab"
                         + (1).to_bytes(8, "little") + np.array([1.0], "<f8").tobytes())


class TestExport(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp)

    def test_export_check_is_deterministic(self):
        a, b = self.tmp / "a", self.tmp / "b"
        self.assertEqual(export_golden.main(["--out", str(a)]), 0)
        self.assertEqual(export_golden.main(["--out", str(b)]), 0)
        names = sorted(p.name for p in a.iterdir())
        self.assertIn("signals.sgg", names)
        for name in names:
            self.assertEqual((a / name).read_bytes(), (b / name).read_bytes(), msg=name)
        self.assertEqual(export_golden.main(["--out", str(a), "--check"]), 0)

    def test_check_detects_a_changed_or_missing_file(self):
        out = self.tmp / "g"
        export_golden.main(["--out", str(out)])
        path = out / "signals.sgg"
        data = bytearray(path.read_bytes())
        data[-1] ^= 1
        path.write_bytes(bytes(data))
        self.assertEqual(export_golden.main(["--out", str(out), "--check"]), 1)
        path.unlink()
        self.assertEqual(export_golden.main(["--out", str(out), "--check"]), 1)

    def test_golden_budget_under_3_mb(self):
        golden = REPO / "tests" / "golden"
        total = sum(p.stat().st_size for p in golden.glob("*.sgg"))
        self.assertGreater(total, 0)
        self.assertLessEqual(total, 3_000_000)


if __name__ == "__main__":
    unittest.main()
