# SPDX-License-Identifier: AGPL-3.0-or-later
"""Result recorder shared by all P2 experiment scripts."""
from __future__ import annotations

import json
import math
from pathlib import Path

import numpy as np

REPO_ROOT = Path(__file__).resolve().parents[2]


def _jsonable(v):
    if isinstance(v, np.ndarray):
        return _jsonable(v.tolist())
    if isinstance(v, np.floating):
        return float(v)
    if isinstance(v, np.integer):
        return int(v)
    if isinstance(v, np.bool_):
        return bool(v)
    if isinstance(v, Path):
        return str(v)
    if isinstance(v, dict):
        return {str(k): _jsonable(x) for k, x in v.items()}
    if isinstance(v, (list, tuple)):
        return [_jsonable(x) for x in v]
    return v


class Recorder:
    """Collects PASS/FAIL checks and INFO values for one experiment `name` (e.g. 'p2_03')."""

    _OPS = ("<=", ">=", "<", ">", "==")

    def __init__(self, name: str, out_dir=None, img_dir=None, verbose: bool = True):
        self.name = name
        self.out_dir = Path(out_dir) if out_dir else REPO_ROOT / "proto" / "out"
        self.img_dir = Path(img_dir) if img_dir else REPO_ROOT / "docs" / "img"
        self.verbose = verbose
        self.checks: list[dict] = []
        self.infos: dict = {}

    def check(self, label, value, op, threshold, unit="", tol=1e-9) -> bool:
        if op not in self._OPS:
            raise ValueError(f"unknown op {op!r}")
        v = float(value)
        if math.isnan(v):
            ok = False
        elif op == "<=":
            ok = v <= threshold
        elif op == ">=":
            ok = v >= threshold
        elif op == "<":
            ok = v < threshold
        elif op == ">":
            ok = v > threshold
        else:
            ok = abs(v - threshold) <= tol
        self.checks.append({"label": label, "value": v, "op": op, "threshold": threshold, "unit": unit, "passed": ok})
        if self.verbose:
            print(f"[{'PASS' if ok else 'FAIL'}] {label}: {v:.6g} {op} {threshold} {unit}".rstrip())
        return ok

    def info(self, label, value) -> None:
        self.infos[label] = _jsonable(value)
        if self.verbose:
            print(f"[INFO] {label}: {value}")

    def save(self) -> Path:
        self.out_dir.mkdir(parents=True, exist_ok=True)
        passed = sum(1 for c in self.checks if c["passed"])
        data = {
            "name": self.name,
            "summary": {"passed": passed, "failed": len(self.checks) - passed},
            "checks": _jsonable(self.checks),
            "info": self.infos,
        }
        path = self.out_dir / f"{self.name}.json"
        path.write_text(json.dumps(data, indent=2))
        return path

    def fig_path(self, suffix: str) -> Path:
        self.img_dir.mkdir(parents=True, exist_ok=True)
        return self.img_dir / f"{self.name}_{suffix}.png"
