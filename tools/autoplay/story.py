#!/usr/bin/env python3
"""dw2-pc-port story driver: plays the city part of the story through pad input.

The game is driven like a player would: the city map (nav), walking scenes (town), talks,
domain maps, dungeons and battles (policy.py). The bot reads game state to decide, it never
writes it. Progress is measured by the save block's event flags, the roster and the shown map
destinations.

    story.py EXE PAK WORK --card CARD [--waits N]     (continue slot 1 of CARD)
"""
import argparse
import json
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dw2bot import Game, GameError  # noqa: E402
import navigate  # noqa: E402
import policy  # noqa: E402
import town  # noqa: E402

CITY_MAP = 0x32D
LOG = None


def roster(g):
    b = g.peek("Save_GameState", 0xE4, 0x5C * 0x24)
    return [(b[i * 0x5C + 1], b[i * 0x5C + 0xD]) for i in range(0x24) if b[i * 0x5C]]


def flags(g):
    return g.peek("Save_GameState", 0x1004, 0x44)


def flag_diff(a, b):
    out = []
    for i in range(0x40):
        x = a[i] ^ b[i]
        for k in range(8):
            if x >> k & 1:
                out.append(("+" if b[i] >> k & 1 else "-") + str(i * 8 + k))
    pa, pb = int.from_bytes(a[0x40:], "little"), int.from_bytes(b[0x40:], "little")
    if pa != pb:
        out.append("progress %d->%d" % (pa, pb))
    return out


class Log:
    def __init__(self, path):
        self.f = open(path, "a")

    def __call__(self, g, text):
        line = "wait %d mode 0x%x: %s" % (g.wait, g.mode(), text)
        print(line, flush=True)
        self.f.write(line + "\n")
        self.f.flush()


def settle(g, st, limit=4000):
    """Play through whatever runs (talks, cut-scenes, battles, dungeons) until a walking scene or
    a map pick is waiting for the player. Returns 'town', 'nav' or None."""
    start = g.wait
    while g.wait - start < limit:
        m = g.mode()
        if policy.table(m) == 5:
            policy.battle(g, st)
            continue
        if m == 0x200:
            return "dungeon"
        if g.nav() is not None:
            return "nav"
        if (m >> 8) == 3 and g.town() is not None:
            if town.talk_active(g):
                town.clear_talk(g)
                continue
            g.run(20)
            if g.town() is not None and not town.talk_active(g):
                return "town"
            continue
        if (m >> 8) == 3 and (m & 0xFF) >= 0x2F:
            g.tap("Triangle", 3, 20)  # a lab / shop menu the bot did not ask for: back out
            continue
        g.tap("Cross", 3, 17)
    return None


def explore_scene(g, st, log, seen, depth=0):
    """In a walking scene: talk to every walker, then visit the sub-scenes behind the exits
    (not the way back). Returns to this scene's caller through the exit it came from."""
    m = g.mode()
    t = g.town()
    if t is None:
        return
    log(g, "scene 0x%x: %d walkers, exits %s" % (m, len(town.npcs(t)), [hex(e[2]) for e in t["exits"]]))
    g.shot("scene_%x_%d" % (m, g.wait))
    talked = set()
    for _ in range(12):
        t = g.town()
        if t is None or g.mode() != m:
            break
        left = [c for c in town.npcs(t) if c not in talked]
        if not left:
            break
        c = min(left, key=lambda c: abs(c[0] - t["x"]) + abs(c[1] - t["y"]))
        f0, r0 = flags(g), roster(g)
        n = town.talk_to(g, c)
        talked.add(c)
        if g.mode() != m:
            log(g, "talk at %s led to scene 0x%x" % (c, g.mode()))
            return
        d = flag_diff(f0, flags(g))
        r1 = roster(g)
        if n or d:
            log(g, "talk %s: %d presses, flags %s%s" % (c, n, d or "-", ", roster %s" % r1 if r1 != r0 else ""))
    seen.add(m)
    if depth >= 3:
        return
    t = g.town()
    if t is None:
        return
    for sub in [e[2] for e in t["exits"]]:
        if sub == CITY_MAP or sub in seen or g.mode() != m:
            continue
        seen.add(sub)
        if not town.leave(g, sub):
            continue
        if settle(g, st) != "town" or g.mode() != sub:
            return
        explore_scene(g, st, log, seen, depth + 1)
        if g.mode() == sub and g.town() is not None and any(e[2] == m for e in g.town()["exits"]):
            town.leave(g, m)
            settle(g, st)
        if g.mode() != m:
            return


def to_city_map(g, st):
    """From a walking scene back to the city map."""
    for _ in range(8):
        s = settle(g, st)
        if s == "nav" and g.mode() == CITY_MAP:
            return True
        if s == "nav":
            domain_map(g, st, LOG)
            continue
        if s == "dungeon":
            play_domain(g, st, LOG)
            continue
        if s == "town":
            t = g.town()
            if any(e[2] == CITY_MAP for e in t["exits"]):
                town.leave(g, CITY_MAP)
            else:
                town.leave(g, None)
        elif s is None:
            return False
    return g.mode() == CITY_MAP


def city_round(g, st, log, seen):
    """Visit every city destination once."""
    n = g.nav()
    if n is None:
        return
    for i in range(len(n[1])):
        if not to_city_map(g, st):
            log(g, "lost the way back to the city map")
            return
        recs = g.nav()[1]
        if i >= len(recs):
            break
        if not navigate.goto_index(g, i):
            log(g, "cannot pick city destination %d" % i)
            continue
        g.tap("Cross", 3, 30)
        s = settle(g, st)
        if s == "town":
            explore_scene(g, st, log, seen)
        elif s == "nav" and g.mode() != CITY_MAP:
            domain_map(g, st, log)
        elif s == "dungeon":
            play_domain(g, st, log)


DONE_DOMAINS = set()


def play_domain(g, st, log, limit=400000):
    """In a dungeon: floors to the exit, battles, until the game leaves the domain."""
    start = g.wait
    log(g, "domain entered")
    while g.wait - start < limit:
        m = g.mode()
        if m == 0x200:
            policy.dungeon_floor(g, st, max_waits=30000)
        elif policy.table(m) == 5:
            policy.battle(g, st)
        elif (m >> 8) == 3:
            log(g, "domain left (%d battles so far, party %s)" % (st.battles, roster(g)))
            return True
        else:
            g.tap("Cross", 3, 17)
    log(g, "domain time limit")
    return False


def domain_map(g, st, log):
    """The domain map: the first shown domain not played yet (else the last one)."""
    cur, recs = g.nav()
    log(g, "domain map 0x%x: %s" % (g.mode(), recs))
    g.shot("domainmap_%d" % g.wait)
    doms = [i for i, r in enumerate(recs) if r[1] == 0x200]
    order = sorted([i for i in doms if (r := recs[i]) and (r[1], r[2]) not in DONE_DOMAINS],
                   key=lambda i: -recs[i][0])
    if not order:
        order = [i for i, r in enumerate(recs) if r[1] != 0x200] or [0]  # back to the city
    i = order[0]
    DONE_DOMAINS.add((recs[i][1], recs[i][2]))
    if not navigate.goto_index(g, i):
        log(g, "cannot pick domain %d" % i)
        return
    g.tap("Cross", 3, 30)
    log(g, "picked domain %s" % (recs[i],))
    if settle(g, st, limit=8000) == "dungeon":
        play_domain(g, st, log)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("pak")
    ap.add_argument("work")
    ap.add_argument("--card", required=True)
    ap.add_argument("--rounds", type=int, default=2)
    a = ap.parse_args()
    os.makedirs(os.path.join(a.work, "saves"), exist_ok=True)
    shutil.copy(a.card, os.path.join(a.work, "saves", "card1.mcd"))
    from save_test import load_slot1
    g = Game(a.exe, a.pak, a.work)
    st = policy.Stats()
    log = Log(os.path.join(a.work, "story.log"))
    global LOG
    LOG = log
    try:
        load_slot1(g)
        f_start = flags(g)
        for r in range(a.rounds):
            seen = set()
            city_round(g, st, log, seen)
            log(g, "round %d done: flags since start %s, roster %s" % (r, flag_diff(f_start, flags(g)), roster(g)))
            if to_city_map(g, st):
                from save_test import save_menu
                save_menu(g)
                settle(g, st)
                shutil.copy(os.path.join(a.work, "saves", "card1.mcd"), os.path.join(a.work, "card_round%d.mcd" % r))
                log(g, "saved to card (card_round%d.mcd)" % r)
    except GameError as e:
        log(g, "stopped: %s" % e)
    finally:
        g.close()


if __name__ == "__main__":
    main()
