#!/usr/bin/env python3
"""Lays German dialog texts out for the game's message window and checks them (no game data here).

    dialog_wrap.py texts_en.tsv german.tsv OUT.tsv [--width 31] [--lines 3]

german.tsv: key<TAB>German, written like the English inventory line (tokens {B:..}, {TAMER} ...),
line breaks optional. For every text:
  - the command tokens ({B:..}, {ARGn}, {TAMER}, {BEETLE}, {DIGI:n}, {ITEM:n}, {G:..}) must be the
    English ones in the same order; {NL}, {WAIT} and {PAGE} are layout,
  - each page is wrapped again to `width` columns (the font is fixed width; a name counts with its
    longest size), a speaker line ({B:F434}Name{B:F430}) stays the first line of its page,
  - a page with more than `lines` lines is split: "{WAIT}{PAGE}" is added between the parts (the
    game resets its per-page command counters at every page, so this is safe for plain text),
  - the number of {PAGE} may only grow, {WAIT} and {PAGE} of the English text are kept.
Texts that fail are reported and left out (the game keeps English for them).
The input and output files come from the player's disc: keep them off public repositories."""
import re
import sys

NAME_W = {"TAMER": 8, "BEETLE": 8}
ARG_W = 10
DIGI_W = 14  # longest Digimon / item names in the game
FONT = set("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz &?!/-,.'\";:%+=#ÄÖÜäöüß")

TOK = re.compile(r"\{[^}]*\}")


def tokens(s):
    return [t for t in TOK.findall(s) if t not in ("{NL}", "{WAIT}", "{PAGE}")]


def width(piece):
    """columns of a line: glyphs 1, names their longest size, F801/F802 (choice cursor) 1"""
    w = 0
    for part in re.split(r"(\{[^}]*\})", piece):
        if not part:
            continue
        if not part.startswith("{"):
            w += len(part)
            continue
        name = part[1:-1]
        if name in NAME_W:
            w += NAME_W[name]
        elif name.startswith("ARG"):
            w += ARG_W
        elif name.startswith("DIGI:") or name.startswith("ITEM:"):
            w += DIGI_W
        elif name.startswith("G:") or name in ("B:F801", "B:F802"):
            w += 1
    return w


def wrap_words(text, W):
    words = [w for w in re.split(r"(?:\{NL\}| )+", text) if w != ""]
    lines, cur = [], ""
    for w in words:
        cand = w if not cur else cur + " " + w
        if width(cand) <= W or not cur:
            cur = cand
        else:
            lines.append(cur)
            cur = w
    if cur:
        lines.append(cur)
    return lines


def wrap_page(page, W):
    """page text without {PAGE} -> (speaker line or None, lines). Each choice option ({NL} before
    {B:F801} / {B:F802}) starts its own line, as in the English texts."""
    trail = ""
    m = re.search(r"((?:\{WAIT\}|\{B:[0-9A-F]+\})*)$", page)
    if m:
        trail = m.group(1)
        page = page[: m.start()]
    head = None
    m = re.match(r"^((?:\{B:[0-9A-F]+\})*\{B:F434\}.*?\{B:F430\})(?:\{NL\}|$)", page)
    if m:
        head = m.group(1)
        page = page[m.end():]
    lines = []
    for para in re.split(r"\{NL\}(?=\{B:F80[12]\})", page):
        lines += wrap_words(para, W)
    if not lines:
        lines = [""]
    lines[-1] += trail
    return head, lines


def layout(de, W, L):
    out_pages = []
    pages = de.split("{PAGE}")
    for i, page in enumerate(pages):
        last = i == len(pages) - 1
        if last and page.strip() == "" or re.fullmatch(r"(\{B:[0-9A-F]+\})*", page):
            out_pages.append(page)  # the commands after the last page break
            continue
        head, lines = wrap_page(page, W)
        first_cap = L - 1 if head is not None else L
        parts, k = [], 0
        cap = first_cap
        while k < len(lines):
            parts.append(lines[k:k + cap])
            k += cap
            cap = L
        for j, part in enumerate(parts):
            txt = "{NL}".join(part)
            if j == 0 and head is not None:
                txt = head + ("{NL}" + txt if txt else "")
            if j < len(parts) - 1:
                # the part's own trailing commands stay at the very end of the page
                txt += "{WAIT}"
            out_pages.append(txt)
    return "{PAGE}".join(out_pages)


def check(en, de, W, L):
    if tokens(en) != tokens(de):
        return "command tokens differ"
    if de.count("{PAGE}") < en.count("{PAGE}") or de.count("{WAIT}") < en.count("{WAIT}"):
        return "fewer pages or waits than English"
    plain = TOK.sub("", de)
    bad = sorted(set(c for c in plain if c not in FONT))
    if bad:
        return "characters the font has no glyph for: " + "".join(bad)
    for page in de.split("{PAGE}"):
        ls = page.split("{NL}")
        if len(ls) > L:
            return "page with %d lines" % len(ls)
        for line in ls:
            if width(line) > W:
                return "line too wide (%d): %s" % (width(line), TOK.sub("", line))
    return None


def main():
    W, L, files = 31, 3, []
    argv = sys.argv[1:]
    i = 0
    while i < len(argv):
        if argv[i] == "--width":
            W = int(argv[i + 1])
            i += 2
        elif argv[i] == "--lines":
            L = int(argv[i + 1])
            i += 2
        else:
            files.append(argv[i])
            i += 1
    inv, tr, out = files[:3]
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
            continue
        laid = layout(de, W, L)
        why = check(en[key], laid, W, L)
        if why:
            bad.append((key, why))
        else:
            good.append(key + "\t" + laid)
    with open(out, "w", encoding="utf-8") as f:
        f.write("# German texts (key = file:pointer offset). Derived from the game: do not share.\n")
        f.write("\n".join(good) + ("\n" if good else ""))
    print("%d texts laid out, %d rejected" % (len(good), len(bad)))
    for k, why in bad[:40]:
        print("REJECTED", k, why)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
