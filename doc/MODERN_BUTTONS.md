# Modern UI with new buttons - PlayStation

Test version: **1.5.0-modern-ui-buttons-ps-test1**. Built on perf-test2, retaining its
presentation pacing, overload guard, diagnostics and report-script correction.

## Included

- New circular, coloured PlayStation badges for the four verified game-font prompts:
  Cross (blue), Circle (coral), Square (pink) and Triangle (turquoise).
- Both small and large text atlases, at native scale and rerasterized at HD output scale.
  Existing menu, dialog, save/load and other text-engine uses share the same route.
- Matching badges for all 16 existing inputs in F1 / Controls: the face buttons, directional
  inputs, L1/L2/R1/R2, Start/Select and L3/R3. Keyboard/gamepad binding labels remain truthful
  to the existing mapping. No input mappings are changed.

Select **Modern** under F1 / Grafik / Spielfenster (or the Start tab). The controls table
is under **Steuerung**. Existing Classic preferences are not forcibly changed.

The icon design is original procedural artwork inspired by the approved preview, not a
cropped image sheet or a downloaded PlayStation logo. The exact C rasterizer can export
a contact sheet with fonts_test --buttons-preview PATH.ppm.

## Preservation and implementation

New art lives in verified empty cells: small atlas 0xF0..0xF3, large atlas 0x80..0x83.
A separate, previously empty 16-colour bank is used in each existing font palette row.
The text engine selects that cell/bank only in Modern mode, preserving glyph advances,
button identity, draw order and clipping. Original icon cells and original CLUT banks
are untouched and remain selectable by Classic, including an immediate style switch.
Conflicting mod allocations are refused rather than overwritten. Palette brightness
variants are derived from the existing rows; HD tiles remain indexed, not a new stream
of per-frame RGBA uploads. Cached controls-table badges need no new GPU textures.

The neighbour glyphs L1..L5 in the game atlas are LEVEL glyphs, not shoulder-button
prompts, and are not remapped. No unsupported button hints or new key assignments are
invented in story text. Existing translations and font files are unchanged. Game data,
private text inventories and the reference artwork are not included in this package.

## Validation

- Linux release/no-disc build, font tests and textscript tests (90/90).
- Button checks cover 16 inputs, bounds, original glyph/palette isolation, repeatability,
  conflicting mods, indexed HD colours and Classic HD fallback.
- Six Classic image hashes match the existing references exactly in a local game-data run.
- The F1 Controls screen was rendered and visually checked locally.
- Sanitizer checks on the font/button tests; Windows/Linux Vulkan CI remains the build gate.

A synthetic renderer/atlas test does not verify every window in an entire playthrough.
Check in-game prompts, legibility at your scale and Modern/Classic switching on Windows.
The earlier GTX 1070 performance comparison remains a separate pending user test.

Test release naming comes from misc/release/test-build-name.txt; update that file for a
future differently named test build. The normal test-build tag is moving, so use the
internal version or commit to identify this test.
