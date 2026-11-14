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
