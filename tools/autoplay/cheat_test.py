#!/usr/bin/env python3
"""dw2-pc-port cheat menu backend test (release exe): the F1 Cheats tab sends its changes through
the same edit queue the bot's devedit command uses; this checks that path and the safety rules.

    cheat_test.py EXE PAK CARD WORK
1. without --cheats: an edit is refused, the save block stays as it is
2. with --cheats: the cards are backed up first (identical copies), Bits and a Digimon's level
   change as asked, a value out of range is refused, undo restores the save block
Writes WORK/cheat_test.json; exit 0 when all checks passed."""
import glob
import json
import os
import shutil
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dw2bot import Game, GameError  # noqa: E402
from save_test import load_slot1  # noqa: E402

results = []


def check(ok, what, **info):
    results.append({"ok": bool(ok), "check": what, **info})
    print("%s %s %s" % ("PASS" if ok else "FAIL", what, info if info else ""), flush=True)


def bits(g):
    return g.s32("Save_GameState", 0x08)


def level(g, slot):
    return g.u8("Save_GameState", 0xE4 + slot * 0x5C + 0x0D)


def run(exe, pak, card, work, extra):
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(os.path.join(work, "saves"))
    shutil.copy(card, os.path.join(work, "saves", "card1.mcd"))
    g = Game(exe, pak, work, extra=extra)
    load_slot1(g)
    g.run(120)
    return g


def edit(g, spec):
    try:
        g.devedit(spec)
    except GameError as e:
        return str(e)
    g.run(6)
    return "ok"


def main():
    exe, pak, card, work = sys.argv[1:5]
    os.makedirs(work, exist_ok=True)
    # 1. cheats off
    g = run(exe, pak, card, os.path.join(work, "off"), [])
    b0 = bits(g)
    r = edit(g, "bits=55555")
    check(bits(g) == b0, "cheats off: edit refused, Bits unchanged", bits=bits(g), reply=r)
    g.close()
    # 2. cheats on
    w = os.path.join(work, "on")
    g = run(exe, pak, card, w, ["--cheats"])
    backups = glob.glob(os.path.join(w, "saves", "cheat_backup_*", "card1.mcd"))
    check(len(backups) == 1 and open(backups[0], "rb").read() == open(card, "rb").read(),
          "cheats on: card backed up first (identical copy)", backups=backups)
    before = g.peek("Save_GameState", 0, 0x1058)
    b0 = bits(g)
    edit(g, "bits=55555")
    check(bits(g) == 55555, "Bits set to 55555", bits=bits(g), was=b0)
    l0 = level(g, 0)
    edit(g, "digi=0,level,12")
    check(level(g, 0) == 12, "Digimon slot 0 level set to 12", level=level(g, 0), was=l0)
    edit(g, "digi=0,level,250")
    check(level(g, 0) == 12, "level 250 (out of range) refused", level=level(g, 0))
    g._call("cheatundo")
    g.run(10)
    after = g.peek("Save_GameState", 0, 0x1058)
    diff = [i for i in range(0x1058) if before[i] != after[i] and not 4 <= i < 8]
    check(not diff, "undo restores the save block (play time aside)", differing=len(diff), bits=bits(g), level=level(g, 0))
    g.close()
    card_now = open(os.path.join(w, "saves", "card1.mcd"), "rb").read()
    check(card_now == open(card, "rb").read(), "memory card unchanged (cheats do not save by themselves)")
    with open(os.path.join(work, "cheat_test.json"), "w") as f:
        json.dump(results, f, indent=1)
    bad = [x for x in results if not x["ok"]]
    print("%d checks, %d failed" % (len(results), len(bad)))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
