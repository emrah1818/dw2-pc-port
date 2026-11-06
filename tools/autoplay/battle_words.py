#!/usr/bin/env python3
"""dw2-pc-port: pictures of the battle's situation words (word pictures, attack names, result).

Loads slot 1 of CARD, gives the party three Digimon set up for the test, warps to a domain and
walks into battles. Inside a battle the commands are chosen at random (attack, interrupt,
counter, assist, guard; skills; targets) and a picture is taken every EVERY waits, so the
pictures show the command words (Attack, Interrupt, Counter Attack, Assist, Guard), Missed,
Damage, the attack name banners, WIN / LOSE and the result screen (EXP, LEVEL UP, BITs).

    battle_words.py EXE PAK WORK CARD [--mode win|lose] [--domain N] [--battles N] [--every N]
                    [--seed N] [--extra "ARGS"]

--mode win: level 2 Digimon with full HP and weak attacks (long battles, a level up at the end);
--mode lose: level 1 Digimon with 1 HP (LOSE, GAME OVER).
Pictures: WORK/shots/bNN_WWWWW_display.png (and _hd.png with --hd); WORK/battle_words.log lists
the battles and the waits of each picture."""
import argparse
import os
import random
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dw2bot import Game  # noqa: E402
from save_test import load_slot1  # noqa: E402
import policy  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("pak")
    ap.add_argument("work")
    ap.add_argument("card")
    ap.add_argument("--mode", default="win", choices=["win", "lose"])
    ap.add_argument("--domain", type=int, default=1)
    ap.add_argument("--battles", type=int, default=2)
    ap.add_argument("--every", type=int, default=12)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--limit", type=int, default=9000, help="waits per battle")
    ap.add_argument("--hd", action="store_true")
    ap.add_argument("--extra", default="")
    a = ap.parse_args()
    rnd = random.Random(a.seed)
    shutil.rmtree(a.work, ignore_errors=True)
    os.makedirs(os.path.join(a.work, "saves"))
    shutil.copy(a.card, os.path.join(a.work, "saves", "card1.mcd"))
    g = Game(a.exe, a.pak, a.work, extra=("--cheats",) + tuple(a.extra.split()))
    log = open(os.path.join(a.work, "battle_words.log"), "w")
    count = [0]

    def shot(k, tag=""):
        name = "b%02d_%05d%s" % (k, g.wait, tag)
        g.shot(name)
        if a.hd:
            g.hdshot(name)
        count[0] += 1
        log.write("%s mode %x\n" % (name, g.mode()))

    def battle(g, st, limit=a.limit):
        st.battles += 1
        k = st.battles - 1
        start = g.wait
        log.write("battle %d at wait %d\n" % (k, start))
        last = -999
        def enemies_down():
            # Stg30_Battle.digis[3..5] (at 0x18, 0x5C each): max HP at +0x14, HP at +0x16
            alive = 0
            for i in range(3, 6):
                o = 0x18 + i * 0x5C
                if g.s16("Stg30_Battle", o + 0x14) > 0 and g.s16("Stg30_Battle", o + 0x16) > 0:  # max HP, HP
                    alive += 1
            return alive == 0

        while policy.table(g.mode()) == 5 and g.wait - start < limit:
            if g.wait - start > 600 and enemies_down():
                break  # won: the rest (WIN, the result) without random keys
            # a random command: a direction now and then, then Cross; Triangle rarely (back)
            r = rnd.random()
            if r < 0.35:
                g.tap(rnd.choice(["Down", "Right", "Up", "Left"]), 3, 6)
            elif r < 0.40:
                g.tap("Triangle", 3, 6)
            else:
                g.tap("Cross", 3, 6)
            if g.wait - last >= a.every:
                shot(k)
                last = g.wait
        # the result screen and what follows: pictures until the scene is back in the dungeon
        end = g.wait
        while g.wait - end < 3000 and policy.table(g.mode()) == 5:
            g.run(a.every)
            shot(k, "r")
            if (g.wait - end) % 240 < a.every:
                g.tap("Cross", 3, 6)
        return policy.table(g.mode()) != 5

    policy.battle = battle
    try:
        load_slot1(g)
        g.run(240)
        if a.mode == "win":
            spec = "adddigi=%d,2,99,999,999,40,700,300"
        else:
            spec = "adddigi=%d,1,99,1,1,1,1,1"
        for slot in range(3):  # the party: slots 0..2 replaced by the test Digimon
            g.poke("Save_GameState", 0xE4 + slot * 0x5C, bytes([0]))
        for sp in (3, 4, 5):
            g.devedit(spec % sp)
            g.run(10)
        for slot in range(3):
            if g.u8("Save_GameState", 0xE4 + slot * 0x5C) != 0:
                g.poke("Save_GameState", 0xE4 + slot * 0x5C, bytes([3 + slot]))
        for _ in range(30):
            g.devedit("warp=0x200,%d" % a.domain)
            g.run(60)
            if g.mode() == 0x200 or (g.mode() >> 8) != 3:
                break
        for _ in range(400):
            if g.mode() == 0x200 and g.dung() is not None:
                break
            g.tap("Cross", 3, 7)
        st = policy.Stats()
        start = g.wait
        while g.wait - start < 30000 and st.battles < a.battles and g.mode() >> 8 in (2, 5):
            policy.dungeon_floor(g, st, max_waits=3000)
        print("battles %d, pictures %d, mode %x" % (st.battles, count[0], g.mode()))
        for slot in range(3):
            o = 0xE4 + slot * 0x5C
            print("slot", slot, "level", g.u8("Save_GameState", o + 0x0D), "exp", g.s32("Save_GameState", o + 0x10))
    finally:
        log.close()
        g.close()


if __name__ == "__main__":
    main()
