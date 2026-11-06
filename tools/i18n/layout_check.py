#!/usr/bin/env python3
"""Checks that every translated text can be shown inside its window (no game data here).

    layout_check.py texts_en.tsv texts.tsv SMALLFONT.bin [--report OUT.tsv]

SMALLFONT.bin: the small font file as the game uses it with the German layer (dw2 --lang-dump DIR
writes 013A.bin there). The glyph widths are measured from it the same way host/uilayout.c does.

For each text and line, the width is computed for both UI modes:
  classic  every glyph one 7-pixel cell (the PS1 game),
  modern   letters with their own width + 1 pixel, digits and symbols one cell, space 4 pixels,
  close    letters with their own width only (what the PC layout uses for a line that does not fit).
Names count with their longest size: {TAMER}/{BEETLE} 8 cells, {DIGI:n}/{ITEM:n} 14, {ARGn} 10.

What room a line has:
  dialogs (event tables, "dialog" in the inventory): the message window, 31 cells (217 pixels in
    classic; the window itself has 288 pixels inside),
  all other texts (menus, items, system lines): the English line's classic width is the room the
    game made for it; more than that is fine when the PC layout can grow the window or close-set the
    line, which the screenshots have to show (status "fitted").

Status per text: ok (fits as set in both modes), fitted (longer than the English line, fits close
set within 70 % spacing: the runtime layout handles it), check_on_screen (a menu text that is wider
even then: it only fits when its window can grow or the text next to it can move, which the
screenshots must show), too_wide (a dialog line over 31 cells: rewrap). Exit 1 for too_wide."""
import re
import sys

TOK = re.compile(r"(\{[^}]*\})")
CELL = 7
NAMES = {"TAMER": 8, "BEETLE": 8}
UML = dict(zip("ÄÖÜäöüß", range(0xE6, 0xED)))
PUNCT = {"&": 0x42, "?": 0x44, "!": 0x45, "/": 0x46, "-": 0x49, ",": 0x54, ".": 0x55, "'": 0x56,
         '"': 0x57, ";": 0x58, ":": 0x59, "%": 0x5A, "+": 0x5B, "=": 0x5C, "#": 0x5D}
PROP = set(range(0x0A, 0x3E)) | set(range(0xE6, 0xED)) | {0x42, 0x44, 0x45, 0x46, 0x49} | set(range(0x54, 0x5E))


def font_ink(path):
    d = open(path, "rb").read()
    o = 4 + 8 + 44 + 12
    ink = {}
    for c in range(0xEF):
        x0, y0 = (c % 17) * 7, (c // 17) * 10
        if y0 + 10 > 256:
            continue
        cols = [x for x in range(7) for y in range(10)
                if (d[o + (y0 + y) * 64 + (x0 + x) // 2] >> (((x0 + x) & 1) * 4)) & 15]
        if cols:
            ink[c] = max(cols) - min(cols) + 1
    return ink


def code(ch):
    if ch.isdigit():
        return int(ch)
    if "A" <= ch <= "Z":
        return 10 + ord(ch) - 65
    if "a" <= ch <= "z":
        return 0x24 + ord(ch) - 97
    return UML.get(ch, PUNCT.get(ch))


def widths(line, ink):
    """(classic, modern, close) pixels of one line"""
    cl = mo = cs = 0
    for part in TOK.split(line):
        if not part:
            continue
        if part.startswith("{"):
            name = part[1:-1]
            n = 0
            if name in NAMES:
                n = NAMES[name]
            elif name.startswith("ARG"):
                n = 10
            elif name.startswith("DIGI:") or name.startswith("ITEM:"):
                n = 14
            elif name.startswith("G:") or name in ("B:F801", "B:F802"):
                n = 1
            cl += n * CELL
            mo += n * CELL
            cs += n * CELL
            continue
        for ch in part:
            cl += CELL
            if ch == " ":
                mo += 4
                cs += 4
                continue
            c = code(ch)
            if c is not None and c in PROP and c in ink:
                mo += ink[c] + 1
                cs += ink[c]
            else:
                mo += CELL
                cs += CELL
    return cl, mo, cs


def lines(text):
    for page in text.split("{PAGE}"):
        for line in page.split("{NL}"):
            yield line.replace("{WAIT}", "")


def main():
    args = sys.argv[1:]
    report = None
    if "--report" in args:
        i = args.index("--report")
        report = args[i + 1]
        del args[i:i + 2]
    inv, tr, fontfile = args[:3]
    ink = font_ink(fontfile)
    en, cat = {}, {}
    for l in open(inv, encoding="utf-8"):
        f = l.rstrip("\n").split("\t")
        if len(f) >= 4 and f[0] != "key":
            en[f[0]] = f[3]
            cat[f[0]] = f[1]
    stats = {"ok": 0, "fitted": 0, "check_on_screen": 0, "too_wide": 0}
    rows, dialog_bad = [], 0
    for l in open(tr, encoding="utf-8"):
        if l.startswith("#") or "\t" not in l:
            continue
        key, de = l.rstrip("\n").split("\t", 1)
        if key not in en:
            continue
        dialog = cat[key].startswith("dialog")
        en_room = max(widths(x, ink)[0] for x in lines(en[key]))
        worst, status = None, "ok"
        for x in lines(de):
            cl, mo, cs = widths(TOK.sub(lambda m: "" if m.group(0).startswith("{B:F4") or m.group(0).startswith("{B:F9") else m.group(0), x), ink)
            room = 31 * CELL if dialog else en_room
            if dialog and cl > room:
                s = "too_wide"
            elif mo <= room and cl <= room:
                s = "ok"
            elif cs * 0.70 <= room:
                s = "fitted"
            else:
                s = "too_wide" if dialog else "check_on_screen"
            if dialog and cl > room:
                dialog_bad += 1
            order = ["ok", "fitted", "check_on_screen", "too_wide"]
            if order.index(s) > order.index(status):
                status, worst = s, (x, cl, mo, cs, room)
        stats[status] += 1
        if status != "ok":
            rows.append((key, cat[key], status) + worst)
    print("texts: %d ok, %d fitted by the PC layout, %d to check on screen, %d too wide; dialog lines over 31 cells: %d"
          % (stats["ok"], stats["fitted"], stats["check_on_screen"], stats["too_wide"], dialog_bad))
    if report:
        with open(report, "w", encoding="utf-8") as o:
            o.write("key\tcategory\tstatus\tline\tclassic_px\tmodern_px\tclose_px\troom_px\n")
            for r in rows:
                o.write("\t".join(str(v) for v in r) + "\n")
    for r in rows[:20]:
        if r[2] in ("too_wide", "check_on_screen"):
            print(r[2].upper(), r[0], r[1], TOK.sub("", r[3]), r[4:])
    return 1 if dialog_bad else 0


if __name__ == "__main__":
    sys.exit(main())
