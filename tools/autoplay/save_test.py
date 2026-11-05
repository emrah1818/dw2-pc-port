#!/usr/bin/env python3
"""dw2-pc-port save / load test through the game's own menus (no poking):

1. new game -> Boot Domain -> city map (to_city.py)
2. read Save_GameState (the save block, 0x1058 bytes), save to memory card slot 1 by the menu
   (Circle, Save, slot 1)
3. a new program run: title, Continue, slot 1: the loaded Save_GameState must equal the saved one
   (play time aside), the game must reach the city
4. damaged cards: random bytes, cut short, zero length, flipped bytes in the save block: the
   program must not crash and must reach the title / slot screen again

    save_test.py EXE PAK WORKDIR
Writes WORKDIR/save_test.json; exit 0 when every check passed."""
import json
import os
import random
import shutil
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dw2bot import Game, GameError  # noqa: E402
from to_city import to_city  # noqa: E402

GS = 0x1058
results = []


def check(ok, what, **info):
    results.append({"ok": bool(ok), "check": what, **info})
    print("%s %s %s" % ("PASS" if ok else "FAIL", what, info if info else ""), flush=True)
    return ok


def summary(b):
    bits, = struct.unpack_from("<i", b, 0x08)
    name = b[0x14:0x24].split(b"\xff")[0].hex()
    party = [(b[0xE4 + i * 0x5C + 1], b[0xE4 + i * 0x5C + 0xD]) for i in range(0x24) if b[0xE4 + i * 0x5C] != 0]
    return {"bits": bits, "name": name, "rank": b[0x12], "roster": party,
            "progress": struct.unpack_from("<i", b, 0x1004 + 0x40)[0]}


def save_menu(g):
    g.tap("Circle", 3, 40)
    g.tap("Right", 3, 15)
    g.tap("Down", 3, 15)
    g.tap("Down", 3, 15)
    g.shot("save_menu")
    g.tap("Cross", 3, 60)
    for _ in range(60):
        if g.mode() == 0x601:
            break
        g.run(10)
    g.run(120)
    g.shot("card_scene")
    # slot 1, confirm; then Cross through the messages until the scene ends
    for _ in range(40):
        g.tap("Cross", 3, 30)
    g.shot("card_saved")
    # back out of the card scene: Triangle is "back" in the card menus (stag1100 cardmenu.c)
    for _ in range(40):
        if g.mode() != 0x601:
            break
        g.tap("Triangle", 3, 30)
    g.run(120)
    return g.mode()


def load_slot1(g):
    g.run(600)
    g.tap("Start")
    g.run(120)
    g.tap("Cross")  # Continue (the default)
    for _ in range(60):
        if g.mode() == 0x602:
            break
        g.run(10)
    g.run(120)
    g.shot("load_scene")
    start = g.wait
    while g.mode() == 0x602 and g.wait - start < 6000:
        g.tap("Cross", 3, 30)
    g.run(300)
    return g.mode()


def main():
    exe, pak, work = sys.argv[1:4]
    os.makedirs(work, exist_ok=True)
    g, _ = to_city(exe, pak, os.path.join(work, "a_play"))
    before = g.peek("Save_GameState", 0, GS)
    check(len(before) == GS, "save block read", **summary(before))
    m = save_menu(g)
    check(m != 0x601, "save scene finished", mode=hex(m))
    g.shot("after_save")
    g.close()
    card = os.path.join(work, "a_play", "saves", "card1.mcd")
    ok = check(os.path.exists(card) and os.path.getsize(card) == 131072, "card1.mcd written (128 KB)",
               size=os.path.getsize(card) if os.path.exists(card) else 0)
    if ok:
        shutil.copy(card, os.path.join(work, "saved_card1.mcd"))

    # load in a new run
    lw = os.path.join(work, "b_load")
    os.makedirs(os.path.join(lw, "saves"), exist_ok=True)
    if ok:
        shutil.copy(card, os.path.join(lw, "saves", "card1.mcd"))
    g = Game(exe, pak, lw)
    m = load_slot1(g)
    after = g.peek("Save_GameState", 0, GS)
    g.shot("after_load")
    check((m >> 8) == 3, "loaded game runs in the city", mode=hex(m))
    diff = [i for i in range(GS) if before[i] != after[i] and not (4 <= i < 8)]
    check(not diff, "loaded save block = saved one (play time aside)", differing_bytes=len(diff),
          first=diff[:16], **summary(after))
    g.close()

    # damaged cards
    rnd = random.Random(1)
    good = open(os.path.join(work, "saved_card1.mcd"), "rb").read() if ok else bytes(131072)
    cases = {
        "random_bytes": bytes(rnd.randrange(256) for _ in range(131072)),
        "cut_short": good[:5000],
        "zero_length": b"",
        "flipped_save_block": bytes(b ^ 0x5A if 0x2000 <= i < 0x3000 else b for i, b in enumerate(good)),
    }
    for name, data in cases.items():
        cw = os.path.join(work, "c_" + name)
        os.makedirs(os.path.join(cw, "saves"), exist_ok=True)
        with open(os.path.join(cw, "saves", "card1.mcd"), "wb") as f:
            f.write(data)
        try:
            g = Game(exe, pak, cw)
            m = load_slot1(g)
            g.run(600)
            m2 = g.mode()
            g.shot("damaged")
            alive = g.proc.poll() is None
            g.close()
            check(alive, "damaged card (%s): no crash" % name, mode_after_load=hex(m), mode_later=hex(m2))
        except GameError as e:
            check(False, "damaged card (%s): no crash" % name, error=str(e))
    with open(os.path.join(work, "save_test.json"), "w") as f:
        json.dump(results, f, indent=1)
    bad = [r for r in results if not r["ok"]]
    print("%d checks, %d failed" % (len(results), len(bad)))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
