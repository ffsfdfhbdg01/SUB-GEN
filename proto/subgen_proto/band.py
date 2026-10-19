# SPDX-License-Identifier: AGPL-3.0-or-later
"""Source band x_b (SPEC §5.3)."""
from __future__ import annotations

from . import svf


def range_filter(x, fs: float, lo: float = 30.0, hi: float = 120.0, order: int = 4):
    """x_b = LP(hi) o HP(lo): Butterworth sections of the given order on each side, minimum phase."""
    return svf.process_fast(svf.design_butter(order, lo, fs, "high") + svf.design_butter(order, hi, fs, "low"), x)
