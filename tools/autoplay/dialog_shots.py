#!/usr/bin/env python3
"""dw2-pc-port: screenshots of real conversations, for checking translated dialogs in the game.

Loads slot 1 of CARD, visits the city destinations, talks to every walker of each walking scene
and takes a picture of every page of the talk (Cross between the pages). Pictures go to
WORK/shots/<scene>_<npc>_<page>_display.png, a list to WORK/dialog_shots.log.

    dialog_shots.py EXE PAK WORK --card CARD [--lang de] [--scenes N] [--pages N] [--hd] [--extra ARGS]
"""
import argparse
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dw2bot import Game, GameError  # noqa: E402
import navigate  # noqa: E402
import policy  # noqa: E402
import story  # noqa: E402
import town  # noqa: E402


HD = False


def talk_with_shots(g, cell, tag, pages, log):
    """talk_to, but with a picture of each page; returns the number of pictures"""
    for attempt in range(4):
        t = g.town()
        if t is None:
            return 0
        cands = [c for c in town.npcs(t) if abs(c[0] - cell[0]) + abs(c[1] - cell[1]) <= 2]
        if not cands:
            return 0
        cell = min(cands, key=lambda c: abs(c[0] - cell[0]) + abs(c[1] - cell[1]))
        adj = {(cell[0] + dx, cell[1] + dy): (-dx, -dy) for dx, dy in town.STEP}
        adj = {c: d for c, d in adj.items() if 0 <= c[0] < 24 and 0 <= c[1] < 24 and t["grid"][c[0]][c[1]] in ".P"}
        if not adj or not town.walk_to(g, adj.keys()):
            continue
        t = g.town()
        if t is None or (t["x"], t["y"]) not in adj:
            continue
        g.run(36)
        g.tap(town.STEP[adj[(t["x"], t["y"])]], 2, 8)
        g.tap("Cross", 3, 24)
        n = 0
        while town.talk_active(g) and n < pages:
            g.run(90)  # the text types out
            g.shot("%s_%02d" % (tag, n))
            if HD:
                g.hdshot("%s_%02d" % (tag, n))
            log.write("%s_%02d mode 0x%x\n" % (tag, n, g.mode()))
            n += 1
            g.tap("Cross", 3, 14)
        town.clear_talk(g)
        return n
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("pak")
    ap.add_argument("work")
    ap.add_argument("--card", required=True)
    ap.add_argument("--lang", default="en")
    ap.add_argument("--scenes", type=int, default=3)
    ap.add_argument("--pages", type=int, default=6)
    ap.add_argument("--hd", action="store_true", help="also the HD picture (with --extra: wide, scale)")
    ap.add_argument("--extra", default="", help="more program arguments, e.g. \"--settings-set wide=1\"")
    a = ap.parse_args()
    os.makedirs(os.path.join(a.work, "saves"), exist_ok=True)
    shutil.copy(a.card, os.path.join(a.work, "saves", "card1.mcd"))
    from save_test import load_slot1
    global HD
    HD = a.hd
    g = Game(a.exe, a.pak, a.work, extra=("--lang", a.lang) + tuple(a.extra.split()))
    st = policy.Stats()
    log = open(os.path.join(a.work, "dialog_shots.log"), "w")
    total = 0
    try:
        load_slot1(g)
        story.settle(g, st)
        n = g.nav()
        dests = len(n[1]) if n else 0
        for i in range(min(dests, a.scenes)):
            if not story.to_city_map(g, st):
                break
            if not navigate.goto_index(g, i):
                continue
            g.tap("Cross", 3, 30)
            if story.settle(g, st) != "town":
                continue
            t = g.town()
            for k, c in enumerate(town.npcs(t)):
                total += talk_with_shots(g, c, "s%d_n%d" % (i, k), a.pages, log)
    except GameError as e:
        log.write("stopped: %s\n" % e)
    finally:
        g.close()
        log.write("%d pictures\n" % total)
        log.close()
    print("%d pictures in %s" % (total, os.path.join(a.work, "shots")))


if __name__ == "__main__":
    main()
