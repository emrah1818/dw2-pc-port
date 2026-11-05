#!/usr/bin/env python3
"""dw2-pc-port: pick a destination on the open city / area map by pressing the D-pad (the bot
reads the shown destinations with the nav command; the game only sees pad input)."""
import collections

DIRS = ("Down", "Left", "Up", "Right")  # the game's Stg20_AreaSelectFindDir order 0..3


def _cell_dist(c, r, vertical):
    # Stg20_CellDistWeighted: the main axis counts once, the cross axis twice (approximation of the
    # retail weighting is not needed: the bot verifies every press with nav)
    dx, dy = abs(r[3] - c[3]), abs(r[4] - c[4])
    return (dy + 2 * dx) if vertical else (dx + 2 * dy)


def step_target(recs, cur, d):
    """The index the game moves to from cur for direction d (Stg20_AreaSelectFindDir), or -1."""
    c = recs[cur]
    best, found = 0x7D00, -1
    for i, r in enumerate(recs):
        if i == cur:
            continue
        ok = ((d == 0 and c[4] < r[4]) or (d == 1 and c[3] > r[3]) or
              (d == 2 and c[4] > r[4]) or (d == 3 and c[3] < r[3]))
        if ok:
            dist = _cell_dist(c, r, d in (0, 2))
            if dist < best:
                best, found = dist, i
    return found


def goto_index(g, target, max_presses=24):
    """Move the cursor to recs[target]; True when it is there (verified by nav)."""
    for _ in range(max_presses):
        n = g.nav()
        if n is None:
            return False
        cur, recs = n
        if cur == target:
            return True
        # BFS over the predicted moves; the first press is verified by the next nav
        prev = {cur: None}
        q = collections.deque([cur])
        while q:
            p = q.popleft()
            if p == target:
                break
            for d in range(4):
                t = step_target(recs, p, d)
                if t >= 0 and t not in prev:
                    prev[t] = (p, d)
                    q.append(t)
        if target not in prev:
            return False
        p, first = target, None
        while prev[p] is not None:
            p, first = prev[p][0], prev[p][1]
        g.tap(DIRS[first], 3, 12)
    n = g.nav()
    return n is not None and n[0] == target


def pick(g, mode=None, arg=None, index=None):
    """Choose a destination by its target mode (and arg) or index, then Cross. True if picked."""
    n = g.nav()
    if n is None:
        return False
    cur, recs = n
    if index is None:
        cand = [i for i, r in enumerate(recs) if r[1] == mode and (arg is None or r[2] == arg)]
        if not cand:
            return False
        index = cand[0]
    if not goto_index(g, index):
        return False
    g.tap("Cross", 3, 30)
    return True
