#!/usr/bin/env python3
"""Picks the German dictionary words for the dialog compression (host/lang.c load_dict).

    build_dict.py texts_de.tsv OUT/dictionary.txt [--max 183]

Reads the German dialog texts (key<TAB>text, as for Languages/German/texts.tsv), counts words
and words with their following space, and keeps those that save the most bytes: each use of a
word of n letters stored as a 2-byte code saves n - 2 bytes. A trailing space is written as "_".
The game's own 67 words (English, e.g. Digimon, Domain, Tamer) are used anyway and skipped here.
The output lists common German words; it is built from your translation, keep it with texts.tsv."""
import collections
import re
import sys

GAME_WORDS = {
    "Digimon", "you", "the", "Digi-Beetle", "Domain", "Guard", "Tamer", "here", "have", "Knights", "and",
    "thing", "Security", "that", "Bertran", "Tournament", "Crimson", "Vendor", "something", "Item", "Falcon",
    "for", "That's", "Commander", "Blood", "Leader", "Attendant", "Cecilia", "all", "mission", "this",
    "MasterTyrannomon", "Archive", "Black", "I'll", "are", "Sword", "right", "digivolve", "enter", "What",
    "will", "come", "You", "Coliseum", "about", "don't", "anything", "Vandar", "Parts", "where", "The", "know",
    "Leomon", "want", "Oldman", "like", "need", "Chief", "with", "Thank", "strange", "Island", "can", "really",
    "Blue", "time",
}


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    mx = 183
    if "--max" in sys.argv:
        mx = int(sys.argv[sys.argv.index("--max") + 1])
    src, out = args[0], args[1]
    count = collections.Counter()
    for line in open(src, encoding="utf-8"):
        if line.startswith("#") or "\t" not in line:
            continue
        key, text = line.rstrip("\n").split("\t", 1)
        plain = re.sub(r"\{[^}]*\}", "\n", text)  # commands and layout split the word runs
        for run in plain.split("\n"):
            words = re.findall(r"[A-Za-zÄÖÜäöüß0-9'\-]+ ?", run)
            for w in words:
                base = w.rstrip(" ")
                if base in GAME_WORDS or len(base) < 3:
                    continue
                count[base] += 1
                if w.endswith(" "):
                    count[w] += 1
    score = sorted(((c * (len(w) - 2), w) for w, c in count.items() if len(w) >= 4), reverse=True)
    picked = []
    for s, w in score:
        if len(picked) >= mx:
            break
        if w.endswith(" ") and w.rstrip(" ") in picked and count[w] * 3 < count[w.rstrip(" ")] * 2:
            continue  # the word alone already covers most of its uses
        picked.append(w)
    with open(out, "w", encoding="utf-8") as f:
        f.write("# German dictionary words for the dialog compression (from your texts.tsv). '_' = space.\n")
        for w in picked:
            f.write(w.replace(" ", "_") + "\n")
    saved = sum(count[w] * (len(w) - 2) for w in picked)
    print("%d words, about %d bytes saved (before overlaps)" % (len(picked), saved))


if __name__ == "__main__":
    sys.exit(main())
