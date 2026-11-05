#!/usr/bin/env python3
"""dw2-pc-port test driver: starts dw2 with --bot over two named pipes and drives it lock-step.

    from dw2bot import Game
    g = Game(exe, pak, workdir)          # starts the program headless
    g.run(60)                            # 60 waits with no buttons
    g.tap("Start")                       # press, then release
    g.u32("Sys_State", 0x18)             # read globals by symbol (+ offset)
    g.shot("title")                      # display + VRAM PNG in workdir/shots

Symbols come from `nm` of the same exe; the program reports where Sys_State is at run time, every
other address follows from the link offsets. Linux only (named pipes, nm)."""

import os
import struct
import subprocess
import sys
import tempfile
import time

BUTTONS = ["Select", "L3", "R3", "Start", "Up", "Right", "Down", "Left",
           "L2", "R2", "L1", "R1", "Triangle", "Circle", "Cross", "Square"]


def load_symbols(exe):
    out = subprocess.run(["nm", "-S", "--defined-only", exe], capture_output=True, text=True, check=True).stdout
    syms = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 4:
            addr, size, _t, name = parts
            syms[name] = (int(addr, 16), int(size, 16))
        elif len(parts) == 3:
            addr, _t, name = parts
            syms.setdefault(name, (int(addr, 16), 0))
    return syms


class GameError(RuntimeError):
    pass


class Game:
    def __init__(self, exe, pak, workdir, extra=(), log_name="game.log", env=None):
        self.exe = os.path.abspath(exe)
        self.workdir = os.path.abspath(workdir)
        os.makedirs(self.workdir, exist_ok=True)
        self.syms = load_symbols(self.exe)
        fifo_dir = tempfile.mkdtemp(prefix="dw2bot")
        self.cmd_path = os.path.join(fifo_dir, "cmd")
        self.rep_path = os.path.join(fifo_dir, "rep")
        os.mkfifo(self.cmd_path)
        os.mkfifo(self.rep_path)
        args = [self.exe, "--pak", os.path.abspath(pak), "--no-window", "--fast", "--renderer", "soft",
                "--no-update-check", "--save-dir", os.path.join(self.workdir, "saves"),
                "--bot", self.cmd_path, self.rep_path] + list(extra)
        os.makedirs(os.path.join(self.workdir, "saves"), exist_ok=True)
        self.log = open(os.path.join(self.workdir, log_name), "w")
        e = dict(os.environ)
        if env:
            e.update(env)
        self.proc = subprocess.Popen(args, cwd=self.workdir, stdout=self.log, stderr=subprocess.STDOUT, env=e)
        # open order matches the program: it opens IN (our write end) first, then OUT
        self.cmd = open(self.cmd_path, "w")
        self.rep = open(self.rep_path, "r")
        hello = self._call("hello").split()
        self.base = int(hello[1], 16)
        self.wait = int(hello[3])
        self.vb = int(hello[5])
        self.delta = self.base - self.syms["Sys_State"][0]
        self.t0 = time.time()

    # ---- channel ----
    def _call(self, line):
        if self.proc.poll() is not None:
            raise GameError("program ended (exit %s), see %s" % (self.proc.returncode, self.log.name))
        self.cmd.write(line + "\n")
        self.cmd.flush()
        r = self.rep.readline()
        if not r:
            self.proc.wait(timeout=10)
            raise GameError("program ended (exit %s) during %r" % (self.proc.returncode, line))
        r = r.strip()
        if r.startswith("err"):
            raise GameError("%s -> %s" % (line, r))
        return r

    def run(self, n=1, buttons=None):
        """n waits holding buttons ("Cross", "Up+Cross", None)."""
        r = self._call("run %d %s" % (n, buttons or "-")).split()
        self.wait, self.vb = int(r[2]), int(r[4])
        return self

    def tap(self, buttons, hold=3, after=6):
        self.run(hold, buttons)
        self.run(after)
        return self

    def addr(self, sym, off=0):
        if isinstance(sym, int):
            return sym + off
        return self.syms[sym][0] + self.delta + off

    def size(self, sym):
        return self.syms[sym][1]

    def peek(self, sym, off=0, n=4):
        r = self._call("peek %x %d" % (self.addr(sym, off), n))
        return bytes.fromhex(r[4:])

    def poke(self, sym, off, data):
        self._call("poke %x %s" % (self.addr(sym, off), data.hex()))

    def u8(self, sym, off=0):
        return self.peek(sym, off, 1)[0]

    def s16(self, sym, off=0):
        return struct.unpack("<h", self.peek(sym, off, 2))[0]

    def u16(self, sym, off=0):
        return struct.unpack("<H", self.peek(sym, off, 2))[0]

    def s32(self, sym, off=0):
        return struct.unpack("<i", self.peek(sym, off, 4))[0]

    def u32(self, sym, off=0):
        return struct.unpack("<I", self.peek(sym, off, 4))[0]

    def ptr(self, sym, off=0):
        """A 32-bit pointer the game keeps: the native address in the game's 16 MB window."""
        v = self.u32(sym, off)
        return 0 if v == 0 else (self.base & ~0xFFFFFFFF) | v

    def shot(self, tag):
        d = os.path.join(self.workdir, "shots")
        os.makedirs(d, exist_ok=True)
        self._call("shot %s %s" % (d, tag))
        return os.path.join(d, tag + "_display.png")

    def hdshot(self, tag):
        d = os.path.join(self.workdir, "shots")
        os.makedirs(d, exist_ok=True)
        path = os.path.join(d, tag + "_hd.png")
        r = self._call("hdshot %s" % path)
        return path, r[3:]

    def devedit(self, spec):
        """Dev builds: a PD.4 state edit for the next VBlank (a test set-up, not play)."""
        self._call("devedit " + spec)

    def dung(self):
        """The dungeon floor, or None: dict cols, rows, x, y, floor, grid (rows of '.' / '#'),
        ents [(kind, x, y)]."""
        try:
            r = self._call("dung").split()
        except GameError:
            return None
        cols, rows, x, y, floor = (int(v) for v in r[1:6])
        cells = r[6]
        ents = [] if r[7] == "-" else [tuple(int(v) for v in e.split(",")) for e in r[7].split(";") if e]
        return {"cols": cols, "rows": rows, "x": x, "y": y, "floor": floor,
                "grid": [cells[i * cols:(i + 1) * cols] for i in range(rows)], "ents": ents}

    def nav(self):
        """The open city / area map pick, or None: (index, [(id, mode, arg, x, y), ...])."""
        r = self._call("nav").split()
        if r[1] == "none":
            return None
        recs = [] if r[2] == "-" else [tuple(int(v, 0) for v in e.split(",")) for e in r[2].split(";")]
        return int(r[1]), recs

    # ---- common state ----
    def mode(self):
        return self.s32("Sys_State", 0x18)

    def next_mode(self):
        return self.s32("Sys_State", 0x1C)

    def close(self):
        try:
            if self.proc.poll() is None:
                self.cmd.write("quit\n")
                self.cmd.flush()
                self.proc.wait(timeout=20)
        except Exception:
            self.proc.kill()
        self.log.close()

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()


if __name__ == "__main__":
    g = Game(sys.argv[1], sys.argv[2], sys.argv[3])
    print("base 0x%x, mode 0x%x" % (g.base, g.mode()))
    for _ in range(10):
        g.run(100)
        print("wait", g.wait, "vb", g.vb, "mode 0x%x" % g.mode())
    g.shot("selftest")
    g.close()
