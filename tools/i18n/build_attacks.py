#!/usr/bin/env python3
"""Builds Languages/German/attacks.csv from the research table
Languages/German/source/digimon_world_2_attacken_de_en.xlsx (sheet "Alle Attacken").

Rules (binding):
  1. "Deutsch im Anime" present -> its FIRST name (alternatives separated by " / ")
  2. else "Deutsch in anderem Spiel" present -> that name
  3. else the English DW2 name unchanged (no own translation)
A dash "–" / "-" / empty counts as not present. Sources and notes are kept.
Output: UTF-8, ';' separated, one line per English DW2 name:
  english;german;source;status;digimon;check_note;source_de;source_dw2
"""
import csv
import os
import sys

import openpyxl

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "Languages/German/source/digimon_world_2_attacken_de_en.xlsx")
OUT = os.path.join(ROOT, "Languages/German/attacks.csv")


def present(v):
    return v is not None and str(v).strip() not in ("", "–", "-", "—")


def first_alt(v):
    # alternatives are " / " separated; a "/" without spaces (none in the table) would stay one name
    return str(v).split(" / ")[0].strip()


def main():
    ws = openpyxl.load_workbook(SRC)["Alle Attacken"]
    rows = list(ws.iter_rows(min_row=2, values_only=True))
    seen = {}
    out = []
    problems = []
    for r in rows:
        digi, de_anime, en, de_other, kind, status, src_de, src_dw2, note = (list(r) + [None] * 9)[:9]
        if not present(en):
            continue
        en = str(en).strip()
        if present(de_anime):
            de, source = first_alt(de_anime), "anime (first name)"
        elif present(de_other):
            de, source = str(de_other).strip(), "other game"
        else:
            de, source = en, "english (no verified German name)"
        for field in (de, en):
            if ";" in field:
                problems.append("';' in %r" % field)
        key = en.lower()
        if key in seen:
            if seen[key] != de:
                problems.append("%s: two different German names (%s / %s)" % (en, seen[key], de))
            continue
        seen[key] = de
        out.append([en, de, source, status or "", digi or "", note or "", src_de or "", src_dw2 or ""])
    with open(OUT, "w", encoding="utf-8", newline="") as f:
        w = csv.writer(f, delimiter=";", quoting=csv.QUOTE_MINIMAL)
        w.writerow(["english", "german", "source", "status", "digimon", "check_note", "source_de", "source_dw2"])
        w.writerows(out)
    n_de = sum(1 for x in out if not x[2].startswith("english"))
    print("%d attacks, %d with a German name (%d anime, %d other game), %d stay English" % (
        len(out), n_de, sum(1 for x in out if x[2].startswith("anime")), sum(1 for x in out if x[2] == "other game"),
        len(out) - n_de))
    for p in problems:
        print("CHECK:", p)
    return 0


if __name__ == "__main__":
    sys.exit(main())
