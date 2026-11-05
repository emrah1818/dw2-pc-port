#!/usr/bin/env python3
"""dw2-pc-port auto-play policy: generic behaviour per scene, used by play.py.

- dialogs and events: Cross
- area select (city 0x32A): a destination by name in the route
- dungeon (0x200): breadth-first path on the floor grid to the exit (entity kind 2 / 3), the
  D-pad held until the tile changes; battles that start on the way are fought by battle()
- battle (0x5xx): Cross through the command menus (first command, first target)

Everything goes through the normal pad input; the game is never poked."""
import collections
import time

DIRS = {(0, -1): "Up", (0, 1): "Down", (-1, 0): "Left", (1, 0): "Right",
        (1, -1): "Up+Right", (-1, -1): "Up+Left", (1, 1): "Down+Right", (-1, 1): "Down+Left"}


def table(mode):
    return mode >> 8


class Stats:
    def __init__(self):
        self.battles = 0
        self.floors = 0
        self.steps = 0
        self.log = []

    def note(self, g, text):
        line = "wait %d mode 0x%x: %s" % (g.wait, g.mode(), text)
        self.log.append(line)
        print(line, flush=True)


def walkable(d, x, y):
    """A floor cell; cells marked occupied by an entity (enemy party, exit, the player) count as
    floor too: walking into an enemy starts the battle the bot wants anyway."""
    if not (0 <= x < d["cols"] and 0 <= y < d["rows"]):
        return False
    if d["grid"][y][x] == ".":
        return True
    occ = d.get("_occ")
    if occ is None:
        occ = d["_occ"] = {(e[1], e[2]) for e in d["ents"]}
    return (x, y) in occ


def path(d, start, goals, blocked):
    q = collections.deque([start])
    prev = {start: None}
    while q:
        p = q.popleft()
        if p in goals:
            out = []
            while prev[p] is not None:
                out.append(p)
                p = prev[p]
            return out[::-1]
        for (dx, dy) in DIRS:
            n = (p[0] + dx, p[1] + dy)
            if n in prev or (p, n) in blocked or not walkable(d, *n):
                continue
            if dx and dy and not (walkable(d, p[0] + dx, p[1]) and walkable(d, p[0], p[1] + dy)):
                continue
            prev[n] = p
            q.append(n)
    return None


def battle(g, st, limit=12000):
    """Cross until the battle scene ends (mode leaves table 5)."""
    st.battles += 1
    st.note(g, "battle %d starts" % st.battles)
    start = g.wait
    while table(g.mode()) == 5 and g.wait - start < limit:
        g.tap("Cross", 3, 9)
    st.note(g, "battle %d over after %d waits" % (st.battles, g.wait - start))
    return table(g.mode()) != 5


def clear_text(g):
    """One Cross: closes an open message (in a dungeon a Cross with no message searches the tile
    and opens "Nothing here.", which the next stuck step closes again)."""
    g.tap("Cross", 3, 30)


def dungeon_floor(g, st, max_waits=20000):
    """Walk the current floor to an exit; returns when the floor or the scene changes."""
    start = g.wait
    d = g.dung()
    if d is None:
        return False
    floor0 = (d["floor"], d["cols"], d["rows"])
    blocked = set()
    stuck = 0
    while g.wait - start < max_waits:
        m = g.mode()
        if table(m) == 5:
            battle(g, st)
            continue
        d = g.dung()
        if d is None or m != 0x200:
            g.tap("Cross", 3, 17)
            if g.mode() != 0x200 and table(g.mode()) != 5:
                return True
            continue
        if (d["floor"], d["cols"], d["rows"]) != floor0:
            st.floors += 1
            st.note(g, "new floor %d" % d["floor"])
            return True
        goals = {(e[1], e[2]) for e in d["ents"] if e[0] in (2, 3)}
        if not goals:
            clear_text(g)
            continue
        here = (d["x"], d["y"])
        if here in goals:
            g.run(30)  # on the exit: the floor change plays, no input
            continue
        p = path(d, here, goals, blocked)
        if not p:
            st.note(g, "no path from %s to %s" % (here, goals))
            key = (d["floor"], here)
            if key not in getattr(st, "dumped", set()):
                st.dumped = getattr(st, "dumped", set()) | {key}
                import json
                with open(g.workdir + "/nopath_f%d_%d_%d.json" % (d["floor"], here[0], here[1]), "w") as f:
                    json.dump({k: v for k, v in d.items() if not k.startswith("_")}, f)
                g.shot("nopath_f%d_%d_%d" % (d["floor"], here[0], here[1]))
            blocked.clear()
            clear_text(g)
            continue
        nxt = p[0]
        btn = DIRS[(nxt[0] - here[0], nxt[1] - here[1])]
        moved = False
        for _ in range(3):
            g.run(16, btn)
            g.run(4)
            d2 = g.dung()
            if d2 is None or (d2["x"], d2["y"]) != here or table(g.mode()) == 5:
                moved = True
                break
        if moved:
            st.steps += 1
            stuck = 0
        else:
            stuck += 1
            clear_text(g)  # a message or event on the way
            if stuck >= 4:
                blocked.add((here, nxt))
                stuck = 0
    st.note(g, "floor time limit")
    return False
