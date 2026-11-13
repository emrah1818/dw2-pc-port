#!/usr/bin/env python3
"""dw2-pc-port: graphics profiles from the command line, without game data.

    python3 tests/profile_test.py EXE

Starts EXE headless with a pack that does not exist: the program prints its settings and stops at
the pack check. Checks the values of each profile (host/settings.c Settings_ProfileValue), the
old names, that a single option wins over the profile and that an unknown name is refused."""

import re
import subprocess
import sys

EXPECT = {
    "original": dict(scale=1, wide=0, pgxp=0, sharpen=0, smooth=0, texture_filter="nearest", smooth_2d=0),
    "balanced": dict(scale=3, wide=1, pgxp=1, sharpen=0, smooth=1, texture_filter="bilinear", smooth_2d=0),
    "hd": dict(scale=0, wide=1, pgxp=1, sharpen=25, smooth=1, texture_filter="bilinear", smooth_2d=1),
    "ultra": dict(scale=9, wide=1, pgxp=1, sharpen=40, smooth=1, texture_filter="bilinear", smooth_2d=1),
}
ALIAS = {"classic": "original", "remastered": "hd"}


def run(exe, *args):
    p = subprocess.run([exe, "--no-window", "--no-update-check", "--pak", "profile_test_missing.pak", *args],
                       capture_output=True, text=True, timeout=120)
    return p.returncode, p.stdout + p.stderr


def values(out):
    got = {}
    for line in out.splitlines():
        if line.startswith("[settings] "):
            for key, val in re.findall(r"(\w+) ([\w-]+)(?:,|$)", line[len("[settings] "):]):
                got[key] = val
    return got


def main():
    exe = sys.argv[1]
    bad = 0
    for name in list(EXPECT) + list(ALIAS):
        code, out = run(exe, "--profile", name)
        got = values(out)
        for key, want in EXPECT[ALIAS.get(name, name)].items():
            if got.get(key) != str(want):
                print("FAIL --profile %s: %s is %r, expected %r" % (name, key, got.get(key), want))
                bad += 1
    code, out = run(exe, "--profile", "ultra", "--scale", "2", "--sharpen", "10")
    got = values(out)
    if got.get("scale") != "2" or got.get("sharpen") != "10" or got.get("pgxp") != "1":
        print("FAIL single options do not win over the profile: %r" % got)
        bad += 1
    code, out = run(exe, "--profile", "nonsense")
    if code != 2 or "unknown profile" not in out:
        print("FAIL unknown profile not refused (exit %d)" % code)
        bad += 1
    print("profile_test: %s" % ("passed (4 profiles, 2 old names, option priority, unknown name)" if bad == 0 else "%d failure(s)" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
