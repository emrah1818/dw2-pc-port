#!/usr/bin/env python3
"""Shared segment: new game -> intro -> Boot Domain -> back in Digital City, at the city map
(game mode 0x32D). Returns the Game (lock-stepped there) and the policy stats."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dw2bot import Game  # noqa: E402
import policy  # noqa: E402


def to_city(exe, pak, work, extra=(), limit=60000, env=None):
    g = Game(exe, pak, work, extra=extra, env=env)
    st = policy.Stats()
    g.run(600)
    g.tap("Start")
    g.run(120)
    g.tap("Up")
    g.run(20)
    g.tap("Cross")
    been_in_domain = False
    while g.wait < limit:
        m = g.mode()
        if m == 0x32D and been_in_domain:
            g.run(120)
            return g, st
        if m == 0x32A:
            g.run(90)
            g.tap("Down")
            g.run(20)
            g.tap("Cross")
            g.run(60)
        elif m == 0x200:
            been_in_domain = True
            policy.dungeon_floor(g, st, max_waits=20000)
        elif policy.table(m) == 5:
            policy.battle(g, st)
        else:
            g.tap("Cross", 3, 17)
    raise RuntimeError("city map not reached by wait %d" % g.wait)
