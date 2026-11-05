#!/usr/bin/env python3
"""Dialog translation by text segments: the commands stay where they are, only the words change.

    dialog_segments.py extract texts_en.tsv SEG_EN.tsv SPEAKERS_EN.tsv
    dialog_segments.py build   texts_en.tsv SEG_DE.tsv SPEAKERS_DE.tsv OUT_german.tsv

extract: every dialog text (category "dialog ...") is cut at its commands ({B:..} tokens). Each
piece of words between two commands is a segment; {NL} becomes a space and "{WAIT}{PAGE}" becomes
" ¶ " (a page break). Speaker names ({B:F434}Name{B:F430}) go to the speaker file. Equal segments
are listed once. Segment id = first 12 hex digits of SHA-1 of the English segment.
  SEG_EN.tsv:      id<TAB>English segment<TAB>uses
  SPEAKERS_EN.tsv: English name<TAB>uses
build: puts the German segments (id<TAB>German, ¶ = page break, {TAMER} etc. kept) and speaker
names (English<TAB>German) back between the original commands. A text with a missing segment is
left out (it stays English). Output: key<TAB>German, for tools/i18n/dialog_wrap.py (layout, checks).
All files here come from the player's disc: keep them off public repositories."""
import hashlib
import re
import sys

CMD = re.compile(r"(\{B:[0-9A-F]+\})")
PB = " ¶ "


def sid(text):
    return hashlib.sha1(text.encode("utf-8")).hexdigest()[:12]


LAY = r"(?:\{NL\}|\{WAIT\}|\{PAGE\}| )*"


def split_lay(raw):
    """(leading layout, words, trailing layout) of a raw piece; the layout around stays as it was"""
    m = re.match(r"^(" + LAY + r")(.*?)(" + LAY + r")$", raw, re.S)
    return m.group(1).replace(" ", ""), m.group(2), m.group(3).replace(" ", "")


def to_seg(raw):
    s = split_lay(raw)[1].replace("{WAIT}{PAGE}", PB).replace("{NL}", " ")
    return re.sub(r" +", " ", s).strip()


def pieces(text):
    """[(kind, value)]: ('cmd', token) / ('spk', name) / ('seg', raw words) / ('lay', layout only)"""
    out = []
    parts = CMD.split(text)
    i = 0
    while i < len(parts):
        p = parts[i]
        if p == "{B:F434}" and i + 2 < len(parts) and parts[i + 2] == "{B:F430}":
            out.append(("cmd", p))
            out.append(("spk", parts[i + 1]))
            out.append(("cmd", parts[i + 2]))
            i += 3
            continue
        if CMD.fullmatch(p):
            out.append(("cmd", p))
        elif p:
            out.append(("seg", p) if re.sub(r"\{(WAIT|PAGE|NL)\}| ", "", p) else ("lay", p))
        i += 1
    return out


def load_inv(path):
    rows = []
    for line in open(path, encoding="utf-8"):
        f = line.rstrip("\n").split("\t")
        if len(f) >= 4 and f[0] != "key" and f[1].startswith("dialog"):
            rows.append((f[0], f[3]))
    return rows


def extract(inv, seg_out, spk_out):
    segs, spk = {}, {}
    for key, text in load_inv(inv):
        for kind, v in pieces(text):
            if kind == "seg":
                s = to_seg(v)
                segs.setdefault(s, [sid(s), 0])[1] += 1
            elif kind == "spk":
                spk[v] = spk.get(v, 0) + 1
    with open(seg_out, "w", encoding="utf-8") as f:
        for s, (i, n) in segs.items():
            f.write("%s\t%s\t%d\n" % (i, s, n))
    with open(spk_out, "w", encoding="utf-8") as f:
        for s, n in sorted(spk.items(), key=lambda x: -x[1]):
            f.write("%s\t%d\n" % (s, n))
    print("%d segments (%d characters), %d speaker names" % (len(segs), sum(len(s) for s in segs), len(spk)))


def build(inv, seg_de, spk_de, out):
    de, names = {}, {}
    for line in open(seg_de, encoding="utf-8"):
        if line.startswith("#") or "\t" not in line:
            continue
        f = line.rstrip("\n").split("\t")
        if len(f) >= 2 and f[1].strip():
            de[f[0]] = f[1].strip()
    for line in open(spk_de, encoding="utf-8"):
        f = line.rstrip("\n").split("\t")
        if len(f) >= 2 and not line.startswith("#"):
            names[f[0]] = f[1]
    done, missing = 0, 0
    with open(out, "w", encoding="utf-8") as o:
        for key, text in load_inv(inv):
            res, ok = [], True
            for kind, v in pieces(text):
                if kind in ("cmd", "lay"):
                    res.append(v)
                elif kind == "spk":
                    res.append(names.get(v, v))
                else:
                    t = de.get(sid(to_seg(v)))
                    if t is None:
                        ok = False
                        break
                    lead, _, trail = split_lay(v)
                    body = re.sub(r"\s*¶\s*", "{WAIT}{PAGE}", t)
                    res.append(lead + body + trail)
            if ok:
                o.write(key + "\t" + "".join(res) + "\n")
                done += 1
            else:
                missing += 1
    print("%d texts built, %d still missing segments (stay English)" % (done, missing))


if __name__ == "__main__":
    if len(sys.argv) >= 5 and sys.argv[1] == "extract":
        extract(*sys.argv[2:5])
    elif len(sys.argv) >= 6 and sys.argv[1] == "build":
        build(*sys.argv[2:6])
    else:
        print(__doc__)
        sys.exit(2)
