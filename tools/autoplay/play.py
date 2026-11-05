#!/usr/bin/env python3
"""dw2-pc-port auto-play: from a new game as far as the policy gets, with a log and screenshots.

    play.py EXE PAK WORKDIR [--waits N] [--card FILE] [--shot-every N]

Segments it knows: the opening (dialogs), the area select (first shown domain below the city),
dungeon floors (path to the exit), battles (first command). Everything else: Cross."""
import argparse
import json
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dw2bot import Game  # noqa: E402
import policy  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("pak")
    ap.add_argument("work")
    ap.add_argument("--waits", type=int, default=60000)
    ap.add_argument("--card")
    ap.add_argument("--continue-game", action="store_true", help="load slot 1 instead of a new game")
    ap.add_argument("--shot-every", type=int, default=1500)
    a = ap.parse_args()
    os.makedirs(os.path.join(a.work, "saves"), exist_ok=True)
    if a.card:
        shutil.copy(a.card, os.path.join(a.work, "saves", "card1.mcd"))
    g = Game(a.exe, a.pak, a.work)
    st = policy.Stats()
    modes = []
    try:
        g.run(600)
        g.tap("Start")
        g.run(120)
        if not a.continue_game:
            g.tap("Up")
            g.run(20)
        g.tap("Cross")
        st.note(g, "title: %s" % ("continue" if a.continue_game else "new game"))
        next_shot = g.wait + a.shot_every
        last = None
        while g.wait < a.waits:
            m = g.mode()
            if m != last:
                modes.append((g.wait, m))
                st.note(g, "scene 0x%x" % m)
                g.shot("scene_%06d_%x" % (g.wait, m))
                last = m
            if g.wait >= next_shot:
                g.shot("t%06d" % g.wait)
                next_shot = g.wait + a.shot_every
            if m == 0x32A:
                g.run(90)
                g.tap("Down")
                g.run(20)
                g.tap("Cross")
                g.run(60)
            elif m == 0x200:
                policy.dungeon_floor(g, st, max_waits=min(20000, a.waits - g.wait))
            elif policy.table(m) == 5:
                policy.battle(g, st)
            else:
                g.tap("Cross", 3, 17)
    except Exception as e:  # report what happened, keep the log
        st.note(g, "stopped: %r" % e)
    finally:
        rep = {"waits": g.wait, "vblanks": g.vb, "battles": st.battles, "floors": st.floors, "steps": st.steps,
               "scenes": [[w, hex(m)] for w, m in modes], "log": st.log}
        try:
            g.shot("final")
        except Exception:
            pass
        g.close()
        with open(os.path.join(a.work, "report.json"), "w") as f:
            json.dump(rep, f, indent=1)
        print(json.dumps({k: rep[k] for k in ("waits", "battles", "floors", "steps")}))


if __name__ == "__main__":
    main()
