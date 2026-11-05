#!/usr/bin/env python3
"""Exploration run: new game, then tap a button pattern for N waits, logging game mode changes
and a screenshot every K waits; stops early when the picture has not changed for a while.

    explore.py EXE PAK WORKDIR [--waits N] [--every K] [--card FILE] [--pattern Cross]
"""
import argparse
import hashlib
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dw2bot import Game  # noqa: E402


def new_game(g):
    g.run(600)
    g.tap("Start")
    g.run(120)
    g.tap("Up")
    g.run(20)
    g.tap("Cross")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("pak")
    ap.add_argument("work")
    ap.add_argument("--waits", type=int, default=6000)
    ap.add_argument("--every", type=int, default=300)
    ap.add_argument("--pattern", default="Cross")
    ap.add_argument("--card")
    ap.add_argument("--rule", action="append", default=[],
                    help="MODE:BUTTONS: in game mode MODE (hex) tap BUTTONS once, then go on with the pattern")
    a = ap.parse_args()
    os.makedirs(os.path.join(a.work, "saves"), exist_ok=True)
    if a.card:
        shutil.copy(a.card, os.path.join(a.work, "saves", "card1.mcd"))
    g = Game(a.exe, a.pak, a.work)
    new_game(g)
    last_mode, last_hash, same = None, None, 0
    start = g.wait
    try:
        while g.wait - start < a.waits:
            m = g.mode()
            rule = [r.split(":", 1)[1] for r in a.rule if int(r.split(":", 1)[0], 16) == m]
            if rule:
                for b in rule[0].split(","):
                    if b.startswith("w") and b[1:].isdigit():
                        g.run(int(b[1:]))
                    else:
                        g.tap(b, 3, 17)
                a.rule = [r for r in a.rule if int(r.split(":", 1)[0], 16) != m]
            g.tap(a.pattern, 3, 17)
            m = g.mode()
            if m != last_mode:
                print("wait %d mode 0x%x" % (g.wait, m), flush=True)
                last_mode = m
            if (g.wait - start) // a.every != (g.wait - start - 20) // a.every:
                p = g.shot("e%06d" % g.wait)
                h = hashlib.md5(open(p, "rb").read()).hexdigest()
                same = same + 1 if h == last_hash else 0
                last_hash = h
                if same >= 6:
                    print("picture unchanged for %d shots: stop at wait %d" % (same, g.wait))
                    break
    finally:
        g.close()


if __name__ == "__main__":
    main()
