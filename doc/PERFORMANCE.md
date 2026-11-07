# Performance: runtime font tiles (7 November 2026)

Report from v1.5.0-preview (Windows, Nvidia GPU, 4K): as soon as an attack starts and the new
German attack names / word pictures appear, the game runs in slow motion and recovers later; CPU
5-7 %, GPU 18-30 %. This file records what was measured, the cause, the fix and what is not
verified.

## Measuring

`DW2_PERF_LOG=<file>` (any build, any renderer) writes one CSV line per VBlank wait of the game
thread (`host/sdl.c` perf_line):

| Column | Meaning |
|---|---|
| frame_ms | time since the previous wait (game logic + drawing + waiting) |
| flush_ms | in the HD renderer's flushes (software: rasterizing; GPU: building and submitting) |
| publish_ms | Host_Publish (display copy, tile trim) |
| sleep_ms, vblanks | waiting for the VBlank; VBlanks run at this wait (more than 1: the game thread was late) |
| finds / hits / new_tiles / new_kpx | texture replacement lookups, runtime tiles found in the cache / made (output kilopixels) |
| full / drops | tile requests refused because the tile memory was full / complete tile drops |
| raster_ms / conv_ms / style_ms | glyph and picture rasterization, palette conversion, Modern UI panel style |
| gpu_ups / gpu_kib / gpu_up_ms / gpu_deferred | uploads into the GPU replacement pool, their size and time; lookups deferred or refused (pool full or per-flush limit: the original is drawn meanwhile) |
| tiles / tile_kib / used_kib | tiles held, their memory, the distinct tiles drawn in this picture |
| mag1..mag4 | tile uses by the primitive's magnification |

`DW2_PERF_TILES=1` also prints every new tile (source, box, k, magnification). On Windows,
`DW2-Leistung.bat` in the program folder starts the game with the log and writes a summary
(`test_out\leistung.txt`: frame time percentiles, late waits, tiles, uploads, the slowest frames).

Reproducible runs here: `tools/autoplay/battle_words.py` (one German battle with attacks, result
screen) with `--extra="--settings-set language=de --settings-set ui_style=modern --scale 9"`; the
GPU path headless on Mesa lavapipe with `DW2_GPU_HEADLESS=1 SDL_VIDEO_DRIVER=offscreen --renderer gpu`
(`DW2_GPU_HEADLESS` only keeps the GPU renderer when the window has no GPU presentation; it does
not change drawing).

## Cause (measured)

Runtime text (attack names, battle words, both font atlases) is rasterized from the font outlines
at the output resolution into tiles (`backend/texrep.c` idx_tile). Up to v1.5.0-preview **a tile
was stored in its final colours, one tile per palette state**. The battle banners, attack names
and the title's pulsing text cycle or fade their palettes every few frames, so each colour state
made a new full-size tile on the game thread (at 4K up to 1 M pixels = 4 MiB each) and uploaded it
into the GPU replacement pool again.

- GPU path, 4K, first 591 frames (title screen and load): 378 tiles made (43 M pixels), **2471
  uploads = 1076 MiB** into the pool, the pool (128 MiB) overflowed 9 times and started over (each
  time every tile in view was uploaded again; meanwhile 528 lookups drew the original pixel font),
  tile memory up to 165 MiB, 630 ms of upload work on the game thread.
- Software path, 4K, one complete battle: 925 tiles made (276 M pixels), the 256 MiB tile memory
  ran full 284 times and was dropped and regenerated 4 times.

The game thread runs the game logic and builds the drawing, and the game's animation advances per
frame: when that thread is held up by tile generation and uploads, the game runs slower (slow
motion), while the CPU as a whole (one busy core of many) and the GPU stay lightly loaded. That
matches the report.

Further findings in the same path:
- A magnified primitive (zooming words) averaged k / scale = magnification² tile pixels per
  output pixel (4 reads at magnification 2, 16 at 4) although its tile is made at exactly its output
  size: extra reads in the shader / software sampler and slightly blurred edges.
- Every pool upload created and released its own GPU transfer buffer (an allocation per tile).
- Word pictures re-rasterized the masks of **all** pictures of a file whenever a tile at a
  different k (magnification) was made.

Not a cause: shader or pipeline creation (all pipelines are created at start), the font files
(loaded once), SDL3 presentation (the window thread is separate).

## Fix

1. Tiles hold **palette indices** (1 byte per pixel instead of 4) and are independent of the
   palette: the primitive's CLUT colours them when drawn, exactly like the game's own textures
   (`TexRep_Sample` in software, `rep_sample` in `backend/shaders/hd.frag` reads the CLUT from the
   VRAM buffer). Palette cycling, fades and colour changes reuse the same tile; nothing is made or
   uploaded again. Tiles with the Modern UI panel style (on by default in Modern since preview2,
   `DW2_MODERN_PANELS=0` turns it off) are still coloured when made, since the style's colours are
   not the palette's; see "Panel style spikes" below.
2. The sampling block follows the primitive's magnification: one read per output pixel for runtime
   tiles; texture packs on magnified primitives are averaged over the pixels they actually cover.
3. One transfer buffer for all pool uploads of a flush, kept and cycled.
4. Word picture masks are cached per picture for its two latest sizes (budget 64 MiB) and only
   made for pictures inside the requested tile.

Quality is unchanged: same output resolution (4K = 9x), same fonts, same German names, GPU
rendering; no resolution or feature is reduced.

## Results (this machine: 2 CPU cores, no GPU; GPU = Mesa lavapipe on the CPU)

| | before | after |
|---|---|---|
| GPU 4K, 591 frames: tiles made | 378 (43.2 M px) | 30 (4.1 M px) |
| GPU 4K, 591 frames: pool uploads | 2471 (1076 MiB, 630 ms) | 30 (4.0 MiB, 2.5 ms) |
| GPU 4K, 591 frames: pool overflows / deferred lookups | 9 / 528 | 0 / 0 |
| GPU 4K, 591 frames: tile memory peak | 165 MiB | 4 MiB |
| Software 4K, one battle: tiles made | 925 (276 M px) | 225 (36 M px) |
| Software 4K, one battle: rasterization + conversion | 1.87 s | 0.20 s |
| Software 4K, one battle: tile memory peak / memory full / drops | 254 MiB / 284 / 4 | 34 MiB / 0 / 0 |
| GPU 720p (scale 2), one battle: tiles made / pool uploads | 831 / 68 MiB | 225 / 2 MiB |
| GPU 720p, attack phase (frames 3600-4400): tiles made / uploads | 198 / 11.9 MiB | 72 / 0.7 MiB |

The GPU battle ran at scale 2 because lavapipe is too slow for a whole battle at 4K; the "before"
column there is the new build with the old per-palette tiles forced (a temporary switch, not
committed). Tile sizes grow with the square of the scale (4K = 9x: about 20 times the 720p
pixels), so at 4K the same battle uploads in the order of 1.4 GB before (with pool overflows, as
measured on the title screen) and about 40 MiB after.

First vs repeated display: before, a repeated attack name with a different palette state was a new
tile (first and repeated displays alike were slow); after, only the first display of a glyph or
picture at a size makes a tile (on average 0.9 ms for one at 4K here), repeats are cache hits.

Frame times of the software renderer at 4K are dominated by software rasterizing on this 2-core
machine (English Classic, without any runtime tiles, takes the same 13-48 s per 400 frames as
German Modern); there the tile work was a small share (1.9 s of 410 s). After the fix the
total is the same (410.5 s): in the attack phase German Modern stays about 20 ms per frame slower
than English Classic in software (frames 3600-4400: 96.8 s against 80.2 s) although no tiles
are made there; this share is not the tiles (candidates: the per-pixel replacement lookup of
the software renderer, larger German text areas). Not yet isolated. The slowdown reported on
Nvidia is the GPU path, where the measured upload and regeneration traffic above is what the game
thread waited on.

## Panel style spikes (perf-test1 measurement, 7 November 2026)

The player's perf-test1 log (GTX 1070, i7-6700K, 1080p, internal 4x, 582 s) showed the Modern UI
panel style at 1254 ms in total with a **peak of 90.3 ms in one frame** (about five frames late at
60 Hz). The main wait in that log is elsewhere (195 s inside `SDL_SubmitGPUCommandBufferAndAcquireFence`,
addressed by perf-test2); the style peaks are separate hitches.

Measured cause (`DW2_PERF_TILES=1`, menu tour at 4x): 212 styled tiles were made for only 99 different
panel pieces. The same piece is made again for every palette state (41 states: cursor colours,
highlights), and each time the style computed the distance field interpolation, edge and gradient
for every output pixel (k² pixels per texel), although those depend only on the panel shape.

Fix (`backend/texrep.c` style_plan): the style of one piece is computed once as a per-pixel plan
(as drawn / clear / style colour / style colour where the drawn texel is old blue shading) and kept
(keyed by the source and a hash of its panel classes, at most 48 MiB, freed with the tiles); another
palette only applies the plan. Texels whose whole 3x3 neighbourhood lies deeper inside the panel than
the edge width get the fill colour without the per-pixel interpolation. Output is unchanged.

| run (German, Modern, software renderer here) | style total before / after | peak frame before / after |
|---|---|---|
| menu tour, 4x | 108 ms / 25 ms | 26.8 ms / 5.8 ms |
| menu tour, 6x (1440p) | 236 ms / 48 ms | 59.8 ms / 11.7 ms |
| menu tour, 9x (4K) | 528 ms / 110 ms | 132.5 ms / 34.0 ms |
| one battle with attacks, 4x (6502 frames) | 634 ms / 116 ms | 80.0 ms / 13.1 ms |

All screenshots of these runs (5 per menu tour, 24 in the battle) are bit-identical before and after;
reference images (Classic, English) 6/6. The remaining largest raster peak in these runs is the
one-time boot of the title screen (frame 14). Not measured on the player's hardware yet.

## Player measurement with 1.6.0-preview10 (8 November 2026)

GTX 1070 (driver 32.0.15.6603), i7-6700K, 1080p window, GPU renderer, German, Modern UI, Smooth + VSync (mailbox pacing), PGXP, 21:9; 51 minutes of play (184,537 intervals).

| Internal scale | Intervals | Median | 95 % | 99 % | Late (more than one VBlank) | New tiles | Tile memory full / dropped |
|---|---|---|---|---|---|---|---|
| 4x | 51,935 | 16.69 ms | 32.17 ms | 33.10 ms | 191 | 1,407 | 2 / 1 |
| 5x | 132,572 | 16.74 ms | 32.22 ms | 32.86 ms | 124 | 4,638 | 8 / 3 |

The 32 ms values are the game's own 30 fps scenes (two VBlanks per game frame), not slow motion.

**Game thread waiting inside `SDL_SubmitGPUCommandBufferAndAcquireFence`:**

| Measurement | Wait | Share of play time |
|---|---|---|
| perf-test1 | 195 s in 582 s | 34 % |
| 1.6.0-preview10 | 97 s in 3,084 s | 3 % |

The perf-test2 pacing works on this machine. The player reports smooth battles.

**Remaining hitches:** 168 intervals over 40 ms (0.09 %, 45 ms on average, at most 75 ms). Of their 7.6 s:

| Share | Time | Note |
|---|---|---|
| VBlank wait | 2.1 s | |
| Tile rasterization | 0.8 s | |
| Palette conversion and panel style | 0.5 s | |
| Flush | 0.7 s | |
| GPU submit | 0.6 s | |
| Elsewhere in the game thread | about 2.7 s | |

1,195 of the session's 6,045 new tiles were made in these frames: mostly the first display of a screen's text at 5x. 13 of them follow one of the 4 tile-memory resets (255 MiB at 5x).

**Possible next step (not done):** make tiles on a worker thread, or ahead of time for a screen's known texts, instead of on the game thread at first use.

**Not measured by the player:** English/Classic, without the HD texture pack, internal 6x (1440p) and 9x (4K).

## Not verified

- **No test on real Nvidia (or any real GPU) hardware.** The GPU path ran on Mesa lavapipe, a
  software Vulkan driver: upload counts, tile counts and memory are exact, frame times are not
  representative of a real GPU. The Windows build is checked in CI (build and hd_test on lavapipe).
  `DW2-Leistung.bat` produces the same measurement on the player's PC.
- Windows-specific driver behaviour (transfer buffer cycling on the Nvidia Vulkan driver).
- The Modern UI panel style still makes palette-specific colour tiles (their style is now computed once per piece).
- The remaining software-renderer cost in the attack phase (above).
