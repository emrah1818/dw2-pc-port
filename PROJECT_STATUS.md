# PROJECT_STATUS – DW2 PC-Port

Stand: 06.11.2026, **Release 1.5.0** auf `main`. Was fertig, getestet und offen ist: TESTBERICHT.md, Abschnitt „1.5.0“, dazu GRAFIKTEXTE.md (Wortgrafiken) und CHEATS.md (Cheat-Menü).
Dieses Dokument ist der Einstieg für die Fortsetzung in einem neuen Projekt. Einzelheiten stehen in:

| Datei | Inhalt |
|---|---|
| [README_PORT.md](README_PORT.md) | Installation, Import, Bedienung, Sprache, Modding, Cheats, Änderungen gegenüber Upstream |
| [TESTBERICHT.md](TESTBERICHT.md) | Alle Testergebnisse je Version (1.1.0, 1.2.0, 1.3.0), Einschränkungen, Windows-Risiken |
| [doc/TESTLOG.md](doc/TESTLOG.md) | Frühe Testnotizen, Szenenmodi (0x301 Main Gate … 0x32D Stadtkarte) |
| [doc/PACK_FORMAT.md](doc/PACK_FORMAT.md), [doc/STATE_MAP.md](doc/STATE_MAP.md) | Upstream: Paketformat, Spielzustand |
| [README.md](README.md) | Upstream-Beschreibung (DW2-PC) |

## 1. Projekt in einem Satz

Native Windows/Linux-Fassung von Digimon World 2 (USA, SLUS-01193). Sie ist ein Fork von [Wyrelade/DW2-PC](https://github.com/Wyrelade/DW2-PC), Upstream-Commit `1dbb32a` (CC0, vollständige Dekompilierung, SDL3). Dazu kommen 4K, Profile, deutsche Sprach-Ebene, Modding, Cheats und automatische Tests. Spieldaten kommen ausschließlich aus dem eigenen Image des Nutzers.

## 2. Verbindliche Anforderungen und Entscheidungen

- **Spieldaten:** Keine geschützten Spieldaten im Repository oder Release. Das gilt auch für alles, was aus Spieltexten abgeleitet ist, etwa `texts.tsv`. Das Repository ist **öffentlich**.
- **Ehrliche Kennzeichnung:** Nichts als fertig oder getestet melden, was nicht geprüft ist. Unterschieden wird zwischen getestet, ungetestet, teilweise und fehlend.
- **Classic-Profil:** Es muss bildgenau dem Original entsprechen. Referenz ist `tests/referenz_md5.txt`, ermittelt mit `--profile classic` und ohne Einstellungsdatei.
- **Englischer Modus:** Er bleibt unverändert. Die Sprach-Ebene wirkt nur bei `language = de`.
- **Digimon-Typen (fest vorgegeben):** Vaccine → Serum, Data → Datei, Virus → Virus. Nur die Anzeige ändert sich, die IDs bleiben.
- **Attackennamen:** Sie kommen aus der Tabelle des Nutzers (`Languages/German/source/*.xlsx` → `tools/i18n/build_attacks.py` → `attacks.csv`). Die Regeln:
  1. Erste Anime-Bezeichnung (Trenner „ / “).
  2. Sonst die Bezeichnung aus einem anderen Spiel.
  3. Sonst bleibt der englische Name. **Nie selbst erfinden.**
- **Übrige Spieltexte:** Dialoge, Items und Menüs darf und soll Claude laut Auftrag 1.3.0 selbst ins Deutsche übersetzen. Gewünscht sind natürlicher Stil, konsistente Begriffe und keine Änderung von Inhalt oder Mechanik.
- **Mod-Ebene:** Originaldaten und Prüfsummen bleiben unverändert. Alle Ersetzungen laufen über die Mod-Ebene.
- **Cheats:** Bei jedem Start aus, vor Änderungen ein Backup, Rückgängig möglich, nur validierte Felder.
- **Arbeitsweise:** Der Nutzer will selbstständige Umsetzung ohne Rückfragen. Fragen sind nur bei echten Blockaden erlaubt, etwa fehlenden Dateien, Rechten oder Rechtsfragen.
- **Update-Prüfung:** Sie ist abgeschaltet (`DW2_PORT_NO_UPDATER`), weil Upstream-Releases den Fork ersetzen würden.

## 3. Architektur und wichtige Dateien

| Bereich | Datei(en) | Notiz |
|---|---|---|
| Disc-Tabellen zur Laufzeit | `host/discassets.c`, `host/pak.c` (`load_exe`), `host/pakbuild.c` | 13 INCLUDE_BIN-Tabellen aus `dw2.pak.slus` und den Overlays. Build mit `tools/build_native.py --no-disc` (eigenes Build-Verzeichnis `*_nodisc`) |
| Datenpaket | `host/pak.c` | `Host_PakSector` (Lesepfad des Spiels), `Host_PakReadFile` (Original), `Host_PakReadFileLayered` (wie das Spiel es sieht) |
| Mod-Ebene | `host/pak.c` (`load_mods`, `Host_PakSetLayer`) | `Mods/files/<ID>.bin`, `--no-mods`. Nur Form-1-Dateien, höchstens die Originalgröße |
| Sprach-Ebene DE | `host/lang.c` | Umlaut-Glyphen (Codes 0xE6–0xEC, aus A O U a o u B abgeleitet), Attacken (WAZADATA 0x25B), Typen (SYSTEXT 0x1FD), `texts.tsv` mit Neupacken der Textbereiche, `--export-texts`, `--lang-dump`, Bericht `Languages/German/report.txt` |
| Spieltabellen und Decoder | `host/devdata.c` | Digimon-, Item- und Skill-Tabellen, Textdecoder mit Wörterbuch (`0xF0 k`). Auch im Release über `DW2_CHEATS=1` |
| Cheats | `host/cheat.c`, `host/devedit.c` | Änderungs-Warteschlange (Spiel-Thread), `allowed` im Release standardmäßig 0, Backup und Rückgängig |
| Asset-Export und Textur-Import | `host/assets.c` | `--export-assets`, `--import-texture`. DW2-Modell-TIM: 64 Paletten in den unteren 32 Zeilen, benutzte Paletten aus den Polygonen (wie im Viewer) |
| PC-Menü F1 | `host/settingsui.cpp` | DE/EN-Tabelle `k_tr`, Reiter Start, Grafik, Steuerung, Spiel, Audio, Modding, Cheats |
| Einstellungen | `host/settings.c/.h` | u. a. `language`, `mute`, `fps_limit`, Profile |
| Grafik und Ton | `backend/`, `host/sdl.c`, `host/audio.c` | Auto-Skalierung, 16:9/21:9, Filter, 2D-Glättung, Smooth Frames (Upstream), FPS-Grenze |
| Test-Bot | `host/bot.c`, `tools/autoplay/` | `--bot IN OUT`. Befehle: `run`, `peek`, `poke`, `shot`, `dung`, `town`, `nav`, `devedit`, `cheatundo`. Skripte: `save_test.py`, `cheat_test.py`, `domains_test.py`, `story.py`, `navigate.py`, `town.py`, `policy.py` |
| Skript-Layout, Ereignistabellen | `host/textscript.c/.h` | Befehlslängen F0–FF, Ereignistabellen (`Flag_SelectBranch`), Bereichsprüfung, sicheres Neupacken. Ohne SDL, Test `tests/textscript_test.c` |
| Texturpakete | `backend/texrep.c/.h`, Haken in `backend/psxgpu.c` (Upload/Fill/Move) und `backend/psxgpu_hd.c` (`find_rep`, `rep_texel`) | Nur Software-HD-Pfad. Optionen `--dump-textures`, `--no-texture-packs`, Doku `doc/TEXTURPAKETE.md` |
| i18n-Werkzeuge | `tools/i18n/build_attacks.py`, `tools/i18n/build_texts.py` | `texts.tsv` mit Prüfung der Steuercodes |
| Windows-CI und Release | `.github/workflows/build.yml` | MSYS2 MinGW + SDL 3.4.8. Windows-Tests: hd_test Software/Vulkan (lavapipe über Registry), Import-Ablehnung. Release über `workflow_dispatch` mit Eingabe `release=vX.Y.Z` |
| Nutzer-Testskript | `tools/wintest/DW2-Test.ps1/.bat` | Liegt in der Zip. Echte Windows-Ergebnisse stehen noch aus |

## 4. Implementiert und getestet

Siehe TESTBERICHT.md. Kurzfassung:

- Import und Prüfung des Images (3675 Dateien)
- Programm ohne Spieldaten
- Classic bildgleich
- Speichertest 9/9, Cheattest 7/7
- 4K (9×)
- Windows-Renderer Software = Vulkan
- Audio-Regler gemessen
- Asset-Export und Textur-Mod im Spiel
- Deutsch:
  - 43 Attackennamen, Typen
  - Umlaute in beiden Spielschriften
  - 879 Tabellentexte (Kampf, Menüs, Items, Attackenbeschreibungen) im Spiel gesehen

## 5. Bekannte Fehler und Einschränkungen

- **Keine echten Windows-Hardwaretests:** Grafikkarte, Vollbild, Controller und Lautsprecher sind auf echter Hardware nicht geprüft. Unter Xvfb ohne Fenstermanager wird randloses Vollbild nicht angewendet.
- **Story nicht durchgespielt:** Der Bot endet am Anfang von Mission 2. Agumon L1 verliert gegen zwei L5-Gegner. Flucht funktioniert. Nötig wären Leveln und Taktik, Grundlage ist `policy.py`.
- **Textur-Ersatz:** nur in Originalgröße und mit Originalpalette, kein HD.
- **Item-Namen:** höchstens 10 Zeichen. Beschreibungen höchstens etwa 26 Zeichen (Breite des Textfensters).
- **Kein In-Game-Viewer, kein eigener Launcher, keine neu gestalteten Originalmenüs.** Die PC-Menüs sind ImGui (F1).
- **Bedienelemente nicht per Klick getestet:** F1-Fenstergrößen, FPS-Grenze und Cheat-Klicks.

## 6. Wichtige technische Erkenntnisse

- **Textkodierung:**

  | Code | Bedeutung |
  |---|---|
  | 0x00–0x09 | Ziffern |
  | 0x0A–0x23 | A–Z |
  | 0x24–0x3D | a–z |
  | 0x3E–0x5D | Satzzeichen (`host/devdata.c` `glyph()`) |
  | 0xFD | Leerzeichen |
  | 0xFE | Zeilenumbruch |
  | 0xFC | neue Seite |
  | 0xFB | warten |
  | 0xFF | Ende |
  | `0xF0 k` | Wörterbuch-Wort (k ≥ 6) bzw. Argument/Tamer/Beetle |
  | `0xF1`/`0xF2` + 3 Ziffern | Digimon-/Item-Name |
  | `0xEF k` | Glyphe 0xF0+k |

- **Argumentlängen der Steuercodes** (`src/main/text.c`):

  | Code | Argumente |
  |---|---|
  | F5 | keine |
  | F8, FA | 1 Byte |
  | F9 | k (k ungerade: 1 Byte, sonst 4) |
  | F6/F7 | 9 Bytes |
  | F4 | k4 < 0x20: 4 Bytes (k4 + 3 Ziffern), sonst 1 Byte (k4) – seit 1.4.0 modelliert |
  | F3 | 2 Bytes (Modus, Argument), beendet den Text – seit 1.4.0 modelliert |

- **Schriften:** Datei 0x13A (klein, 17 Spalten, Zelle 7×10, Glyphe 7×9) und 0x110 (groß, 13 Spalten, Zelle 9×14, Seite 2 ab 0x88). TIM bei Offset 4, 4 Bit, Bild ab 0x44. Frei in beiden: 0xE6–0xEE. Die Codes 0x5E–0xE4 im kleinen Font sind Wortgrafiken („Digi-Beetle“, „Counter Attack“, „Specialty“ …).
- **Tabellen** (Zeiger relativ zum Dateianfang):

  | Datei | Inhalt | Aufbau |
  |---|---|---|
  | ITEMDATA 0x45E | Items | Datensätze 0x10, Name +8, Beschreibung +12 |
  | WAZADATA 0x25B | Attacken | Datensätze 0x44, Name +0x24, Beschreibung +0x28 |
  | SYSTEXT 0x1FD | Systemtexte | Zeigerliste am Anfang (453 Einträge), Typen = Einträge 0xC3–0xC5 |

  Freier Platz am Ende ist klein (852 bzw. 1658 Byte). Deshalb wird der ganze Textbereich neu gepackt (`repack` in `host/lang.c`).
- **Stadt:** Raster 24×24 (`Stg20_MapGrid[x][y]`), Ausgänge aus der Kartendatei, Gespräche per Cross vor einer ruhenden Figur (30 Frames Sperre). Kartenmenüs verlässt man mit **Dreieck**. Die Domain-Karte erreicht man über ein Gespräch am Main Gate (11,10).
- **Kampf:** `Stg30_Battle` (`digis[6]` bei 0x18, `inputSlot` = 6 bedeutet Hauptmenü offen). Hauptmenü: Give Orders / Cannon / Run Away.
- **Bot:** Geplante devedit-Änderungen zählen in VBlank-*Waits*, nicht in VBlanks. Etwa 1200 Waits pro Sekunde im `--fast`-Modus.

## 7. Offene Aufgaben – Stand nach 1.5.0

**Stand 1.6.0-Entwicklung (7. November 2026, Commit nach 3dcdc46):**
- Erledigt und getestet:
  - Panel-Stil-Spitzen behoben: Stil einmal pro Panelstück, Kampf 4x Spitze 80 auf 13 ms, bitgleich; siehe doc/PERFORMANCE.md.
  - Digimon-Status (Modern): 3D-Vorschau größer und an ihr Fenster angepasst. Die Skalierung wird pro Digimon an acht Blickwinkeln gemessen, 1x bis 2,5x. Agumon 1,78x, Betamon 1,57x, Greymon 1,0x. Geprüft in 4:3, 16:9, Skalierung 2 und 9; Classic unverändert.
  - Tests: Referenzen Classic/EN 6/6, Speichern 9/9, Cheats 31/31, fonts/hd/textscript bestanden, CI Windows und Linux grün.
- Integriert (ChatGPT): perf-test1 und perf-test2 (Mailbox-Pacing, GPU-Wartezeit-Messung) sowie die PlayStation-Tastensymbole (9d2d651).
- Wichtig für Tests: Modern ist jetzt Standard. Die Referenzbilder gelten für Classic/EN, deshalb ref-Lauf mit `--settings-set ui_style=classic`.
- Nicht bestätigt: Wirkung von perf-test2 auf die GPU-Submit-Wartezeit (195 s von 582 s im GTX-1070-Log) auf dem Nvidia-PC des Nutzers. Das ist nicht als behoben anzusehen.
- Erledigt: Reiter Status/Techniken (Labels aus 01FD 0xD0/0xD1) und MP-Kosten je Zeile in der Attackenliste (96d8fcd).
- **Veröffentlicht: v1.6.0-preview** (Vorabversion, Commit d833f7e, interne Version 1.6.0-preview). Die fertige 1.6.0 kommt erst nach der Nvidia-Bestätigung des Nutzers.
- Offen, in dieser Reihenfolge:
  1. Digitation.
  2. Lexikon.
  3. Restliche Panel-Kanten.
  4. P3–P5.
  5. Auswertung der Leistungsmessung des Nutzers.

**1.5.0 erledigt:**
- Sprachwahl ohne F1: erster Start und Select am Titel.
- Deutsche Kampf- und Titelwortbilder sowie Attackennamen-Banner, siehe GRAFIKTEXTE.md.
- Breitbild-Kampf-HUD.
- Tempo-Schutz für Smooth Frames.
- PGXP-Messung.
- Cheat-Liste nach der SLUS-Sammlung, siehe CHEATS.md.

**Neu offen aus 1.5.0:**
- Breitbild für Gesprächsfenster mit Porträt, den Ergebnisbildschirm und das Feldmenü. Das braucht einen neuen Zeilenumbruch.
- Ein Bild „Critical“ finden.
- Zustandsmeldungen und Namensbanner im Lauf fotografieren.
- Sprachneustart in einem echten Fenster testen.
- GPU und vsync auf echter Hardware.

Ältere Liste:

Reihenfolge nach Priorität des Nutzers:

1. **Dialoge und Story auf Deutsch:** *erledigt in 1.4.0. 81 Ereignistabellen, 1682/1682 Dialoge, 2582 Texte, 0 Fehler, Wörterbuch, Umbruch 31 Spalten, Qualitätsprüfung und Layoutprüfung (`tools/i18n/layout_check.py`). Offen: vollständiger Story-Durchlauf im Bild, vollständige Durchsicht von Hand.*
2. **Wortgrafiken lokalisieren:** *erledigt in 1.4.0: die 18 Wortbild-Texte stehen deutsch als Text und werden vom PC-Layout eingepasst (ohne Abkürzungen), die Wortbilder werden zusätzlich neu gezeichnet. Offen: Kampfschriftzüge (Bilder der Spielgrafik).*
3. **Originalmenüs modernisieren:** *1.4.0: PC-Layout für alle Spielfenster (`host/uilayout.c`): Messen jeder Zeile, wachsende Fenster, ausweichende Spalten, Engsatz; Modi Modern/Klassisch. Offen: eigene, neu gestaltete Menüfenster.*
4. **3D-Studio im Programm:** Modell und Animation aus der Modelldatei, Logik wie im Viewer (`dw2-viewer-android`, `native_gltf.py`/`native_model.py`), Anzeige in ImGui oder über den GPU-Renderer.
5. **HD-Ersatztexturen** – *1.4.0: im Software-Renderer fertig, mit Testszene und mit echten Spieltexturen geprüft. Offen: GPU-Renderer (Shader müssten Ersatztexturen binden).*
6. **Eigenständiger Launcher** (heute F1-Reiter „Start“).
7. **Story-Bot:** Leveln (fliehen bei Überzahl, gezielt Einzelgegner), Heilung, Taktik.
8. **Regressionstests in DE und EN:** Speicher-, Domain- und Cheattest auch mit `--lang de`.

## 8. Genaue Anweisungen zur Fortsetzung

1. Repo klonen: `https://github.com/emrah1818/dw2-pc-port`.
2. **Daten:** Das USA-Image (`Digimon World 2 (USA).bin/.cue`) stellt der Nutzer bereit. Datenpaket bauen: `python3 -I tools/dw2pack.py <bin> -o dw2.pak`. Alternativ legt der erste Start von `dw2` es selbst an, zusammen mit `dw2.pak.slus`.
3. **Linux-Build:** SDL 3.4.8 bauen (siehe Workflow-Job `linux-x64`), dann `CMAKE_PREFIX_PATH=<sdl> python3 tools/build_native.py --release --no-disc` (Release) bzw. ohne `--release` (Dev-Build mit Warp/devedit für `domains_test.py`).
4. **Regressionen nach jeder Änderung:**
   - Referenzbilder: Aufruf wie in `tools/wintest/DW2-Test.ps1`, Abschnitt 4, mit leerem HOME/XDG_DATA_HOME, dann `md5sum -c tests/referenz_md5.txt`. Erwartet sind 6/6.
   - `tools/autoplay/save_test.py EXE dw2.pak WORK` (9/9)
   - `tools/autoplay/cheat_test.py EXE dw2.pak CARD WORK` (31/31 seit 1.5.0). CARD braucht ein Digimon im Team, z. B. den Spielstand nach dem Gold-Hawk-Beitritt aus `story.py` (`card_round0.mcd`).
   - `tools/autoplay/domains_test.py EXE dw2.pak WORK CARD --domains 0-5 --extra=--cheats`. Im Release-Build braucht der Warp `--cheats`.
5. **Deutsch testen:**
   - `Languages/German/` neben die EXE kopieren und `--lang de` setzen.
   - Ergebnis in `Languages/German/report.txt` prüfen.
   - Font-Dump mit `--lang-dump DIR`.
6. **Übersetzung:**
   - `dw2 --export-texts DIR` erzeugt `texts_en.tsv`.
   - Übersetzungen als `Schlüssel<Tab>Deutsch` schreiben.
   - `tools/i18n/build_texts.py texts_en.tsv translations.tsv Languages/German/texts.tsv` erzeugt die Datei.
   - **Nicht committen** (`.gitignore`). Die aktuelle `texts.tsv` mit 879 Einträgen hat der Nutzer erhalten. Für eine Fortsetzung muss er sie wieder bereitstellen, die Arbeitskopie liegt nicht im Repo.
7. **Release:**
   - `main` pushen.
   - Workflow „Build“ per `workflow_dispatch` mit `release=v1.5.0` starten (Tags lassen sich aus dieser Umgebung nicht direkt pushen, der Workflow legt sie an).
   - `DW2_VERSION` in `host/update.h` anheben.
8. **Commits:** Autor `emrah1818`, kurze deutsche Commit-Texte mit Aufzählung.

## 9. Versionen

| Version | Inhalt |
|---|---|
| 1.1.0 | Modding, Cheats, F1 DE/EN, Testbericht |
| 1.2.0 | Attackennamen DE, Typen, Umlaute |
| 1.3.0 | Textinventur, 879 Tabellentexte DE (Datei an den Nutzer), Neupacken |
| 1.4.0 | Story und Dialoge deutsch, Wortgrafiken als Text, PC-Layout der Spielfenster (Modern/Klassisch), Texturpakete (Software-Renderer), Qualitäts- und Layoutprüfung |
| 1.5.0 | Sprachwahl ohne F1, deutsche Wortbilder (Kampf, Titel, Attackennamen), Breitbild-Kampf-HUD, Tempo-Schutz, Cheat-Liste nach SLUS-Sammlung, PGXP-Messung |
| danach (geplant) | neu gestaltete Menüfenster, GPU-Texturpakete, Studio, Launcher (Abschnitt 7) |
