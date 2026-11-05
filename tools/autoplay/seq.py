#!/usr/bin/env python3
"""Run a step list against a fresh game and save screenshots: exploration and replay tests.

    seq.py EXE PAK WORKDIR STEP...
      wN            run N waits
      t:BUTTONS     tap (hold 3, release 6)
      h:BUTTONS:N   hold N waits
      s:TAG         screenshot
      m             print the game mode
      u:WAITS:MODE  run until game mode == MODE (hex) or WAITS passed
      card:SRC      (first step only) copy SRC as card1.mcd before the start
"""
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dw2bot import Game  # noqa: E402


def main():
    exe, pak, work = sys.argv[1:4]
    steps = sys.argv[4:]
    os.makedirs(os.path.join(work, "saves"), exist_ok=True)
    if steps and steps[0].startswith("card:"):
        shutil.copy(steps.pop(0)[5:], os.path.join(work, "saves", "card1.mcd"))
    g = Game(exe, pak, work, extra=os.environ.get("DW2_ARGS", "").split())
    try:
        for st in steps:
            if st[0] == "w":
                g.run(int(st[1:]))
            elif st.startswith("t:"):
                g.tap(st[2:])
            elif st.startswith("h:"):
                _, b, n = st.split(":")
                g.run(int(n), b)
            elif st.startswith("s:"):
                print("shot", g.shot(st[2:]), "wait", g.wait, "mode 0x%x" % g.mode())
            elif st.startswith("hd:"):
                print("hdshot", *g.hdshot(st[3:]), "wait", g.wait)
            elif st == "m":
                print("wait", g.wait, "mode 0x%x" % g.mode())
            elif st.startswith("u:"):
                _, n, m = st.split(":")
                n, m = int(n), int(m, 16)
                for _ in range(n // 10):
                    if g.mode() == m:
                        break
                    g.run(10)
                print("until 0x%x: wait %d mode 0x%x" % (m, g.wait, g.mode()))
    finally:
        g.close()


if __name__ == "__main__":
    main()
