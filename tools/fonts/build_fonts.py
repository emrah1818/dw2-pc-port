#!/usr/bin/env python3
"""Rebuild renamed static instances from hash-pinned upstream font downloads.

Requires fonttools. Normal CMake builds do not run this script or use network.
"""
import hashlib
import io
import json
from pathlib import Path
import urllib.request

from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

ROOT = Path(__file__).resolve().parents[2]
DIRECTORY = ROOT / "misc/fonts"
FAMILIES = ["DW2 Interface", "DW2 Display", "DW2 Telemetry"]
ROLES = ["INTERFACE", "DISPLAY", "TELEMETRY"]


def main():
    sizes = ["/* Sizes of the embedded, committed font files. */"]
    for source, family, role in zip(
        json.loads((DIRECTORY / "sources.json").read_text()), FAMILIES, ROLES
    ):
        data = urllib.request.urlopen(source["upstream"], timeout=45).read()
        if hashlib.sha256(data).hexdigest() != source["upstream_sha256"]:
            raise RuntimeError(f"Upstream hash changed: {source['upstream']}")
        font = instantiateVariableFont(
            TTFont(io.BytesIO(data)), {"wght": source["weight"]}, inplace=True
        )
        # Respect reserved names for these modified font instances.
        for name_id in [1, 2, 3, 4, 6, 16, 17]:
            font["name"].removeNames(nameID=name_id)
            value = (
                family if name_id in [1, 3, 4, 16]
                else family.replace(" ", "") if name_id == 6
                else "Regular"
            )
            font["name"].setName(value, name_id, 3, 1, 0x409)
        font.recalcTimestamp = False
        path = DIRECTORY / source["file"]
        font.save(path)
        sizes.append(f"#define DW2_{role}_SIZE {path.stat().st_size}")
    (DIRECTORY / "sizes.h").write_text("\n".join(sizes) + "\n")


if __name__ == "__main__":
    main()
