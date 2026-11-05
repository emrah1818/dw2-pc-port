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


_learned = {}  # (map key, index, dir) -> index the game moved to (verified by nav)


def goto_index(g, target, max_presses=40):
    """Move the cursor to recs[target]; True when it is there (verified by nav). Moves the game
    made are learned per map; unknown moves use the Stg20_AreaSelectFindDir prediction first."""
    for _ in range(max_presses):
        n = g.nav()
        if n is None:
            return False
        cur, recs = n
        if cur == target:
            return True
        key = (g.mode(), tuple(r[1:] for r in recs))

        def moves(p):
            for d in range(4):
                k = (key, p, d)
                if k in _learned:
                    if _learned[k] != p:
                        yield d, _learned[k], 1
                else:
                    t = step_target(recs, p, d)
                    yield d, (t if t >= 0 else None), 3
        # cheapest path over learned moves (cost 1) and predicted / untried ones (cost 3)
        import heapq
        dist = {cur: 0}
        first = {cur: None}
        heap = [(0, cur)]
        while heap:
            c, p = heapq.heappop(heap)
            if p == target:
                break
            if c > dist.get(p, 1e9):
                continue
            for d, t, w in moves(p):
                if t is None:
                    continue
                if c + w < dist.get(t, 1e9):
                    dist[t] = c + w
                    first[t] = d if p == cur else first[p]
                    heapq.heappush(heap, (c + w, t))
        d = first.get(target)
        if d is None:
            # nothing predicted: try a direction not learned yet from here
            untried = [d for d in range(4) if (key, cur, d) not in _learned]
            if not untried:
                return False
            d = untried[0]
        g.tap(DIRS[d], 3, 12)
        n2 = g.nav()
        if n2 is None:
            return False
        _learned[(key, cur, d)] = n2[0]
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
