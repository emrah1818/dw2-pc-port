# Modern typefaces — status, 7 November 2026

The typefaces are embedded TrueType fonts (below) and the game's lettering is
**rasterized from their outlines at the output resolution while the game runs**
(details: "Runtime rendering"). This is not yet a verified complete replacement
of every piece of lettering in the game; the open items are listed at the end.

| Use | Typeface | Integration |
|---|---|---|
| Dialogues, menus, ordinary text | Exo 2 Medium | Replaces letters and punctuation in both game atlases, including ÄÖÜäöüß; also the ImGui PC interface |
| Large title-menu and battle word pictures | Orbitron Bold | Rasterizes the 25 existing picture mappings using their current DE/EN spelling, palettes, layout and animation |
| Attack-name pictures | Orbitron Bold | Reads names from the current layered WAZADATA, including names that remain English; does not invent translations |
| Battle-status letter cells | Orbitron Bold | Replaces known alphabet cells, retaining the existing German wording, including ae/oe/ue |
| Numbers, II, L1–L5 | Roboto Mono Medium | Main font atlases; numeric sprites identified by `Gfx_SetPartsNumber` are also replaced when drawn |
| Menu word cells | Exo 2 Medium | Restyles the 18 known word ranges; retains existing translations and `words.tsv` overrides |
| Stats image labels | Roboto Mono Medium | Five shared texture regions in 0x13B (HP, two MP sizes, EL, slash), preserving their spelling and adjacent graphics |
| Development scene font | Roboto Mono Medium | Rebuilds all 64 8×8 ASCII cells in a private buffer before upload; original disc table remains unchanged |
| Copyright and controller notice | Exo 2 Medium | Repaints 0xB64:5/6, retaining wording and the controller notice's enclosing frame |

The font binaries are embedded. No system installation, network download or
SDL_ttf dependency is required to run or build the program. Fonts apply in both
languages and with both interface styles, as requested. Existing Classic visual
reference hashes therefore describe the **previous font system**; they must not
be updated before reviewing the intentional image changes.

The game atlases keep their 7×9 and 8×13 glyph cells in VRAM (the game's layout,
spacing and the save/compatibility rules do not change). What reaches the screen
is drawn at the output resolution (next section). Old outline/bold dilation is
removed; it closes the counters of the new letters. A missing character in
Orbitron uses Exo 2; unsupported characters cause an explicit failure rather
than silent character deletion.

## Runtime rendering at the output resolution

Path: `host/fonts.c` / `host/picwords.c` register each texture that holds
lettering (both game atlases, every file with a painted word picture) as a
*runtime index source* (`backend/texrep.h`, `TexRep_AddIndexSource`). When the
HD renderer (scale k ≥ 2: 3 at 720 lines, 6 at 1440, 9 at 2160) draws a primitive
from such a texture, `backend/texrep.c` asks the source for that glyph cell or
picture tile **k times larger** (times the primitive's magnification, at most
16): the letters are rasterized with stb_truetype from the outlines at exactly
that pixel size, with the same scale, baseline, centring and line fitting as the
1x texture, as palette indices. The tile is then coloured through the game's
palette of that primitive, so text colours, palette cycling (battle banners),
drawing order, clipping and fades stay the game's. Both renderers use it
(software: `rep_texel`; GPU: the replacement pool and `hd.frag rep_sample`, which looks the
index up in the CLUT in VRAM).

- Coverage → ink: index 1 where ≥ 28 % of the output pixel is covered (the 1x
  cells' stroke weight). There is no per-pixel blending against the background:
  the PS1 pipeline the renderers emulate has no alpha per texel. Edges are exact
  at the output pixel, not smoothed by partial transparency.
- Tiles hold palette indices (1 byte per pixel) and are cached per cell and k,
  independent of the palette: the primitive's CLUT colours them when drawn, so palette
  cycling and fades reuse them (at most 256 MiB, at most 1 M pixels per tile; when full
  they are dropped after the current picture and made again). Measurements and the
  slow-motion fix of 7 November 2026: [PERFORMANCE.md](PERFORMANCE.md).
- Partial VRAM writes into such a page mark only that part dirty (the original is
  drawn there).
- Kerning and advance: the game positions each glyph by its own layout (1x pixel
  positions, `host/uilayout.c` measures the glyph widths); the sharper glyphs sit
  in those positions.
- Not covered: Classic UI in English (bit-exact PS1 picture), scale 1, the
  numeric sprites of `host/fontnumbers.c` and the multi-cell words inside the
  atlas (they are the 1x texels enlarged), letters burned into movies.

Checked: `fonts_test` (registered atlas source at k 2, 7, 12), `hd_test` (an
index source drawn exactly through a palette, software and GPU on lavapipe in
CI), and in the running game at scale 3, 6 and 9 (load screen, result window,
battle words including zooming banners, attack names, title copyright and code).

## Content and data

No translation files are changed. The picture renderer reads attack names after
the translation layer has finished. Word overrides retain their existing meaning:
an empty override keeps the English word, rendered in the new typeface. Controller
icons remain icons. The original disc package is unchanged; game-font and picture
changes use the existing in-memory layer. Numeric sprites update only registered
glyph rectangles in VRAM, preserving adjoining packed pixels and palettes.

## Validation performed

- Linux release build without disc assets: successful.
- `fonts_test`: all three faces, German characters, bounded mask rendering,
  invalid UTF-8 rejection, atlas header/palette isolation, numeric sprites in
  4-bit and 8-bit textures, adjoining-pixel isolation, cache invalidation.
- The same test with AddressSanitizer and UndefinedBehaviorSanitizer: successful
  (`ASAN_OPTIONS=detect_leaks=0`; leak detection is unavailable under this runner's
  process tracing).
- Existing `textscript_test`: 90/90 checks passed.

Additional game-data validation:

- The user's previously uploaded USA image was located and imported locally:
  all 3,675 files matched the existing manifest. The private German 1.4.0
  package was also located; neither the image nor its derived data is committed.
- Runtime reports 92 replaced glyphs in each atlas, 2,582 German text entries
  with zero translation failures, 25/25 known word pictures and 207/207 collected
  attack-name pictures painted. These painting counts do not mean every effect
  has been seen in battle.
- Title/title-menu, load screen and German field-menu screenshots were captured.
  The title labels and field menu were inspected. This revealed that the old
  outline dilation closed the new letters' counters and that 4-bit sprite palettes
  are embedded inside their texture. Both were corrected; labels now use their
  actual cell palette and the font's own strokes.
- One German battle ran to the result screen and returned to the dungeon, with
  23 captured pictures. The captures cover target selection, command lists,
  attack/damage labels, attack-name pictures and results; this is not an audit
  of every attack or every status condition.
- `texts.tsv` and `dictionary.txt` remain byte-identical to the supplied package.
- Save/load regression: 9/9 checks, including exact restored save data (apart
  from the expected play-time exclusion) and four damaged-card cases.

## Required before completion

### Continuation validation

- Both release and development Linux builds pass. `fonts_test`, including new
  bitmap bounds and picture-table directory regressions, passes; the sanitizer
  build also passes. `textscript_test` remains 90/90.
- Picture directory parsing now stops at the earliest payload pointer while
  scanning. Previously sprite payload bytes could be mistaken for offsets;
  the affected inventory includes all 76 entries in 0x13B rather than 24.
- Runtime logs report stats labels 5/5 and title notices 2/2. The title screen
  was inspected after correcting the narrower second copyright line.
- A further German battle reached the result screen and returned to the
  dungeon, producing 23 captures. The party HUD with the new HP/MP labels was
  visually checked.
- Development mode 0x104 completed 400 VBlanks and its font display was inspected.
  The 0x101 crash was a lineup overflow in the original debug menu (fixed in
  9b5fefd, see TESTBERICHT.md); it was unrelated to the fonts.
- Pull requests now run the Windows/Linux build-and-test jobs. The release job
  explicitly excludes pull-request events. Remote CI results must be checked;
  adding the jobs is not evidence that Windows has passed.

The local game-data checks above are a first pass, not a complete visual audit.
- Audit all atlas cells and other textures for lettering outside the known
  mappings, including any additional stats labels, the title logo, title
  background text, intro/boot screens and any lettering in movies.
  **Those are not covered merely by replacing the two main atlases.**
- Check the numeric sprite hook in battles, shops, status screens and saves.
- Check every menu, dialogue, save window and battle banner for clipping,
  readability, baseline, colour and animation at 4:3, 16:9 and 21:9.
- Compare German text content with the pre-change build, including custom word
  overrides and untranslated attack names. Complete EN/DE gameplay regressions.
- Build and test Windows, including actual window/UI/controller behaviour.

Do not publish a release or claim “no original fonts remain” until these pass.

## Sources and licences

Static instances are derived from the Google Fonts sources listed with their
download SHA-256 and chosen weights in `misc/fonts/sources.json`. Internal names
are changed to DW2 Interface / DW2 Display / DW2 Telemetry. Each font's copyright
notice and SIL Open Font License are included beside the binaries. They remain
under that licence; the repository's CC0 declaration does not relicense them.

- [Exo 2 sources](https://github.com/google/fonts/tree/main/ofl/exo2)
- [Orbitron sources](https://github.com/google/fonts/tree/main/ofl/orbitron)
- [Roboto Mono sources](https://github.com/google/fonts/tree/main/ofl/robotomono)

Licence check (7 November 2026): the three OFL files declare no Reserved Font
Name, so the renamed static instances are permitted; embedding in the program and
distributing them with their copyright notice and licence is permitted by the OFL
(sections 1 and 2); they are not sold on their own.

Consult `tools/fonts/build_fonts.py` to reproduce the static instances with
fontTools. Normal builds consume the committed binaries directly.
