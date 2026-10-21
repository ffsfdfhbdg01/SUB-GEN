#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Documentation consistency check (SPEC_BUILD_RELEASE §11).

(a) PARAMETERS.md §2 holds exactly 141 unique parameter IDs once the eqN_* pattern is expanded (N = 1..5);
(b) every lower_snake identifier in backticks in SPEC.md, SPEC_IO_UI.md and TEST_PLAN.md is a parameter ID or is
    listed in tools/docs_allowlist.txt (internal signals and quantities, reserved IDs); a pattern such as `route_*`
    or `eqN_freq` must match at least one parameter ID or be listed itself;
(c) every test ID cited in docs/*.md, including ranges (T-DIV-01…07) and lists (T-STAB-01/02/04), is defined in
    TEST_PLAN.md.

Usage: check_docs.py [--docs DIR] [--allowlist FILE]; exit 0 when clean, else 1 with one line per problem.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

EXPECTED_PARAMS = 141
EQ_BANDS = 5
ID_DOCS = ("SPEC.md", "SPEC_IO_UI.md", "TEST_PLAN.md")

ROW_ID = re.compile(r"^\|\s*`([A-Za-z0-9_]+)`\s*\|")
BACKTICK = re.compile(r"`([^`\n]+)`")
IDENT = re.compile(r"[a-z][a-z0-9]*(?:_[a-z0-9]+)*")
ID_PATTERN = re.compile(r"[a-z][a-z0-9]*N?(?:_[a-z0-9]+)*_?\*?")  # eqN_freq, route_*, eqN_*
TEST_DEF = re.compile(r"^\|\s*(T-[A-Z]+-\d{2})\s*\|", re.M)
TEST_REF = re.compile(r"T-([A-Z]+)-(\d{2})((?:\s*(?:…|\.\.\.)\s*\d{2})|(?:/\d{2})+)?")


def registry_ids(parameters_md: str) -> list[str]:
    """Parameter IDs of the registry tables in §2, eqN_* expanded, in document order (duplicates kept)."""
    ids, in_registry = [], False
    for line in parameters_md.splitlines():
        if line.startswith("## "):
            in_registry = line.startswith("## 2.")
            continue
        m = ROW_ID.match(line) if in_registry else None
        if not m:
            continue
        pid = m.group(1)
        ids += [pid.replace("N", str(n)) for n in range(1, EQ_BANDS + 1)] if "N" in pid else [pid]
    return ids


def read_allowlist(path: Path) -> set[str]:
    words = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            words.add(line)
    return words


def _expand_test_ref(m: re.Match) -> list[str]:
    area, first, tail = m.group(1), int(m.group(2)), m.group(3) or ""
    numbers = [first]
    if "/" in tail:
        numbers += [int(n) for n in tail.split("/")[1:]]
    elif tail:
        last = int(re.findall(r"\d{2}", tail)[0])
        numbers = list(range(first, last + 1))
    return [f"T-{area}-{n:02d}" for n in numbers]


def check(docs: Path, allowlist: Path) -> list[str]:
    """All problems found in the documents in `docs`, one line each; empty when consistent."""
    problems = []
    ids = registry_ids((docs / "PARAMETERS.md").read_text(encoding="utf-8"))
    if len(ids) != EXPECTED_PARAMS:
        problems.append(f"PARAMETERS.md: {len(ids)} parameter IDs, expected {EXPECTED_PARAMS}")
    seen = set()
    for pid in ids:
        if pid in seen:
            problems.append(f"PARAMETERS.md: duplicate parameter ID {pid}")
        seen.add(pid)
    allowed = read_allowlist(allowlist)
    known = seen | allowed

    for name in ID_DOCS:
        path = docs / name
        if not path.exists():
            continue
        for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            for span in (s.strip() for s in BACKTICK.findall(line)):
                if IDENT.fullmatch(span):
                    if span not in known:
                        problems.append(f"{name}:{lineno}: unknown identifier `{span}`")
                elif ID_PATTERN.fullmatch(span) and ("N" in span or span.endswith("*")):
                    rx = re.compile(re.escape(span).replace("N", "[1-9]").replace(r"\*", ".*"))
                    if span not in allowed and not any(rx.fullmatch(pid) for pid in seen):
                        problems.append(f"{name}:{lineno}: pattern `{span}` matches no parameter ID")

    defined = set(TEST_DEF.findall((docs / "TEST_PLAN.md").read_text(encoding="utf-8")))
    for path in sorted(docs.glob("*.md")):
        for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            for m in TEST_REF.finditer(line):
                for tid in _expand_test_ref(m):
                    if tid not in defined:
                        problems.append(f"{path.name}:{lineno}: unknown test {tid}")
    return problems


def main(argv: list[str] | None = None) -> int:
    here = Path(__file__).resolve().parent
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--docs", type=Path, default=here.parent / "docs")
    ap.add_argument("--allowlist", type=Path, default=here / "docs_allowlist.txt")
    args = ap.parse_args(argv)
    problems = check(args.docs, args.allowlist)
    for p in problems:
        print(p)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
