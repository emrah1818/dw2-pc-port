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
    sl = next(i for i in range(0x24) if g.u8("Save_GameState", 0xE4 + i * 0x5C) != 0)  # first Digimon (in the party: 0..2)
    l0 = level(g, sl)
    edit(g, "digi=%d,level,12" % sl)
    check(level(g, sl) == 12, "Digimon slot %d level set to 12" % sl, level=level(g, sl), was=l0)
    edit(g, "digi=%d,level,250" % sl)
    check(level(g, sl) == 12, "level 250 (out of range) refused", level=level(g, sl))
    # the cheat list categories (Cheat_Digimon_World_2_SLUS: Digimon 1-6, important items, item
    # slots, Misc), through the same edit kinds the F1 tab uses
    D = 0xE4 + sl * 0x5C
    def u8(o): return g.u8("Save_GameState", o)
    def u16(o): return g.u16("Save_GameState", o)
    def s16(o): return g.s16("Save_GameState", o)
    edit(g, "digi=%d,species,0x04" % sl)
    check(u8(D + 1) == 0x04, "species (Digimon Modifier) set to 0x04", species=hex(u8(D + 1)))
    edit(g, "digi=%d,maxlevel,99" % sl)
    check(u8(D + 0x0F) == 99, "level limit set to 99", maxlevel=u8(D + 0x0F))
    edit(g, "digi=%d,maxlevel,0" % sl)
    check(u8(D + 0x0F) == 99, "level limit 0 (out of range) refused", maxlevel=u8(D + 0x0F))
    edit(g, "digi=%d,exp,9999999" % sl)
    check(g.s32("Save_GameState", D + 0x10) == 9999999, "EXP set to 9,999,999 (Max Exp)", exp=g.s32("Save_GameState", D + 0x10))
    for f, o in (("maxhp", 0x14), ("hp", 0x16), ("maxmp", 0x18), ("mp", 0x1A), ("attack", 0x1C), ("defense", 0x1E), ("speed", 0x20)):
        edit(g, "digi=%d,%s,999" % (sl, f))
        check(s16(D + o) == 999, "%s set to 999 (cheat list Max)" % f, value=s16(D + o))
    edit(g, "digi=%d,attack,1000" % sl)
    check(s16(D + 0x1C) == 999, "attack 1000 (over the game's 999) refused", value=s16(D + 0x1C))
    edit(g, "skill=%d,11,0x1F" % sl)
    check(u8(D + 0x22 + 11) == 0x1F, "move slot 12 set to skill 0x1F", skill=hex(u8(D + 0x22 + 11)))
    edit(g, "storage=0xED,1")
    check(u16(0xDD4 + 0xED * 2) == 1, "important item Tamer-Lic. (0xED, 0x8005F5CE) owned", count=u16(0xDD4 + 0xED * 2))
    edit(g, "storage=0x108,1")
    check(u16(0xDD4 + 0x108 * 2) == 1, "important item 0x108 (0x8005F604) owned", count=u16(0xDD4 + 0x108 * 2))
    edit(g, "storage=0x109,1")
    check(u16(0xDD4 + 0x109 * 2) == 0, "item 0x109 (past the item list) refused", count=u16(0xDD4 + 0x109 * 2))
    edit(g, "bag=0,0x01")
    check(u16(0x66) == 1, "bag slot 1 set to item 0x01 (Item Slot 1, 0x8005E686)", item=hex(u16(0x66)))
    edit(g, "bits=99999999")
    check(bits(g) == 99999999, "Bits set to 99,999,999 (Max Bits)", bits=bits(g))
    edit(g, "bits=100000000")
    check(bits(g) == 99999999, "Bits over 99,999,999 refused", bits=bits(g))
    for f in ("maxhp", "hp", "maxep", "ep"):
        edit(g, "beetle=%s,9999" % f)
    check(all(s16(o) == 9999 for o in (0x24, 0x26, 0x28, 0x2A)), "Digi-Beetle HP / Max HP / EP / Max EP set to 9999",
          hp=s16(0x24), maxhp=s16(0x26), ep=s16(0x28), maxep=s16(0x2A))
    # held values: written again at every frame
    g._call("cheathold 0x1F")
    edit(g, "bits=5")
    g.run(4)
    check(bits(g) == 99999999, "Infinite Bits: Bits back at 99,999,999 after a change to 5", bits=bits(g))
    edit(g, "digi=%d,hp,1" % sl)
    edit(g, "digi=%d,mp,0" % sl)
    g.run(4)
    check(s16(D + 0x16) == s16(D + 0x14) and s16(D + 0x1A) == s16(D + 0x18),
          "Infinite HP / MP: party slot refilled", hp=s16(D + 0x16), mp=s16(D + 0x1A))
    edit(g, "beetle=hp,1")
    g.run(4)
    check(s16(0x24) == s16(0x26), "Infinite HP (Digi-Beetle): refilled", hp=s16(0x24), maxhp=s16(0x26))
    g._call("cheathold 0")
    edit(g, "bits=5")
    check(bits(g) == 5, "held values off: Bits stay at 5", bits=bits(g))
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
