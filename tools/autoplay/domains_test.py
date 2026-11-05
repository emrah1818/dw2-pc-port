#!/usr/bin/env python3
"""dw2-pc-port domain smoke tests (a single test per domain, NOT story play): load the saved card
(city after Boot Domain), make the party strong with dev state edits (test set-up), warp into
domain N, then walk its floors to the exits for a while; battles on the way are fought with the
first command.

    domains_test.py EXE PAK WORKDIR CARD [--domains 0-32] [--waits 9000] [--hd]

Per domain: did the domain load, which floors were reached, battles started / ended, a crash or
a hang. Writes WORKDIR/domains.json."""
import argparse
import json
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dw2bot import Game, GameError  # noqa: E402
import policy  # noqa: E402
from save_test import load_slot1  # noqa: E402

BOOST = ["maxhp,999", "hp,999", "maxmp,999", "mp,999", "attack,700", "defense,700", "speed,700"]


def one(exe, pak, work, card, n, waits, extra):
    os.makedirs(os.path.join(work, "saves"), exist_ok=True)
    shutil.copy(card, os.path.join(work, "saves", "card1.mcd"))
    rec = {"domain": n, "loaded": False, "floors": [], "battles": 0, "battles_ended": 0, "error": None}
    g = None
    try:
        g = Game(exe, pak, work, extra=extra)
        m = load_slot1(g)
        if (m >> 8) != 3:
            rec["error"] = "load ended in mode 0x%x" % m
            return rec
        # test set-up: three Digimon into the empty roster (dev edit adddigi), marked as the
        # battle party (state 3, as the Digimon menu does), strong stats
        g.run(240)
        for sp in (3, 4, 5):
            g.devedit("adddigi=%d,40,99,999,999,700,700,700" % sp)
            g.run(10)
        g.run(20)
        for slot in range(3):
            if g.u8("Save_GameState", 0xE4 + slot * 0x5C) != 0:
                g.poke("Save_GameState", 0xE4 + slot * 0x5C, bytes([3]))
        g.run(30)
        rec["party"] = [g.u8("Save_GameState", 0xE4 + i * 0x5C + 1) for i in range(3)
                        if g.u8("Save_GameState", 0xE4 + i * 0x5C) >= 3]
        for _ in range(30):
            g.devedit("warp=0x200,%d" % n)
            g.run(60)
            if g.mode() == 0x200 or (g.mode() >> 8) != 3:
                break
        for _ in range(400):
            if g.mode() == 0x200 and g.dung() is not None:
                break
            g.tap("Cross", 3, 7)
        d = g.dung()
        if d is None:
            rec["error"] = "no dungeon after the warp (mode 0x%x)" % g.mode()
            return rec
        rec["loaded"] = True
        st = policy.Stats()
        start = g.wait
        g.shot("d%02d_first" % n)
        if "--hd" in extra or any(a.startswith("--scale") for a in extra):
            g.hdshot("d%02d_first" % n)
        while g.wait - start < waits:
            d = g.dung()
            if d is not None and (not rec["floors"] or rec["floors"][-1] != d["floor"]):
                rec["floors"].append(d["floor"])
            if (g.mode() >> 8) == 3:
                rec["left_to_city"] = True
                break
            policy.dungeon_floor(g, st, max_waits=min(4000, waits - (g.wait - start)))
        rec["battles"] = st.battles
        rec["battles_ended"] = st.battles  # battle() returns only once the battle scene is over
        rec["steps"] = st.steps
        rec["alive"] = g.proc.poll() is None
        g.shot("d%02d_last" % n)
    except GameError as e:
        rec["error"] = str(e)
    finally:
        if g is not None:
            g.close()
    return rec


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("pak")
    ap.add_argument("work")
    ap.add_argument("card")
    ap.add_argument("--domains", default="0-32")
    ap.add_argument("--waits", type=int, default=9000)
    ap.add_argument("--extra", default="")
    a = ap.parse_args()
    lo, hi = (int(v) for v in a.domains.split("-")) if "-" in a.domains else (int(a.domains), int(a.domains))
    out = []
    for n in range(lo, hi + 1):
        rec = one(a.exe, a.pak, os.path.join(a.work, "d%02d" % n), a.card, n, a.waits, a.extra.split())
        print(json.dumps(rec), flush=True)
        out.append(rec)
        with open(os.path.join(a.work, "domains.json"), "w") as f:
            json.dump(out, f, indent=1)


if __name__ == "__main__":
    main()
