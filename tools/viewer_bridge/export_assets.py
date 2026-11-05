#!/usr/bin/env python3
"""dw2-pc-port developer tool: the DW2 3D Viewer (github.com/emrah1818/dw2-viewer-android) as
an asset exporter for the PC port, used read-only from its own checkout (nothing is copied or
changed there).

    export_assets.py --viewer PATH/dw2-viewer-android/app/src/main/python --disc IMAGE.cue OUT
                     [--ids 1,2,3 | --limit N]

For each Digimon: the viewer's converter (native model -> glTF with textures per CLUT, the
v3.1 palettes, animations) writes OUT/models/<id>_<name>/ and a catalog OUT/catalog.json. The
files come from the player's own disc and stay on the player's PC: they are game data and must
not be shared. Uses: a reference for HD texture work (texture PNGs with the game's palettes),
checking models and animations outside the game, preparing replacement assets."""
import argparse
import json
import shutil
import sys
import tempfile
import time
from pathlib import Path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--viewer", required=True, help="dw2-viewer-android/app/src/main/python")
    ap.add_argument("--disc", required=True)
    ap.add_argument("out")
    ap.add_argument("--ids", default="")
    ap.add_argument("--limit", type=int, default=0)
    a = ap.parse_args()
    sys.path.insert(0, str(Path(a.viewer).resolve()))
    import android_api as api  # the viewer's own API module

    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    cache = Path(tempfile.mkdtemp(prefix="dw2viewer"))
    api.configure(cache, None)
    info = api.open_image(a.disc)
    cat = api.catalog_list()
    ids = [int(v, 0) for v in a.ids.split(",") if v] if a.ids else [c["id"] for c in cat]
    if a.limit:
        ids = ids[:a.limit]
    done = []
    t0 = time.time()
    for i in ids:
        try:
            r = api.prepare_model(i)
            src = (cache / "viewer" / r["url"]).parent
            dst = out / "models" / ("%03d_%s" % (r["id"], "".join(ch for ch in r["name"] if ch.isalnum())))
            if dst.exists():
                shutil.rmtree(dst)
            shutil.copytree(src, dst)
            files = sorted(p.name for p in dst.iterdir())
            done.append({"id": r["id"], "name": r["name"], "animations": r["animations"], "files": files})
            print("ok %3d %-16s %d files, %d animations" % (r["id"], r["name"], len(files), len(r["animations"])), flush=True)
        except Exception as e:  # keep going, report it
            done.append({"id": i, "error": str(e)})
            print("error %d: %s" % (i, e), flush=True)
    with open(out / "catalog.json", "w") as f:
        json.dump({"image": info, "exported": done, "seconds": round(time.time() - t0, 1)}, f, indent=1)
    shutil.rmtree(cache, ignore_errors=True)
    bad = [d for d in done if "error" in d]
    print("%d exported, %d errors" % (len(done) - len(bad), len(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
