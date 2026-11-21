# Mod-Schnittstelle 2 (für DW2/DW3 Viewer ab 0.16b)

Erweitert den bestehenden Datei-Mod-Lader (`Mods/files/<ID>.bin`, siehe `host/pak.c`). Die Änderungen
stammen aus `integration/pc_port_api2.patch` des Viewers (0.15b) und liegen jetzt hier im Quellcode.

## Was sich ändert
- Ersatzdateien dürfen größer sein als das Original (bis 2 MiB je Datei, 64 MiB gesamt). Größere
  Dateien bekommen einen virtuellen CD-Bereich hinter dem Ende des Pakets; `Cd_FileLba` und
  `Cd_FileSectors` zeigen für diese Datei dorthin. `dw2.pak`, `dw2.pak.slus` und deren Prüfsummen
  bleiben unverändert. Audio-/Videodateien sind weiter nicht ersetzbar.
- Der Spielheap liegt in einem eigenen 6-MiB-Bereich (`Dw2_ModHeap`) im selben 16-MiB-Fenster wie
  `Ps1_Ram`, statt im Rest der 2 MiB PS1-RAM. Die PS1-Adressspiegelung bleibt 2 MiB.
- `Cd_QueueFile` verdrängt bei vollem Datei-Cache (80 Einträge) den ältesten freien Eintrag, statt
  einen Nullzeiger zu benutzen; der größere Heap macht diesen Fall erst möglich.
- `Snd_ServiceSlotLoads` begrenzt die Kopie durch die Sektorgröße der Datei statt durch das Ende
  des PS1-RAM (die Puffer liegen jetzt im neuen Heap).

## Neue Kommandozeilenoptionen
- `--mod-api`: gibt `{"mod_api":2,...}` aus und prüft, dass Heap und Programm im 16-MiB-Fenster liegen.
- `--check-cache`: 200 Ersetzungen bei vollem Datei-Cache ohne Spieldaten.
- `--check-model MODELL ANIMATION`: lädt nach dem Start Modell und Animation über den Spielcode,
  120 Schritte `Anim_StepModelAnim`/`Gfx_CalcModelBoneMatrices`, prüft ein HD-Texturpaket.

## Geprüft (14.11.2026, Linux, eigene dw2.pak)
| Prüfung | gepatcht | unverändert 1.6.2 |
|---|---|---|
| Referenzbilder (tests/referenz_md5.txt) | 6/6 bitgleich | (Referenz) |
| save_test.py | 9/9 | – |
| cheat_test.py, domains_test.py mit frischer Karte | scheitern gleich (Karte ohne Gruppe/Domain-Zugang) | scheitern gleich |
| `--mod-api`, `--check-cache` | bestanden | – |
| Viewer-Mod Renamon (DW3) im Gazimon-Slot, erster Kampf der Boot Domain | geladen, gezeigt, Kampf beendet | – |

Nicht geprüft: Windows-Hardware, längere Spielstände, Domain- und Cheat-Test mit einer echten Karte.

## Mod Studio settings, Format 1 (`host/modconf.c`)

Two optional files next to `Mods/files`, read once at start (user folder, then the folder of
`dw2.exe`, which wins). Missing, broken or newer-format files change nothing; the game then
behaves exactly as without them. `--mod-api` reports `"digivolve_levels":1,"battle_camera":1`.

- `Mods/digivolve_levels.json`: `{"format":1,"levels":{"<id>":<level 1-99>}}`. In the Digi-Lab
  (`Stg20_LabInfo`) a Digimon with an entry may digivolve from that level on instead of the
  fixed band (Rookie 11, Champion 21, Ultimate 31). Mega stays locked.
- `Mods/battle_camera.json`: `{"format":1,"digimon":{"<id>":{"distance":<percent 25-400>,
  "height":<units>,"target":<units>,"angle":<4096 = 360 deg>,"closeup":{same keys}}}}`.
  The top-level keys change the attack and target shots (camera states 10-21) of that
  Digimon, `closeup` the command / target-select close-up (states 2-7). Missing keys keep
  the game's values.

Tests: `modconf_test` (made-up files, no game data).

### DNA overrides (Studio 0.99, interface `dna_overrides: 1`)

Optional UTF-8 `Mods/dna_overrides.json`:

```json
{"format":"dw2-dna-overrides","version":1,"rules":[{"a":1,"b":2,"result":3,"rank":3}]}
```

IDs must be present species 1–255 in the game's active DIGIMNDT table; the Studio
checks this before authoring/export. Partners must differ. A+B equals B+A. Invalid,
duplicate, contradictory or newer documents are ignored atomically. An executable-folder
file replaces the entire preference-folder DNA document. Empty rules clear all overrides.
Missing pairs use the original DNA table. `--no-mods` also disables DNA overrides.
Restart the game after installing/enabling/disabling a mod.

Rank is a species property: a result's rank (0 Rookie, 1 Champion, 2 Ultimate,
3 Mega) applies everywhere `Digi_GetRank` is used, including other instances of that
species. All rules with that result must agree. Removing the mod restores its original
rank. Start level is rank*10+1; inherited maximum level is raised to the start level if
necessary. Mega results use Ultimate's HP/MP/attack/defense/speed inheritance factors.
The lab's club/type restrictions still apply; explicitly configured Rookie partners are
allowed, including as the first partner when that species has a configured DNA pair.
This does not add a new model/species: import/replace a slot first to use e.g. Omnimon.
