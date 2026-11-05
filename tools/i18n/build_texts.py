#!/usr/bin/env python3
"""Builds Languages/German/texts.tsv from the inventory and translation files (local only).

    build_texts.py texts_en.tsv translations.tsv OUT/texts.tsv

texts_en.tsv: from `dw2 --export-texts DIR` (key, category, kind, English).
translations.tsv: key<TAB>German, written by the translator.
Checks every line: the German text must have the same control tokens ({NL}, {PAGE}, {WAIT},
{ARGn}, {TAMER}, {BEETLE}, {DIGI:n}, {ITEM:n}, {B:..}) in the same order as the English one
({G:..} glyphs may differ). Lines that fail are reported and left out.
Both input files and the output are derived from the game: keep them off public repositories."""
import re
import sys


def toks(s):
    return [t for t in re.findall(r"\{[^}]*\}", s) if not t.startswith("{G:")]


def main():
    inv, tr, out = sys.argv[1:4]
    en = {}
    for line in open(inv, encoding="utf-8"):
        f = line.rstrip("\n").split("\t")
        if len(f) >= 4 and f[0] != "key":
            en[f[0]] = f[3]
    good, bad = [], []
    for line in open(tr, encoding="utf-8"):
        if line.startswith("#") or "\t" not in line:
            continue
        key, de = line.rstrip("\n").split("\t", 1)
        if key not in en:
            bad.append((key, "unknown key"))
        elif toks(en[key]) != toks(de):
            bad.append((key, "control tokens differ"))
        else:
            good.append(key + "\t" + de)
    with open(out, "w", encoding="utf-8") as f:
        f.write("# German texts (key = file:pointer offset). Derived from the game: do not share.\n")
        f.write("\n".join(good) + "\n")
    print("%d texts written, %d rejected" % (len(good), len(bad)))
    for k, why in bad[:20]:
        print("REJECTED", k, why)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
