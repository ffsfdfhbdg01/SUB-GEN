#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The parameter registry for C++: PARAMETERS.md §2 -> src/params/ParamSpecs.inc.

Every row of the registry tables becomes, in document order, one line
    SUBGEN_PARAM(id, "Name", Type, min, max, default, Scale, Smoothing, automatable, "choices", "unit")
eqN_* rows expand band by band (eq1_on ... eq1_q, eq2_on, ...) with the band defaults table. The document's notation
is read as written: Unicode minus, decimal comma, "…" ranges, "·" lists. Type: Float, Choice, Bool. Scale: Lin, Log,
LinDb, LinDbOff (a gain whose minimum, -60 dB, is Off: exactly 0). Smoothing: None, G20, F20, L20, X5 ... X40
(crossfades). A choice defaults to its index and lists its labels with "|"; a bool lists its two labels and defaults
to 0 or 1. Names, labels and units are UTF-8 (non-ASCII bytes escaped in the C++ literals); names are made ASCII for
hosts.

Usage: gen_params.py [--params docs/PARAMETERS.md] [--out src/params/ParamSpecs.inc] [--check]
"""
from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
EQ_BANDS = 5

ROW = re.compile(r"^\|\s*`([A-Za-z0-9_]+)`\s*\|")
NUMBER = r"[−+-]?\d+(?:,\d+)?"
RANGE = re.compile(rf"^({NUMBER})…({NUMBER})\s*(.*)$")
EQ_DEFAULT = re.compile(r"(\d) — ([A-Za-z ]+?), (" + NUMBER + r") Hz, Q (" + NUMBER + r")")
SMOOTHING = {"G20", "F20", "L20"}


@dataclass
class Spec:
    id: str
    name: str
    type: str  # Float, Choice, Bool
    min: float
    max: float
    default: float
    scale: str  # Lin, Log, LinDb, LinDbOff
    smoothing: str  # None, G20, F20, L20, X<ms>
    automatable: bool
    choices: list[str] = field(default_factory=list)
    unit: str = ""


def number(text: str) -> float:
    return float(text.strip().replace("−", "-").replace(",", ".").replace("+", ""))


def crossfade(text: str) -> str:
    m = re.match(r"X\s+(\d+)\s*ms", text)
    return f"X{m.group(1)}" if m else "None"


def labels(text: str) -> list[str]:
    parts = [p.strip() for p in text.split("·")]
    unit = re.match(r"^(.*?\d)\s+(dB/okt)$", parts[-1])
    if unit:  # "12 · 24 · 48 · 96 dB/okt": the unit belongs to every value
        parts[-1] = unit.group(1)
        parts = [f"{p} dB/oct" for p in parts]
    return parts


def eq_defaults(document: str) -> dict[int, tuple[str, float, float]]:
    line = next(l for l in document.splitlines() if l.startswith("Wartości domyślne pasm:"))
    return {int(b): (kind, number(f), number(q)) for b, kind, f, q in EQ_DEFAULT.findall(line)}


def parse_row(cells: list[str], band: int | None, bands: dict[int, tuple[str, float, float]]) -> Spec:
    pid, name, kind, values, default, scale_smoothing, auto = cells[:7]
    pid = pid.strip("`")
    if band is not None:
        pid, name = pid.replace("N", str(band)), name.replace("N", str(band))
    name = name.replace("−", "-")
    automatable = auto == "A"
    if kind == "float":
        m = RANGE.match(values)
        lo, hi, rest = number(m.group(1)), number(m.group(2)), m.group(3)
        unit = re.sub(r"\(.*?\)", "", rest).strip()
        scale_text, _, smooth_text = (s.strip() for s in scale_smoothing.partition("·"))
        smoothing = smooth_text if smooth_text in SMOOTHING else "None"
        scale = {"lin": "Lin", "log": "Log", "lin dB": "LinDb"}[scale_text]
        if scale == "LinDb" and lo == -60.0 and ("Off" in rest or smoothing == "G20"):
            scale = "LinDbOff"
        if band is not None and default.startswith("wg tabeli"):
            default_value = bands[band][1] if pid.endswith("_freq") else bands[band][2]
        else:
            default_value = number(default)
        return Spec(pid, name, "Float", lo, hi, default_value, scale, smoothing, automatable, [], unit)
    choices = labels(values)
    if band is not None and default.startswith("wg tabeli"):
        default = bands[band][0]
    index = next(i for i, c in enumerate(choices) if c == default or c.startswith(default + " "))
    return Spec(pid, name, "Bool" if kind == "bool" else "Choice", 0.0, float(len(choices) - 1), float(index), "Lin",
                crossfade(scale_smoothing), automatable, choices, "")


def parse(document: str) -> list[Spec]:
    """The registry rows of §2 in document order, eqN_* expanded band by band."""
    bands = eq_defaults(document)
    specs, in_registry, pattern = [], False, []
    for line in document.splitlines():
        if line.startswith("## "):
            in_registry = line.startswith("## 2.")
        if pattern and not (in_registry and ROW.match(line)):
            specs += [parse_row(cells, b, bands) for b in range(1, EQ_BANDS + 1) for cells in pattern]
            pattern = []
        if not (in_registry and ROW.match(line)):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if "N" in cells[0]:
            pattern.append(cells)
        else:
            specs.append(parse_row(cells, None, bands))
    return specs


def literal(text: str) -> str:
    """A C++ string literal; non-ASCII bytes as \\x escapes (a split literal keeps the next hex digit apart)."""
    out, escaped = [], False
    for ch in text:
        if ord(ch) < 128:
            if escaped and ch in "0123456789abcdefABCDEF":
                out.append('" "')
            out.append("\\" + ch if ch in '"\\' else ch)
            escaped = False
        else:
            out += [f"\\x{b:02x}" for b in ch.encode("utf-8")]
            escaped = True
    return '"' + "".join(out) + '"'


def render(specs: list[Spec]) -> str:
    lines = ["// SPDX-License-Identifier: AGPL-3.0-or-later",
             "// Generated by tools/gen_params.py from docs/PARAMETERS.md: do not edit. Regenerate after changing the",
             "// registry; ctest gen_params_check fails while this file is stale.",
             "// SUBGEN_PARAM(id, name, type, min, max, default, scale, smoothing, automatable, choices, unit)"]
    for s in specs:
        lines.append(f"SUBGEN_PARAM({s.id}, {literal(s.name)}, {s.type}, {s.min!r}, {s.max!r}, {s.default!r}, "
                     f"{s.scale}, {s.smoothing}, {'true' if s.automatable else 'false'}, "
                     f"{literal('|'.join(s.choices))}, {literal(s.unit)})")
    return "\n".join(lines) + "\n"


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Generate src/params/ParamSpecs.inc from PARAMETERS.md.")
    ap.add_argument("--params", type=Path, default=REPO / "docs" / "PARAMETERS.md")
    ap.add_argument("--out", type=Path, default=REPO / "src" / "params" / "ParamSpecs.inc")
    ap.add_argument("--check", action="store_true", help="fail when --out differs from the registry")
    args = ap.parse_args(argv)
    text = render(parse(args.params.read_text(encoding="utf-8")))
    if args.check:
        if not args.out.exists() or args.out.read_text(encoding="utf-8") != text:
            print(f"{args.out}: stale, run tools/gen_params.py", file=sys.stderr)
            return 1
        return 0
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
