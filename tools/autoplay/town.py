#!/usr/bin/env python3
"""dw2-pc-port: walking and talking in the city's walking scenes (game table 3), through pad input.
The bot reads the scene grid with the town command; the game sees only the D-pad and buttons.

Grid: grid[x][y], '.' free, '#' wall, 'n' a walker (NPC), 'P' the player. Up = y - 1,
Down = y + 1, Left = x - 1, Right = x + 1."""
import collections

STEP = {(0, -1): "Up", (0, 1): "Down", (-1, 0): "Left", (1, 0): "Right"}


def talk_active(g):
    return g.s32("Stg20_MenuState", 0x04) != 0


def clear_talk(g, limit=200):
    """Cross through an open conversation (first choice on questions) until it closes."""
    n = 0
    while n < limit:
        if not talk_active(g):
            g.run(20)
            if not talk_active(g):
                return n
        g.tap("Cross", 3, 14)
        n += 1
    return n


def bfs(t, goals, allow_npc_goal=False):
    start = (t["x"], t["y"])
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
        for dx, dy in STEP:
            n = (p[0] + dx, p[1] + dy)
            if not (0 <= n[0] < 24 and 0 <= n[1] < 24) or n in prev:
                continue
            c = t["grid"][n[0]][n[1]]
            if c == "." or c == "P" or (allow_npc_goal and c == "n" and n in goals):
                prev[n] = p
                q.append(n)
    return None


def step(g, here, nxt):
    btn = STEP[(nxt[0] - here[0], nxt[1] - here[1])]
    for _ in range(3):
        g.run(10, btn)
        for _ in range(4):
            g.run(4)
            t = g.town()
            if t is None or (t["x"], t["y"]) != here:
                return True
        if talk_active(g):
            clear_talk(g)
    return False


def walk_to(g, goals, max_steps=80):
    """Walk to one of the goal cells. True when the player stands on one (or the scene changed:
    an exit cell starts the fade at once)."""
    goals = set(goals)
    mode0 = g.mode()
    for _ in range(max_steps):
        if g.mode() != mode0:
            return True
        t = g.town()
        if t is None:
            return True
        here = (t["x"], t["y"])
        if here in goals:
            return True
        p = bfs(t, goals)
        if not p:
            return False
        if not step(g, here, p[0]) and talk_active(g):
            clear_talk(g)
    return False


def npcs(t):
    return [(x, y) for x in range(24) for y in range(24) if t["grid"][x][y] == "n"]


def talk_to(g, cell, tries=4):
    """Stand next to the walker on cell, face it, Cross, and go through the conversation.
    Returns the number of Cross presses the talk took (0 = no talk opened). A walker that is
    moving (or the 30-frame pause after a talk) does not answer, so it retries."""
    for attempt in range(tries):
        t = g.town()
        if t is None:
            return 0
        # the walker may have moved: the 'n' cell nearest to where it was
        cands = [c for c in npcs(t) if abs(c[0] - cell[0]) + abs(c[1] - cell[1]) <= 2]
        if not cands:
            return 0
        cell = min(cands, key=lambda c: abs(c[0] - cell[0]) + abs(c[1] - cell[1]))
        adj = {(cell[0] + dx, cell[1] + dy): (-dx, -dy) for dx, dy in STEP}
        adj = {c: d for c, d in adj.items() if 0 <= c[0] < 24 and 0 <= c[1] < 24 and t["grid"][c[0]][c[1]] in ".P"}
        if not adj:
            return 0
        if not walk_to(g, adj.keys()):
            if talk_active(g):
                return 1 + clear_talk(g)
            continue
        if talk_active(g):  # a trigger cell on the way opened a talk
            return 1 + clear_talk(g)
        t = g.town()
        if t is None:
            return 0
        here = (t["x"], t["y"])
        if here not in adj:
            continue
        g.run(36)  # the pause after an earlier talk
        g.tap(STEP[adj[here]], 2, 8)  # the cell is taken, so the player only turns
        g.tap("Cross", 3, 24)
        if talk_active(g):
            return 1 + clear_talk(g)
    return 0


def leave(g, mode=None):
    """Walk onto an exit (to the scene `mode`, else the city map 0x32D, else any)."""
    t = g.town()
    if t is None:
        return False
    ex = [e for e in t["exits"] if mode is None or e[2] == mode]
    if not ex and mode is None:
        ex = t["exits"]
    if not ex:
        return False
    m0 = g.mode()
    walk_to(g, [(e[0], e[1]) for e in ex])
    for _ in range(60):
        if g.mode() != m0:
            break
        g.run(10)
    g.run(60)
    return g.mode() != m0
