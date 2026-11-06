# Modern typefaces — development status, 6 November 2026

This change implements the requested font system. **It is not yet a verified
complete replacement of every piece of lettering in the game.** It must remain
in a draft branch until the outstanding inventory and in-game checks below pass.

| Use | Typeface | Integration |
|---|---|---|
| Dialogues, menus, ordinary text | Exo 2 Medium | Replaces letters and punctuation in both game atlases, including ÄÖÜäöüß; also the ImGui PC interface |
| Large title-menu and battle word pictures | Orbitron Bold | Rasterizes the 25 existing picture mappings using their current DE/EN spelling, palettes, layout and animation |
| Attack-name pictures | Orbitron Bold | Reads names from the current layered WAZADATA, including names that remain English; does not invent translations |
| Battle-status letter cells | Orbitron Bold | Replaces known alphabet cells, retaining the existing German wording, including ae/oe/ue |
| Numbers, II, L1–L5 | Roboto Mono Medium | Main font atlases; numeric sprites identified by `Gfx_SetPartsNumber` are also replaced when drawn |
| Menu word cells | Exo 2 Medium | Restyles the 18 known word ranges; retains existing translations and `words.tsv` overrides |

The font binaries are embedded. No system installation, network download or
SDL_ttf dependency is required to run or build the program. Fonts apply in both
languages and with both interface styles, as requested. Existing Classic visual
reference hashes therefore describe the **previous font system**; they must not
be updated before reviewing the intentional image changes.

The game atlases retain the original 7×9 and 8×13 glyph dimensions. This is a
typeface replacement, not yet a vector-text renderer at window resolution.
The PC interface remains scalable. Large word pictures are rasterized directly
at their available texture size, with uniform scaling rather than horizontal
stretching. Old outline/bold dilation is removed; it closes the counters of the
new letters. Each cell uses a visible ink colour from its actual palette. A missing character in Orbitron uses Exo 2; unsupported characters
cause an explicit failure rather than silent character deletion.

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

The local game-data checks above are a first pass, not a complete visual audit.
- Audit all atlas cells and other textures for lettering outside the known
  mappings, including HP/MP labels rendered as pictures and the title logo,
  trademark/copyright lettering, intro/boot screens and any lettering in movies.
  **Those are not covered merely by replacing the two main atlases.**
- The development scene's separate 8×8 font has not been replaced.
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

Consult `tools/fonts/build_fonts.py` to reproduce the static instances with
fontTools. Normal builds consume the committed binaries directly.
