# DW2 PC-Port – Testbericht (Stand: nach 1.5.0, 06.11.2026)


## 1.6.2-dev (Branch `development/v1.6.2`, 13.11.2026) – noch nicht veröffentlicht

Geprüft unter Linux x64 mit dem verifizierten USA-Image des Projektinhabers (Import: alle 3675 Dateien passen). GPU-Renderer hier nur auf Mesa lavapipe (Software-Vulkan), nicht auf echter Grafikkarte.

**Regressionen gegenüber 1.6.0 (im Spiel, automatisch):**
- Referenzbilder Original/Englisch: 6/6 bitgleich, vor und nach den neuen Filtern.
- Speichern und Laden: 9/9 (inklusive vier beschädigter Karten).
- Cheats: 31/31.
- Domains: 33/33 geladen, 44 Kämpfe begonnen und beendet, kein Absturz, kein Hänger.
- `textscript_test` 90/90, `fonts_test` bestanden, `hd_test` 0 Fehler.

**Neu: Grafikprofile Original, Balanced, HD, Ultra**
- `tests/profile_test.py` (Windows- und Linux-CI): Werte aller vier Profile, alte Namen `classic` und `remastered`, Vorrang einzelner Optionen, unbekannter Name wird abgelehnt.
- Im Spiel gesehen: HD (Namenseingabe, 3× bei 720p-Fenster). Balanced und Ultra nicht im Bild angesehen.

**Neu: FPS-Anzeige** (aus, FPS, FPS und Details)
- Im Spiel gesehen (Bild ohne Fenster gerendert): Anzeige oben rechts mit Spiel- und Fensterrate, längstem Bild, Auflösung, Renderer, Version.
- Nicht geprüft: Anzeige in einem echten Fenster unter Windows; die Zahlen selbst gegen eine Messung.

**Neu: Kantenglättung (FXAA) und Farbregler (Helligkeit, Kontrast, Sättigung, Gamma)**
- `hd_test`, beide Renderer: FXAA ändert nur Kanten (5,5 % der Pixel der Testszene); Sättigung 0 ergibt ein graues Bild; Gamma hellt auf bzw. dunkelt ab; das 1×-Bild bleibt von allen Filtern unberührt; GPU und Software stimmen überein (0,05 bis 0,21 % der Pixel weichen um mehr als 24 ab).
- Im Spiel, Software-Renderer (Stadtkarte, 3D-Raum, 3×): reines Schärfen ist bitgleich mit 1.6.0; FXAA ändert etwa 44 000 von 920 000 Pixeln, die Farbregler das ganze Bild.
- Im Spiel, GPU-Renderer im Fenster (lavapipe, 3D-Raum): reines Schärfen gleich 1.6.0 bis auf die animierte Spielfigur (die weicht auch zwischen zwei Läufen derselben Version ab); FXAA und Farbregler wirken sichtbar.
- Nicht geprüft: echte Grafikkarte, Rechenzeit des Filterdurchgangs (GPU und Software), Zusammenspiel mit Smooth Frames auf echter Hardware.
- Bekannt: FXAA wirkt auf das ganze Bild und macht auch Schrift etwas weicher. Es ist deshalb in keinem Profil eingeschaltet.

**Noch nicht bearbeitet:** SMAA, MSAA, trilineare und anisotrope Filterung, 3D-Viewer, JPG-Texturen, Paketverwaltung, restliche Menüs, gelbe Pyramide.

## 1.6.0 (8.11.2026, freigegeben vom Projektinhaber)
- Stand wie 1.6.0-preview12, nur Version und Release-Art geändert.
- Breitbild-Dialogfenster (16:9, 21:9, Deutsch/Englisch, Hangar, Wache, Digi-Labor): Fenster über die ganze Breite, Text neu umbrochen, Namenszeile und JA/NEIN-Fragen unverändert.
- Referenzen 6/6, Speichern 9/9, Cheats 31/31, `fonts_test`, `hd_test` 0 Fehler, textscript 90/90.
- **Offen:** gelbe Pyramide bei ToyAgumon/ClearAgumon. Geprüft wurden 3 Kämpfe mit GPU-Renderer, Smooth und PGXP inklusive der Zwischenbilder: keine Pyramide, aber auch keiner dieser Gegner. Noch nicht gemessen: Englisch/Classic, ohne HD-Texturen, 6× und 9×.
## Leistung beim Nutzer mit 1.6.0-preview10 (8.11.2026)
- GTX 1070, 51 Minuten, Deutsch und Modern, 4× und 5×, 21:9: Median 16,7 ms. 99,9 % der Bilder kommen pünktlich; 168 Ruckler über 40 ms, meist bei der ersten Anzeige neuer Texte.
- GPU-Submit-Wartezeit 34 % → 3 % der Spielzeit.
- Details in doc/PERFORMANCE.md.

## 1.6.0-preview8 – Bugfix Screen Issue 69219 (7.11.2026)

| Test | Ergebnis |
|---|---|
| **Nutzer, Windows/Nvidia, 16:9:** Technik erlernen, ersetzen und übertragen | „passt, kein Problem“ (Rückmeldung 7.11., 22:54) |
| **Nutzer:** Namenseingabe | „funktioniert jetzt“ |
| Namenseingabe nach DNA-Digitation (Dev-Warp, PGXP, 16:9) | 501 → 0 verschobene Buchstaben |
| Kampf mit 1, 2 und 3 Gegnern (Domänen 6, 10, 14, 16:9) | Gegnerfenster links, Mitte und rechts auf einer Höhe, passend zu den Digimon-Fenstern |
| Referenzen, Speichern, Cheats, Schrift-, HD- und Textskript-Tests | 6/6, 9/9, 31/31, bestanden |

**Nutzer, 7.11. 23:01:** 21:9 im Technik-Menü ebenfalls bestätigt („funktioniert, sieht gut aus“). Der deutsche Text darf veröffentlicht werden; in 1.6.0-preview9 sind die Menü-, Item- und Technik-Texte öffentlich (`menu.tsv`), Story-Dialoge nicht.

## 1.6.0-preview7 (7.11.2026, Master-Prompt)

| Test | Ergebnis |
|---|---|
| Referenzen Classic/Englisch | 6/6 bitgleich |
| save_test / cheat_test | 9/9 / 31/31 |
| fonts_test, hd_test, textscript_test | bestanden / 0 Fehler / 90/90 |
| LV ohne und mit privater `texts.tsv`, Modern und Classic | Status und Tamer-Status zeigen LV |
| Lademenü | zeigt kein Digimon-Level |
| Digi-Labor (Dev-Warp 0x32F): Moduswahl, Liste, Werte mit DP, Techniken, DNA-Weg; 4:3, 16:9, 21:9 | neues Fensterdesign, Hintergrund unverändert, deutsche Meldungen |
| Kampf 3×, 16:9 gegen den vorigen Stand | 8 von 9 Bildern bitgleich (Cursor-Endlinie jetzt blau) |
| Kampf 5×, ganzer Kampf | Kachelspeicher 0-mal voll, max. 83 MiB; Stil 140 ms, Rastern 294 ms gesamt |
| F1 Cheats | Felder lesbar, Werte unter „Digimon-Details“ |
| F1 Lexikon (Screenshot) | Agumon: Serum, Rookie, Kleine Flamme MP 8, → Greymon |
| Porträtrahmen | glatt, Gesichtsfarben unverändert |

**Nicht getestet:**
- Echte Windows-/Nvidia-Hardware für diesen Stand.
- Kampf mit drei Gegnern.
- Eine tatsächlich ausgeführte Digitation oder DNA-Digitation. Bis zur Bestätigungsfrage ist es geprüft; die Spielregeln sind unverändert.

## 1.6.0-preview2 (7.11.2026, nach dem Test des Nutzers)

**Messung des Nutzers mit 1.6.0-preview** (GTX 1070, 1080p, 5× intern, GPU, Smooth + VSync, Mailbox):

| Wert | perf-test1 | 1.6.0-preview |
|---|---|---|
| Bildzeit (Median) | – | 16,7 ms (60 Hz) |
| Zeit in SDL_Submit | 195.058 ms in 582 s | 11.084 ms in 177 s |
| Spiel-Thread zu spät | – | 20 von 10.602 Wartepunkten |

Der Nutzer meldet: „läuft flüssig, auch bei den Kämpfen“. Die GPU-Submit-Wartezeit ist damit auf seinem PC weitgehend weg.

**Noch auffällig:** Kachelspeicher 255 MiB, 204 Mal voll (5×, Modern). Das ist noch zu untersuchen.

**Behoben und hier getestet:**

| Fehler | Test |
|---|---|
| Schrift zittert über bewegten Modellen (PGXP) | Kampf 3× mit PGXP: 663 → 0 verschobene Buchstaben; Ergebnisbild geändert, andere 22 von 24 Bildern bitgleich |
| Striche im Dialogfenster | Dialog Zudokorn 16:9 sauber; Menüs und Kampf mit Kanten |
| MetalGreymon/Greymon größer als das Fenster | Greymon 0,88×, im Fenster |
| Regressionen | Referenzen 6/6, Speichern 9/9, Cheats 31/31, Schrift-, HD- und Textskript-Tests bestanden |

**Nicht getestet:**
- MetalGreymon selbst (kein Spielstand damit hier).
- Der Gegner-Infokasten im Kampf (Meldung des Nutzers). Er ist noch nicht umgebaut.

## 1.6.0-preview: Modern-UI-Entwicklungsstand (7.11.2026)

Interne Version `1.6.0-preview`. Alles hier wurde auf dieser Maschine getestet (Linux, 2 Kerne, keine GPU; GPU-Pfad auf Mesa lavapipe), außerdem der Windows-Build in CI.

| Test | Ergebnis |
|---|---|
| Referenzbilder Classic/Englisch (`--settings-set ui_style=classic`; Modern ist Standard) | 6/6 bitgleich |
| save_test / cheat_test | 9/9 / 31/31 |
| fonts_test, hd_test, textscript_test | bestanden, 0 Fehler, 90/90 |
| Panel-Stil vor/nach (Kampf 4×, Menüs 4×/6×/9×) | 24 + 15 Bilder bitgleich; Stilzeit Kampf 634 → 116 ms, Spitze 80 → 13 ms |
| Digimon-Status Modern: Agumon, Betamon, Greymon; 4:3, 16:9; Faktor 2 und 9 | Modell im Fenster (1,78× / 1,57× / 1,0×); Drehen und Kippen funktionieren |
| Reiter Status/Techniken: hin, zurück, verlassen | wechseln, keine Textreste, Positionen bleiben stehen |
| Attackenliste: MP-Kosten pro Zeile (DE und EN) | „12 MP“ / „8 MP“ rechts in der Zeile |
| Classic Englisch Status-Menü | unverändert (kleines Modell, Original-Kopfzeile) |

**Nicht getestet:**
- Echte Windows-/Nvidia-Hardware, einschließlich der Wirkung von perf-test2 auf die GPU-Submit-Wartezeit.
- 21:9 im Status-Menü.
- Digimon mit langen Technik-Namen und vielen Techniken (hier nur Agumon und Greymon).

## Leistungsfehler „Zeitlupe bei Attacken“ (v1.5.0-preview, 7.11.2026)

Meldung: Beim Anzeigen deutscher Attackennamen und Kampfwörter bricht die Bildrate ein (Zeitlupe), CPU 5–7 %, Nvidia-GPU 18–30 %. Einzelheiten, Messspalten und Rohwerte: [doc/PERFORMANCE.md](doc/PERFORMANCE.md).

**Ursache (gemessen):** Die Laufzeitschrift-Kacheln waren fertig eingefärbt und wurden **pro Palettenzustand** neu erzeugt. Kampfbanner, Attackennamen und der pulsierende Titeltext wechseln ihre Palette laufend. Darum entstand jedes Mal eine neue Kachel in voller 4K-Größe (bis 4 MiB) auf dem Spiel-Thread und wurde neu zur GPU hochgeladen. Der GPU-Ersatzspeicher (128 MiB) lief ständig über und begann von vorn. Weil der Spiel-Thread auch die Spiellogik rechnet, lief das Spiel langsamer, obwohl CPU und GPU insgesamt wenig ausgelastet waren.

**Korrektur:**
1. Kacheln speichern Palettenindizes (1 statt 4 Byte pro Pixel), unabhängig von der Palette; die Farbe kommt beim Zeichnen aus der CLUT (Shader und Software-Renderer).
2. Vergrößerte Wörter werden mit einem Lesezugriff pro Pixel abgetastet statt mit 4–16.
3. Ein gemeinsamer GPU-Transferpuffer pro Durchgang.
4. Wortbild-Masken werden pro Größe zwischengespeichert und nur für die betroffenen Bilder neu gemacht.

| Messung (diese Maschine: 2 Kerne, GPU = Mesa lavapipe) | vorher | nachher |
|---|---|---|
| GPU 4K, 591 Bilder: Kacheln erzeugt | 378 (43,2 Mio. Pixel) | 30 (4,1 Mio.) |
| GPU 4K, 591 Bilder: Uploads | 2471 (1076 MiB, 630 ms) | 30 (4,0 MiB, 2,5 ms) |
| GPU 4K: Speicherüberläufe / mit Pixelschrift gezeichnet | 9 / 528 | 0 / 0 |
| Software 4K, ein Kampf: Kacheln | 925 (276 Mio. Pixel) | 225 (36 Mio.) |
| Software 4K, ein Kampf: Rastern + Umrechnen | 1,87 s | 0,20 s |
| Software 4K, ein Kampf: Kachelspeicher max. / voll / geleert | 254 MiB / 284 / 4 | 34 MiB / 0 / 0 |
| GPU 720p, ein Kampf: Kacheln / Uploads | 831 / 68 MiB | 225 / 2 MiB |

**Release:** [v1.5.0-preview2](https://github.com/emrah1818/dw2-pc-port/releases/tag/v1.5.0-preview2) (Commit c8a7019). CI grün: [Lauf 42369893274](https://github.com/emrah1818/dw2-pc-port/actions/runs/42369893274), Release-Lauf [42370685771](https://github.com/emrah1818/dw2-pc-port/actions/runs/42370685771).

**Offen:** Im Software-Renderer bleibt die Angriffsphase auf Deutsch/Modern etwa 20 ms pro Bild langsamer als Englisch/Classic. Das liegt nicht an den Kacheln und ist noch nicht eingegrenzt.

**Tests:**
- `hd_test` mit neuem Test „Palettenwechsel färbt dieselbe Kachel“: Software und GPU (lavapipe), 0 Fehler.
- `fonts_test`, textscript 90/90.
- Referenzbilder Classic 6/6 (bitgleich).
- Speichertest 9/9, Cheattest 31/31.

**Nicht geprüft:**
- **Kein Test auf echter Nvidia-Hardware** (hier keine GPU; lavapipe liefert exakte Zählwerte, aber keine aussagekräftigen Bildzeiten).
- Für die Messung beim Spieler: `DW2-Leistung.bat` im Programmordner schreibt `test_out\leistung.txt` und `test_out\leistung.csv`.

## Modern-UI-Masterauftrag (7.11.2026): Laufzeitschrift und erste Panel-Stufe

Commits: 4eb3429 (Laufzeitschrift), 1e68287 (Wortbilder in Ausgabeauflösung, Abtastung vergrößerter Vierecke), 5b0a84f und 54346ff (Panel-Stufe, noch abgeschaltet). Linux x64, echtes Image, privates deutsches Sprachpaket (nicht im Repository).

### Umgesetzt und geprüft

| Punkt | Umsetzung | Prüfung |
|---|---|---|
| Schrift in Ausgabeauflösung | Spieltext und alle gemalten Wortbilder werden zur Laufzeit aus den TTF-Umrissen in Renderer-Auflösung gerastert (k = Skalierung × Vergrößerung, höchstens 16) und durch die Spielpalette gefärbt. Beschreibung: [doc/FONTS.md](doc/FONTS.md), Abschnitt „Runtime rendering“. | im Spiel bei Skalierung 3, 6 und 9 (720p, 1440p, 2160p): Lade-Bildschirm, Ergebnisfenster, Kampfwörter mit Zoom, Attackennamen, Titel-Copyright und Titelcode. `fonts_test`, `hd_test` (Index-Quelle Software exakt; GPU in der CI auf lavapipe: 0 von 4096 Pixeln falsch, [Lauf 42310204488](https://github.com/emrah1818/dw2-pc-port/actions/runs/42310204488)). |
| Streifen bei vergrößertem Text (480-Zeilen-Bildschirme) | Der Software-Renderer nimmt für sprite-artige Vierecke mit Ersatz die Texturposition am Ausgabepixel. | Ergebnisfenster in 4K ohne Streifen |
| Ersatz ging verloren | Teil-Schreibzugriffe auf eine Font-Seite markieren nur diesen Teil; die Suche bricht an einem Upload ohne Ersatz nicht mehr ab (die Liste ist nicht in Upload-Reihenfolge). | Kampfwörter in 4K aus Umrissen (vorher blockig) |
| Kachelspeicher | höchstens 256 MiB und 1 Mio. Pixel je Kachel; bei vollem Speicher werden die Kacheln nach dem Bild verworfen und neu gemacht. | 4K-Kampf: dreimal neu begonnen, danach scharf |
| 1×-Bilder unverändert | Der Maskenaufbau ist geteilt (k = 1 gibt das bisherige Bild). | 76 von 76 gemalten Dateien bytegleich |
| Classic UI Englisch | keine Laufzeitschrift, keine Panels | Referenzbilder 6/6 |
| Font-Lizenzen | OFL ohne reservierte Namen; Einbettung mit Lizenz zulässig | [doc/FONTS.md](doc/FONTS.md) |

Regressionen: Commit 1e68287 Speichertest 9/9, Cheattest 31/31, CI grün ([Lauf 42318687718](https://github.com/emrah1818/dw2-pc-port/actions/runs/42318687718)); Commit 54346ff Referenzbilder 6/6, `fonts_test`, textscript 90/90, `hd_test` 0 Fehler, Speichertest 9/9, Cheattest 31/31, CI grün (Windows, Linux, Release-Job nur Test-Build: [Lauf 42321201198](https://github.com/emrah1818/dw2-pc-port/actions/runs/42321201198)).

### Umgesetzt, aber noch nicht freigegeben (abgeschaltet)

**Modern-UI-Panels** (`host/uipanels.c`, `backend/texrep.c style_panels`, nur mit `DW2_MODERN_PANELS=1` und Modern UI): eigener Panel-Stil nach Bild 3 in Ausgabeauflösung, mit Kontur aus einem Abstandsfeld, türkisem Rand und dunkelblauem Verlauf; alte graue Linien werden türkis, Text, Balken und Symbole bleiben. Gesehen im 4×-Kampf: Kampf-HUD, Befehlsleiste, EXP/BITs- und Gesprächsboxen. **Nicht freigegeben**, weil Bildschirme sonst gemischt aussehen: Die Ergebnisfelder pro Digimon werden nur teilweise erkannt (ihre Layouts liegen außerhalb von 0x25C). Die Erkennung nach Blatt reicht nicht; jeder Bildschirm braucht eine eigene Zuordnung seiner Stücke.

### Offen

- **Eigene Menü-Layouts nach Bild 1 und 2** (Status mit großer Modellvorschau und Tabs, Lexikon, Attacken, Digitation): nicht begonnen. Dafür müssen die Menüs selbst neu gezeichnet werden (Code der Spielmenüs, nicht nur Texturen); das ist größer als 1.6.0.
- Panel-Stil je Bildschirm vervollständigen und prüfen: Shops, Speicher/Laden, Feldmenü, Status, Digitation, Dialoge.
- Zahlen-Sprites (`host/fontnumbers.c`) und die mehrzelligen Wörter im Atlas (z. B. „EXP“) noch in 1×-Texeln.
- Laufzeitschrift ohne Teiltransparenz an den Rändern (die nachgebildete PS1-Pipeline hat kein Alpha je Texel).
- 1280×720, 1920×1080, 2560×1440, 3840×2160 im **Fenster** und 16:9/21:9 mit der Laufzeitschrift: geprüft wurden die internen Skalierungen 3, 4, 6 und 9 und 16:9/21:9 bei 4× (frühere Runde); die Fensterausgabe selbst nicht.
- Gezielte Szenen für Konter, Unterbrechen, die Ja/Nein-Frage und NIEDERLAGE sind weiterhin nicht ausgelöst.
- Ressourcen-Export (Abschnitt 6 des Auftrags): nicht begonnen.
- PGXP-Mischpolygone, 3D-Clipping: unverändert offen (siehe unten).

### Blockiert

- Windows mit echtem Image, echte GPU-Hardware, Controller, Vollbild: In dieser Umgebung gibt es weder Windows noch Wine noch eine echte Grafikkarte. Die CI prüft nur Build und synthetische Renderertests (lavapipe).

Release: **1.6.0 nicht veröffentlicht.** Die Modern-UI ist nicht fertig, und die Hardwaretests fehlen.

## Nach dem Zusammenführen von PR #1 (moderne Schrift), für 1.6.0

Ausgangspunkt: PR #1 (`codex/modern-fonts`, Exo 2 / Orbitron Bold / Roboto Mono) ist in `main` (Merge 6ee0f01). Getestet auf Linux x64 mit echtem Image und dem privaten deutschen Sprachpaket (nicht im Repository). Ergebnisse gelten für den genannten Commit; Ergebnisse früherer Commits sind als solche gekennzeichnet.

### Erledigt und geprüft

| Punkt | Änderung | Commit | Prüfung |
|---|---|---|---|
| Windows-Build | `host/lang.c` rief `Fonts_Enabled` ohne Deklaration auf (MinGW GCC 14: Fehler, Linux GCC 13 nur Warnung). Jetzt `#include "host/fonts.h"` statt lokaler `extern`-Zeilen; Host- und Backend-C baut überall mit `-Werror=implicit-function-declaration`, damit Linux denselben Fehler meldet. Decomp-Code behält seine Flags. | 120b19c | CI-Lauf [42300190175](https://github.com/emrah1818/dw2-pc-port/actions/runs/42300190175): windows-x64, linux-x64 und release (nur Test-Build) erfolgreich. Windows-Paket geprüft: keine png/tsv/bin/tim/pak/mcd, keine `texts.tsv`. |
| Classic-Schutz | Classic UI + Englisch malt keine modernen Schriften (`Fonts_SetEnabled`), `--profile classic` setzt auch `ui_style`. | 8102fff | Referenzbilder 6/6 bitgleich |
| Wortbilder dunkel | Der Maler aus PR #1 färbte mit dem dunklen Rand des Originals (KAMPF, Attacke, Hilfe fast schwarz). Jetzt eine Farbe je Bild: die Körperfarbe des Originals. Ein Zwischenstand mit Farbe je Zeile legte dunkle Querbänder über die Buchstaben („Verfehlt“ las sich „Verfehit“). | 8102fff, f1b3a5c | im Spiel, siehe unten |
| Unterbrechungsfrage | „Willst du unterbrechen?“ als eine Zeile über beide Zellen; vorher je Zelle ein Wort, „unterbrechen?“ viel kleiner. | f1b3a5c | gerenderte Textur |
| Titelhintergrund | Der laufende Programmcode hinter dem Titel (0x269) in Exo 2, 19 von 25 Zellen. | f1b3a5c | im Spiel, DE/EN, 4:3/16:9/21:9 |
| Absturz Entwicklermodus 0x101 | Ursache per gdb: `Stg00_LineupBuildList` sortiert 203 Modelle in Felder für 200 und überschreibt `count`, `scrollTop`, `layout` (203/202/201). Danach liest die Aufstellung ID 0, und `Digi_GetModelFile(0)` greift über NULL zu (`digidata.c:58` ← `digimodel.c:30` ← `lineup.c:52`). Das ist ein Fehler des Originalcodes; die PS1 liest dort harmlos Kernel-RAM, nativ ist es SIGSEGV. Mit dem Fontwechsel hat er nichts zu tun. Nativ behält die Liste die 200 kleinsten Sortierwerte. | 9b5fefd | vorher: 0x101 Segfault reproduziert, 0x104 lief. Nachher (Entwickler-Build): 0x101 3000 VBlanks mit Scrollen bis ans Listenende (`count` 200, `scrollTop` 191), 0x104 3000 VBlanks, beide Exit 0 |

### Wortbilder im laufenden Spiel (Commit f1b3a5c bzw. direkter Vorgänger)

`battle_words.py`, deutscher Testkampf mit umgeschriebenen Zügen (`--poke-turns 3:a0,2:82,4:b9`), Aufnahmen alle 6–12 Waits:

| Format | Gesehen, lesbar, nicht abgeschnitten, ohne Überlappung |
|---|---|
| 4:3 | KAMPF, ZIEL, LOS!, Hilfe, Attacke, Verfehlt, Abwehr, Schaden, SIEG, Kleine Flamme, Nadelfeuer, „wurde giftig“, Ergebnisfenster; verlorener Kampf: SPIEL VORBEI |
| 16:9 (HD) | ZIEL, LOS!, Hilfe, Attacke, Verfehlt, Nadelfeuer, Schaden, Befehlsfenster |
| 21:9 (HD) | LOS!, Abwehr, Attacke, Kleine Flamme, Schaden, SIEG, Ergebnisfenster |
| Englisch Modern UI 4:3 | TARGET, FIGHT, Assist, Attack, Missed, Guard, Damage, Fungus Cruncher, Needle Spray, „became poisonous“ (vor dem Farbfix waren Attack und BATTLE fast schwarz) |

**Nicht im Spiel ausgelöst:** Konter Attacke, Unterbrechen, die Unterbrechungsfrage mit Ja/Nein, NIEDERLAGE. Grund: Die Frage erscheint nur, wenn ein Gegner mit eigenem Zugtyp ≠ 2 handelt, während ein Spieler-Digimon Typ 3 hat (`skilleffect.c`). Das hat der Bot auch mit umgeschriebenen Zügen nicht erreicht. NIEDERLAGE gehört nicht zum normalen Game Over. Diese Bilder wurden aus den gemalten Dateien (`DW2_PICDUMP`) mit ihren Zellen gerendert und neben dem Original geprüft: Form, Größe und Abschneiden sind korrekt; die Farbe im Spiel ist dort nicht geprüft.

**Bewusst unverändert:** Markenlogo (Titel, Bootbild), die gedrehten Binärspalten im Titelhintergrund, die 6 schmalen Codefragmente unter 16 Texeln, ins Video eingebrannte Filmschrift, der eingemalte Code im Bootbild. Details: [GRAFIKTEXTE.md](GRAFIKTEXTE.md).

### Regressionstests

| Test | Commit 120b19c (frisch ausgeführt) | Commit f1b3a5c (frisch ausgeführt) |
|---|---|---|
| Referenzbilder Classic (Englisch) | 6/6 | 6/6 |
| `fonts_test` | bestanden | bestanden |
| `textscript_test` | 90/90 | 90/90 |
| `hd_test` | 0 Fehler, GPU-Teil übersprungen (hier kein Vulkan); in der CI auf lavapipe | 0 Fehler, GPU-Teil übersprungen |
| `save_test` | 9/9 | 9/9 |
| `cheat_test` (exe, pak, card, work) | 31/31 | 31/31 |
| `domains_test` | nicht verwertbar: mit dem Release-Build gestartet; der Test warpt über `--devedit`, das gibt es nur im Entwickler-Build (alle 33 Domänen „no dungeon after the warp“) | Entwickler-Build: 33/33 Domänen geladen, 0 Fehler, 36 Kämpfe begonnen und 36 beendet |

Frühere Ergebnisse (8102fff, vor dieser Runde): Referenzbilder 6/6, `fonts_test`, textscript 90/90, Speichertest 9/9, Cheattest 31/31.

### Weiter offen

- **Echte Windows-Hardware:** `dw2.exe` wurde nicht mit echten Spieldaten unter Windows gestartet; hier gibt es weder Windows noch Wine. Die Windows-CI baut und führt `hd_test` aus (Software-Vulkan lavapipe), das ist kein Test auf echter Grafikkarte. Benötigt: Start mit Image, Titel, Kampf, Speichern/Laden, F1-Fenster, Vollbild, Controller, GPU-Renderer mit Texturpaket, Audio-Aussetzer-Log.
- Konter, Unterbrechen, Unterbrechungsfrage und NIEDERLAGE im laufenden Spiel (nur als Textur geprüft).
- Englische Wortbilder in 16:9/21:9 nicht eigens aufgenommen (gleicher Code wie Deutsch).
- Shops, Speicher- und Statusfenster: in dieser Runde nicht erneut aufgenommen.
- PGXP-Mischpolygone (≈0,1 %), kein 3D-Clipping; Erholung der automatischen Auflösung und Audio auf echter Hardware.

## Entwicklung nach 1.5.0 (für 1.6.0)

Getestet wie in 1.5.0: Linux x64, Release-Build ohne Disc-Daten, echtes Image, Testbot und Bildschirmfotos (HD, 2×, 4:3, 16:9, 21:9).

### 1. Breitbild auf den übrigen Bildschirmen

**Hintergründe:** Kachelhintergründe gehen bis in die Seitenränder. Das gilt für:
- das Feldmenü (Gitter, Glasschichten)
- die Shops und die Digi-Beetle-Werkstatt (Platinenmuster)
- das DNA-Labor (Zahlenregen)

So funktioniert es:
- Gemeint ist ein Bild, das die ganze 4:3-Breite als Raster gleich breiter Spalten aus wenigen Kacheln bedeckt.
- Es wird gespiegelt um die Randspalte fortgesetzt. Schachbrett- und versetzte Muster laufen dadurch unverändert weiter.
- Ein Bild aus vielen verschiedenen Teilen wird nie wiederholt. Das betrifft Titel, Karten und Illustrationen.

**Gespräche:** Das Porträt rückt in den Seitenrand seiner Seite, Rahmen und Gesicht zusammen (`src/main/portrait.c`). Das Textfenster bleibt mittig.

**Ergebnisbildschirm, Menüs, Shops, Digi-Beetle, Digitation:** Die Fenster bleiben mittig und vollständig lesbar.

Das alles gilt nur mit Modern UI im Breitbild. Classic und 4:3 sind unverändert: Referenzbilder 6/6.

**Im Spiel getestet, 16:9 und 21:9:**
- Feldmenü mit allen Unterseiten
- DNA-Labor mit Digitation
- Beetle-Werkstatt (Montage, Teileliste)
- Teile-Shop
- Gespräche in zwei Szenen
- Kampf

**Grenze:** Räume, deren 3D-Geometrie an der Bildkante endet, zeigen dort weiter Schwarz. Dort wird nichts erfunden.

### 2. Deutsche Grafiktexte im laufenden Spiel

Der neue Testbot `tools/autoplay/battle_words.py` macht Testkämpfe mit Fotos alle 12 Waits:
- Sieg mit Levelaufstieg
- verlorener Kampf
- Befehlslisten

**Neu im Spiel gesehen:** Schaden, LEVELAUFSTIEG, LOS!, Flucht, Abwehr, SPIEL VORBEI, die Banner „Kleine Flamme“, „Megafeuer“ und „Magisches Feuer“. Dazu wie bisher Attacke, Verfehlt, Sieg, Kampf und Ziel. Die Liste steht in [GRAFIKTEXTE.md](GRAFIKTEXTE.md).

**Gefunden und behoben:**
- LEVELAUFSTIEG lag über „Nächstes Level“ und dem Kastenrand. Es steht jetzt rechtsbündig im Wertefeld.
- Die Zeile vor einem Wertefeld endet jetzt davor (`host/uilayout.c`: ein Balken im Fenster zählt als Zeilenende).
- Lange Attackennamen verschmierten zu einer „Geisterschrift“. Klarere Buchstaben m, w, W, a, g, i, M.
- Kleine Wörter (Unterbrechen, Ja, Nein, „unterbrechen?“) stehen jetzt in schmaler Schrift.

**Nicht im Spiel ausgelöst:** Konter, Unterbrechen, Hilfe, die Unterbrechungsfrage, NIEDERLAGE und Zustandsmeldungen. Diese Szenen hängen von Gegneraktionen ab. Geprüft sind sie als Textur.

### 3. HD-Texturen

**PNG-Laden:** PNGs werden in einem eigenen Thread geladen. Bis ein Bild fertig ist, zeichnet das Spiel die Originaltextur, ohne zu warten. Vorher stand das Spiel beim ersten Gebrauch großer Pakete still.

**Mip-Stufen:** Für 2/4/8/16× entstehen halbierte Stufen, gemittelt mit Alpha. Jedes Pixel liest die Stufe, die zur internen Auflösung passt. Andere Faktoren werden blockweise gemittelt. Das verhindert Flimmern und Aliasing bei kleinerer Auflösung.

**Getestet:** 32 echte Titeltexturen, 16× vergrößert, bis 2048×4096 Pixel:

| Prüfung | Ergebnis |
|---|---|
| Laden | alle 32 geladen, je 5 Stufen |
| Bild bei 2× gegenüber dem Original | 0 von 614.400 Pixeln abweichend |
| Spielgeschwindigkeit mit Paket | 962 Spielbilder in 32 s |
| Spielgeschwindigkeit ohne Paket | 906 Spielbilder in 30 s |
| Hänger | keine. Vorher, ohne Stufen und Lade-Thread: 18 Uhr-Neustarts und 1267 Spielbilder in 118 s (ohne Paket sind es etwa 30 pro Sekunde) |

Neuer Selbsttest in `hd_test`: 16× Schachbrett, bei 4× grau gemittelt, bei 16× exakt: 0 von 32 abweichend.

**Grenze:** Der GPU-Renderer zeichnet weiter die Originaltexturen. Hier gibt es kein Vulkan, also ist das nicht testbar.

### 4. Spieltempo, Bildtakt, Ton

- **Unterläufe:** Leere Tonpuffer werden jetzt gezählt und protokolliert (`[audio] queue ran dry`).
- **Auto-Auflösung:** Ist das Spiel in den ersten 15 Sekunden zu langsam, geht sie sofort eine Stufe herunter, statt erst beim zweiten Mal.
- **Messung** (Fenster 2560×1440 offscreen, Software-Renderer, 2 Kerne): Leere Tonpuffer in den ersten 10 s gingen von 11 auf 1 zurück. Das Spiel lief durchgehend mit 59,94 Hz, Bild und Ton bleiben am selben VBlank-Takt.
- **Nicht geprüft:** vsync, GPU-Last und Bildtakt auf echter Hardware.

### 5. Grafikstabilität (PGXP)

- **Messung:** In Domain 1 wurden die 0,1 % gemischten Polygone protokolliert (`DW2_PGXP_STATS=1`). Gemischt heißt: einige Ecken präzise, andere ganzzahlig.
- **Ergebnis:** Alle protokollierten Fälle sind große Boden- und Wandflächen direkt vor der Kamera, deren Ecken am Bildrand liegen (±512, gesättigt). Eine Ecke liegt hinter der Nahebene, dafür gibt es keinen genauen Bildpunkt.
- **Warum nicht behoben:** Abhilfe wäre ein Zuschnitt in 3D vor der Projektion. Die Zeichenbefehle enthalten zu diesem Zeitpunkt nur noch 2D-Punkte. Ein solcher Eingriff in die Spielgeometrie wurde nicht begonnen.
- **Folge:** An diesen Flächen kann am Bildrand eine leichte Verschiebung der Textur bleiben. Die Modelle der Digimon sind nicht betroffen.

### Regressionstests

| Test | Ergebnis |
|---|---|
| Englische Referenzbilder (Classic) | 6/6 bitgleich, nach allen Layout-Änderungen |
| `hd_test` | bestanden (GPU übersprungen) |
| Menü-Rundgang Deutsch 4:3 | Fotos geprüft, keine Änderung außer dem Ergebnis-Wertefeld |

### HD-Texturen im GPU-Renderer (neu in 1.6.0)

- **Umsetzung:** `backend/psxgpu_hw.c` (Ersatzpixel in einem 128-MiB-Storage-Buffer, Bump-Speicher, Upload im Kopierdurchgang des Bildes, höchstens 32 MiB je Bild, Neustart bei vollem Speicher oder Skalierungswechsel), `backend/shaders/hd.frag` / `hd.vert` (`rep_sample`, Beschreibung je Eckpunkt), `backend/texrep.c` (`TexRep_GpuLevel`). Die Shader wurden mit einem selbst gebauten glslang neu übersetzt (`tools/build_shaders.py`; der Lauf mit dem alten Quelltext ergab dieselbe Datei wie die eingecheckte).
- **Getestet (Automatik):** `hd_test` in der CI, Linux und Windows, auf Mesa lavapipe (Software-Vulkan, keine echte Grafikkarte). Ergebnis: GPU = Software mit Texturpaket (0,08 % der Pixel weichen um mehr als 24 ab), die 3D-Fläche und die vergrößerten Sprite-Vierecke nutzen die Ersatztextur, Sprites texelgenau (0 von 12288 Pixeln falsch), falsche Größe und kaputte Datei bleiben Original. Der erste CI-Lauf zeigte 3,83 % Abweichung; Ursache war ein bekannter Unterschied bei vergrößerten Sprite-Vierecken (siehe `doc/TEXTURPAKETE.md`), kein Fehler der GPU-Ausgabe; der Vergleich lässt diese Fläche aus und prüft sie getrennt.
- **Hier nicht möglich:** Auf der Entwicklungsmaschine gibt es kein Vulkan, der GPU-Teil von `hd_test` läuft dort nicht.
- **Nicht getestet (echte Hardware nötig):** echte Grafikkarten, Tempo und Grafikspeicher mit 16×-Paketen, echter Spielablauf mit vollem Pool, Zusammenspiel mit den weichen Zwischenbildern beim Skalierungswechsel, Treiber mit weniger als 128 MiB je Storage-Buffer.

### Technische Prüfung vor der Veröffentlichung (Commit fc11194 und Folgecommit)

Unterschieden wird: **bestätigter Fehler** (am Code nachgewiesen und behoben), **Risiko** (nicht aufgetreten, aber möglich), **nicht getestet**.

| Bereich | Befund | Art | Stand |
|---|---|---|---|
| texrep.c | Der Loader-Thread wurde beim Beenden nie beendet (er wartet für immer auf die Warteschlange). | bestätigt (Code) | behoben: `TexRep_Shutdown` setzt ein Ende-Flag, weckt den Thread, wartet auf ihn; aufgerufen in `Host_Shutdown`. Test `TexRep_SelfTestLoader` (Hintergrundladen, Ergebnis, Ende, Anfrage danach abgewiesen) in `hd_test`: bestanden. |
| texrep.c | `mem_used` und die Statistikzähler wurden vom Loader-Thread und (Tests) vom Spiel-Thread ohne Synchronisation geändert. | bestätigt (Code) | behoben: atomare Zähler. |
| texrep.c | Die 1-GiB-Grenze prüfte nur die Hauptstufe, die Mip-Stufen (etwa ein Drittel mehr) kamen darüber hinaus. | bestätigt (Code) | behoben: die Prüfung rechnet 4/3. |
| texrep.c | Die Warteschlange (4096) ist begrenzt. Ist sie voll, wird die Textur beim nächsten Zugriff erneut angefragt, bis Platz ist; bis dahin gilt das Original. Fertige Bilder bleiben bis zum Programmende im Speicher (höchstens 1 GiB). Wird ein Pfad bei einem späteren `TexRep_AddRoot` ersetzt, während der Loader liest, wäre das ein Wettlauf; `AddRoot` läuft im Betrieb nur beim Start. | Risiko | unverändert, dokumentiert |
| audio.c | Die Zählung „Warteschlange leer“ misst keine hörbaren Aussetzer: das Gerät hat einen eigenen Puffer, und ein langer Hänger wurde bei jedem Aufruf mitgezählt. | bestätigt (Code) | behoben: der Rückruf des Audiogeräts (`additional_amount > 0` heißt: das Gerät muss Stille spielen) zählt hörbare Aussetzer mit Dauer; „Warteschlange leer“ bleibt als Warnung. Das Log schreibt alle 10 s: `last 10 s: N audible gaps (X ms of silence), queue empty M times`. **Nicht an echter Hardware geprüft** (hier nur der Dummy-Treiber). |
| sdl.c | Die automatische Auflösung stieg nach einem Rückgang nie wieder (`perf_cap` blieb bis zum Programmende). Ein einmaliger Hänger beim Laden senkte sie dauerhaft. | bestätigt (Code) | behoben: nach 60 s ohne späte Bilder eine Stufe höher (das Fenster begrenzt weiterhin); hält die Stufe nicht (spät innerhalb 20 s), verdoppelt sich die Wartezeit bis 16 min. Rückgang je Stufe nur bei wiederholter Verspätung oder in den ersten 15 s, nie unter 2×. **Die Erholung wurde nicht im Lauf geprüft** (die Testmaschine ist dauerhaft zu langsam; es gibt keinen Zustand „lange ruhig“). |
| psxgpu.c | `DW2_PGXP_STATS` gab die Zusammenfassung nur alle 262 144 Polygone aus; kurze Läufe zeigten nichts. | bestätigt | behoben: Ausgabe auch beim Programmende. |
| PGXP | Weiter bestehende Geometriefehler: Etwa 0,1 % der 3D-Polygone haben Ecken mit und ohne genaue Position (große Boden-/Wandvierecke am Bildrand, Eckpunkt hinter der Nahebene, Befehle 0x28/0x2E). Sie werden mit ganzzahligen Ecken gezeichnet. Echtes 3D-Clipping ist nicht umgesetzt; die Befehle enthalten an dieser Stelle nur 2D-Punkte. | bestätigt, offen | dokumentiert |
| Windows-Build | Sauberer Lauf der CI auf dem GitHub-Stand (Commit nach fc11194): Build, `hd_test` (mit Software- und GPU-Pfad auf Mesa lavapipe, synthetische Szene) und Import-Prüfungen ohne Pack / falsches Bild: bestanden. Das Paket enthält `dw2.exe`, `SDL3.dll`, Prüfprogramm, Dokumente, `attacks.csv`, `types.csv` und die Quell-xlsx, keine png/tsv/bin/tim/pak/mcd. Importe der `dw2.exe`: KERNEL32, msvcrt, SHELL32, USER32, WINHTTP, SDL3 (nur Systembibliotheken und die mitgelieferte SDL3.dll). | bestätigt | **nicht getestet:** Start von `dw2.exe` mit echten Spieldaten unter Windows (hier kein Windows, kein Wine) |

Regression nach den Änderungen (Linux, Release-Build): Cheat-Test 31/31, Speichertest 9/9, Referenzbilder Classic 6/6, textscript 90/90, `hd_test` 0 Fehler (GPU-Teil hier übersprungen, in der CI auf lavapipe ausgeführt).

## 1.5.0 – Breitbild-Kampf, Sprachwahl ohne F1, deutsche Wortgrafiken, Cheat-Liste, Spieltempo

Wie getestet wurde: wie in 1.4.0. Linux x64 als Release-Build ohne Disc-Daten, mit dem echten Image des Spielers. Dazu der Testbot und Bildschirmfotos im laufenden Spiel.
- HD-Fotos: 2× Auflösung, 4:3, 16:9 und 21:9, Software-Renderer.
- GPU-Renderer und vsync gibt es auf dem Testrechner nicht, weil Vulkan fehlt.
- Windows: Build und automatische Tests in der CI.

### 1. Sprachwahl ohne F1

**Umgesetzt** (`host/ui.cpp`, `host/main.c`):
- **Erster Start:** Das Fenster „Select Language / Sprache wählen“ erscheint mit den Knöpfen English und Deutsch.
- **Titelbild:** Kurz **Select** drücken öffnet dieselbe Auswahl, mit Controller oder Tastatur. Der Hinweis am Titel lautet „Select: Sprache / Language F1: Einstellungen“.
- **Im laufenden Spiel:** F1 → Start → Sprache, wie bisher.

Die Wahl wird in der Einstellungsdatei gespeichert, unabhängig vom Spielstand, und gilt auch für F1 und das Cheat-Menü.
- **Am Titel:** Das Programm startet nach dem Wechsel sofort neu, weil die Spieltexte beim Start eingelesen werden.
- **Im Spiel:** Erst kommt die Frage „Später / Jetzt neu starten“, damit nichts Ungespeichertes verloren geht.

**Getestet:**
- Das Auswahlfenster am Titel als Bild (`--language-picker --ui-shots`).
- Speichern der Einstellung.
- **Nicht geprüft:** der Neustart in einem echten Fenster. Der Testrechner hat kein Fenster, der Neustart startet einen neuen Prozess.

**Nicht umgesetzt:** ein eigener Eintrag „Sprache“ im Feldmenü des Spiels. Dieses Menü ist ein festes Bild der Spieldaten mit fest verdrahteten Zeilen. Stattdessen gibt es Select am Titel und F1.

### 2. Deutsche Wortgrafiken

Das vollständige Inventar mit Ort, Form und Test steht in [GRAFIKTEXTE.md](GRAFIKTEXTE.md).

- 25 von 25 Bildern und 43 von 43 Attackennamen-Bildern sind gemalt, ohne Fehler.
- 68 Zustandsmeldungen haben deutsche Buchstabenlisten.

**Im Spiel geprüft:**
- STARTTASTE DRÜCKEN, NEUES SPIEL, FORTSETZEN, KAMPFMODUS
- KAMPF, ZIEL, SIEG, „Attacke Verfehlt“

**Als Textur geprüft:**
- LEVELAUFSTIEG, NIEDERLAGE, LOS!, Flucht/Keine, SPIEL VORBEI
- Schaden, WÄHLE, Konter, Unterbrechen, Hilfe, Abwehr
- „Willst du unterbrechen?“, Ja/Nein

**Behoben:**
- „KAMPF“ wurde als „KAAPF“ gelesen, „NIEDERLAGE“ als „HIEDERLAGE“.
- S und G sahen aus wie 8 und 0.
- LEVELAUFSTIEG war auf 64 Pixel zusammengedrückt. Es liegt jetzt in einer freien Fläche derselben Textur mit 124 Pixeln.
- NEUES SPIEL war zugelaufen.

**Offen:**
- Ein Bild „Critical“ wurde nicht gefunden.
- 213 Attacken ohne geprüften deutschen Namen bleiben englisch, wie vorgegeben.
- Zustandsmeldungen und Namensbanner traten im Testlauf nicht auf.

### 3. Breitbild-Kampf (Modern UI, 16:9 und 21:9)

**Umgesetzt** (`host/uilayout.c`, `src/main/parts.c`, `src/main/text.c`):

**Was sich bewegt:**
- Befehlsmenüs und die vier Attackenlisten rücken in die Seitenränder, ebenso die drei Team-Statusfenster und das Aktionsschild über dem Fenster. Ein Fenster links der Mitte geht nach links, eines rechts der Mitte nach rechts.
- Alles darin wandert mit: Texte, Ziffern, Cursor und Markierung.

**Was bleibt, wo es ist:**
- In der Mitte: der Name des Gegners und die Beschreibungszeile.
- Was zur 3D-Szene gehört: Zielmarke, Schadenszahlen und Wortbanner.

Die Bewegung gilt nur mit Modern UI und im Breitbild. Mit Classic UI oder in 4:3 ändert sich nichts.

**Im Spiel getestet** in zwei Kämpfen je Format (16:9 und 21:9):
- Die Mitte bleibt für die Digimon frei.
- Texte stehen in ihren Fenstern, keine Überlappung, nichts abgeschnitten.
- Die Zielmarke steht weiter richtig.

**Nicht umgesetzt:**
- Gesprächsfenster mit Porträt, Ergebnisbildschirm (EXP, Level, BITs), Feldmenü und Sonderbildschirme bleiben in der Bildmitte in 4:3-Größe. Ihre Texte sind an die Fensterbreite gebunden. Ein breiteres Fenster bräuchte einen neuen Zeilenumbruch aller Dialoge und wurde nicht begonnen.
- In 16:9 und 21:9 sind diese Fenster vollständig lesbar und nicht abgeschnitten: Sie stehen mittig über der breiten 3D-Szene.

### 4. Spieltempo unabhängig von der Bildrate

**Ursache für 1.3.0** (Analyse):
- Die Spieluhr läuft fest mit 59,94 Hz (`host/vblank.c`). Das Spiel zählt seine Bilder daran.
- Das Fenster zeichnet die Zwischenbilder der „Smooth Frames“ in einem eigenen Thread, mit denselben HD-Arbeits-Threads wie das Spiel.
- Ohne wirksames vsync waren das ohne Obergrenze etwa 250 Bilder pro Sekunde, ebenso auf 144/240-Hz-Bildschirmen. Diese Bilder nahmen dem Spiel-Thread Rechenzeit, das Spiel lief langsamer. Mit FPS = 60 war genug Zeit übrig.

**Umgesetzt:**
- Ohne wirksames vsync werden Zwischenbilder höchstens mit der Bildwiederholrate des Bildschirms gezeichnet, oder mit 60, wenn sie unbekannt ist.
- **Tempo-Schutz:** Verpasst der Spiel-Thread mehrmals pro Sekunde seine VBlank-Termine, pausieren die Zwischenbilder für 3 Sekunden. Es werden dann nur die echten Spielbilder gezeigt.
- **Automatische Auflösung** mit dem Software-Renderer: Hilft die Pause nicht, sinkt die interne Auflösung um eine Stufe, höchstens bis 2×. Die Einstellung „Auto“ bleibt erhalten, eine fest gewählte Auflösung wird nicht verändert.

**Getestet** (Software-Renderer, Fenster 2560×1440 offscreen, 2 CPU-Kerne):

| Einstellung | Ergebnis |
|---|---|
| Auto | 6× → 3×, danach volles Tempo: 147 Spielbilder je 10 s wie bei niedriger Auflösung, 59,94 Hz |
| fest 12× | Schutz greift (Zwischenbilder pausiert). Das Spiel bleibt langsamer, weil das Zeichnen selbst zu langsam ist: 84 statt 147 Spielbilder je 10 s |
| 3×, FPS 0 und 60 | gleich: 59,94 Hz, 147 Spielbilder je 10 s |

**Nicht geprüft:** GPU-Renderer mit echtem vsync. Die Schutzlogik ist für beide Renderer dieselbe.

### 5. Wackelnde 3D-Modelle und Texturen

Untersucht mit dem neuen Zähler `DW2_PGXP_STATS=1`, Domains 0–5 mit 12 Kämpfen.

**PGXP-Abdeckung:** Ist PGXP an (Profil Remastered), bekommen **99,9 %** der 3D-Polygone präzise Eckpunkte, 0,1 % teilweise, 0,0 % keine.

**Texturen:** Beide Renderer zeichnen die Texturen dann perspektivisch korrekt, aus 1/z (`psxgpu_hd.c`, `psxgpu_hw.c`).

**Bewertung:** Die Videos aus 1.3.0 (Emblem-Plattform, Stadt, Statusbildschirm) zeigen das typische PS1-Bild:
- Ganzzahlige Eckpunkte, die springen.
- Affine Texturen, die schwimmen.

Beides ist ein Effekt des Originals. Er bleibt im Profil Classic absichtlich erhalten, denn dort ist PGXP aus. Im Profil Remastered ist er durch PGXP und Perspektivkorrektur behoben. Ob bei der Aufnahme PGXP an war, lässt sich aus den Videos nicht erkennen. Ein Port-Fehler, der präzise Eckpunkte verliert, zeigte sich in der Messung nicht.

**Was bleibt, weil das Original es so macht:**
- Gelenke animierter Modelle in ganzzahligen Schritten
- die Polygon-Reihenfolge der PS1 (Ordnungstabelle)

**Texturen:**
- Remastered filtert 3D-Texturen bilinear.
- Es werden keine Details erfunden: keine KI-Hochskalierung.
- Bessere Böden gibt es über Texturpakete (siehe 1.4.0).

### 6. Cheat-Menü nach der Code-Sammlung

Einzelheiten stehen in [CHEATS.md](CHEATS.md).

**Neu:**
- Gehaltene Werte: Unendlich Bits, HP/MP des Teams, auch im Kampf, HP/EP des Beetles.
- Max-Knöpfe.
- Art, Level-Grenze und 12 Attacken je Digimon.
- 28 Story-Items mit Warnung.
- Taschenplätze.
- Bits bis 99.999.999. Werte begrenzt auf 999, die Grenze des Spiels.

**Nicht übernommen:** Item Slot 49–64 der Sammlung. Die Tasche hat 48 Plätze, die Adressen dahinter beschädigen den Spielstand.

**Test:** `cheat_test.py` **31/31**. Gehaltene Werte im Kampf geprüft. Menü auf Deutsch als Bild geprüft.

### Regressionstests 1.5.0 (Linux, echtes Image)

| Test | Ergebnis |
|---|---|
| Englische Referenzbilder (`ui_style=classic`) | 6/6 bitgleich |
| Speichertest | 9/9 |
| Cheattest | 31/31 |
| Domaintest, Domains 0–5 (`--extra=--cheats`, PGXP, 2×) | 6/6 geladen, 12/12 Kämpfe beendet, kein Absturz, zurück in die Stadt |
| Kampf Deutsch 4:3, 16:9, 21:9; Englisch 16:9 | je 2 Kämpfe, Fotos geprüft |
| `textscript_test` | 90/90 |
| `hd_test` | bestanden, GPU-Teil übersprungen (kein Vulkan) |
| CI Windows (MinGW) und Linux | siehe Release-Lauf |

**Nicht geprüft:**
- Windows-Hardware
- GPU-Renderer
- Neustart nach dem Sprachwechsel in einem echten Fenster
- Ergebnisbildschirm mit Levelaufstieg im Lauf
- Zustandsmeldungen im Lauf

## 1.4.0 – deutsche Story, PC-Layout der Spielfenster, Texturpakete

Wie getestet wurde: Linux x64 (Release-Build ohne Disc-Daten) mit dem echten Image des Spielers, Testbot über die Spielmenüs (`tools/autoplay/`), Bildschirmfotos im laufenden Spiel. Windows: Build und Renderer-Test in der CI, kein Klicktest auf echter Hardware. Die Übersetzungsdateien liegen nicht im Repository (aus der Disc abgeleitet), der Spieler bekommt sie getrennt.

Begriffe: **umgesetzt** = Code fertig; **automatisch getestet** = Testprogramm/Prüfskript; **im Spiel getestet** = im laufenden Spiel ausgeführt und auf Bildschirmfotos geprüft; **nicht geprüft** = steht dabei.

### 1. Benutzeroberfläche: kein Text über seinem Fenster

**Umgesetzt** (`host/uilayout.c`, `src/main/text.c`, `src/main/parts.c`), zentral für alle Fenster und Texte:

- **Textmessung:** Jede Zeile wird vor dem Zeichnen gemessen, so wie der Spielrenderer sie läuft: Namen ({TAMER}, Digimon, Items), Wörterbuchwörter und Argumente inklusive. Die Glyphenbreiten werden beim Start aus beiden Spielschriften gelesen (nach der deutschen Ebene).
- **Fenster:** Jedes gezeichnete Fensterbild („parts“) wird je Bild als Rechteck notiert, auch mehrere Fenster in einem Bild (z. B. Reiter + Verbinder + Slotleiste im Speicherbildschirm).
- **Einpassen, in dieser Reihenfolge:**
  1. Das Fenster wächst nach rechts, so weit Bildschirm und Nachbarfenster es erlauben. Gezeichnet wird das, indem die mittlere Kachel wiederholt und der rechte Rand verschoben wird, samt Schatten.
  2. Eine Textspalte rechts daneben rückt zur Seite, wenn sie im Fenster Platz hat (z. B. Werte neben „Spezialität“).
  3. Erst dann wird die Zeile eng gesetzt (Buchstaben nur nach ihrer Tinte, wie die Wortbilder des Spiels), und nur wenn nötig zusätzlich etwas schmaler (höchstens 30 %).
- **Tabellen** (Shoplisten, Namenseingabe, Spalten mit Leerzeichen) behalten ihre PS1-Spalten.
- **Zwei Modi** (F1 → Start → Spielfenster, Einstellung `ui_style`, Profile Classic/Remastered):
  - *Modern* (Standard): Buchstaben nach eigener Breite.
  - *Klassisch*: feste PS1-Zeichenzellen.

  Das Einpassen gilt in beiden. Englische Texte passen in die Originalfenster. Klassisch + Englisch ist deshalb bitgleich zum PS1-Bild (6/6 Referenzbilder).

**Behobene Fehler:**

- „Speichern“ ragte im Feldmenü und im Speicherreiter über den Rahmen. Es steht wieder voll da: Ersatz „Sichern“ zurückgenommen.
- Optionen nebeneinander liefen ineinander, z. B. „Kaufen|Verkaufen“ und „Digi-Beetle|Wichtig“.
- Preise in Shoplisten verrutschten mit proportionaler Schrift. Gelöst über Tabellen-Erkennung.
- Absturz im Statusbildschirm bei einer Gruppe mit Lücke (bearbeitete Spielstände): `status.c`, PC-Schutz.

**Im Spiel getestet (Bildschirmfotos, Deutsch, Modern und Klassisch):**

- Feldmenü
- Status mit Reitern Zustand/Digi-Beetle/Wichtig
- Digimon-Status mit „Spezialität“ und „Nächstes Level“
- Items (Benutzen/Wegwerfen, Zielauswahl)
- Digi-Line und Transfer
- Laden und Speichern (Reiter, Slots, Meldungen)
- DNA-Labor: Digitieren/DNA-Digitation, Auswahl, Ja/Nein, Digitationsablauf
- Teile-Shop: Kaufen/Verkaufen, Liste mit Preisen, Beschreibung, Stück
- Digi-Beetle-Werkstatt: Montage/Verbessern, Karosserie, Motor, Batterie, Speicher, Teileliste
- Kampf: Befehle geben/Kanone/Fliehen, Kampf/Abwehr, Angriff/Techniken, Gegenangriff, Hilfe
- Namenseingabe, Titel
- Gespräche in Digital City (30 Szenen, 113 Fotos)
- Neues Spiel → Intro → Boot Domain mit 2 Kämpfen → Stadt (74 Fotos)

Kein Text lag außerhalb seines Fensters. Im Modus Klassisch sind sehr lange Begriffe in kleinen Fenstern eng gesetzt, aber lesbar.

**Nicht geprüft:**

- Optionsmenüs, die nur später im Spiel erscheinen
- Kolosseum-Auswahl, Zwei-Spieler-Kampf
- Server-Lager
- Breitbild 16:9/21:9 und höhere Auflösungen mit dem neuen Layout. Das Layout rechnet in Spielkoordinaten und ist von der Ausgabeauflösung unabhängig.

### 2. Deutsche Übersetzung

**Eingespielt:** `texts.tsv: 2582 texts German, 0 failed`. Das sind 879 Tabellentexte aus der Spielertabelle, 1682 Dialoge (81 Ereignistabellen, 5 Fehltreffer abgelehnt) und 21 eigene Systemzeilen. Dazu kommt das Wörterbuch mit 183 Wörtern.

- Dialogumbruch: 31 Spalten, 0 abgelehnt.
- Skriptbefehle F3/F4/F6/F7, Textzeiger und Ereignistabellen: wie in 1.4.0-dev geprüft (`textscript_test` 90/90, Neupacken mit 0 Fehlern).

**Qualitätsprüfung (alle 2582 Einträge, automatisch):**

| Prüfung | Ergebnis |
|---|---|
| Steuerbefehle und Platzhalter identisch zum Englischen | 2582/2582 (sonst nicht eingespielt) |
| Glyphen im Font | alle |
| Leerzeichen neben Befehlen verloren (z. B. „Okay.Alles“) | 10 Dialoge gefunden und behoben (Ursache im Abschnitts-Werkzeug, behoben) |
| englische Wörter übrig | 0 (11 Treffer waren deutsche Wörter wie „will“, „her“) |
| unverändert englisch | 1 (Palmon-Laute, gewollt) |
| Glossar (Serum/Datei, digitieren, Digi-Beetle, Domain, Wachteam, Kommandant, Archivschiff, BITs) | einheitlich, keine Abweichungen gefunden |
| unnötige Abkürzungen | 27 Tabellentexte ausgeschrieben (22× „Batt.:“ → „Batterie:“, „Zust.“, „eig.“, „Ring v. Chaos-Gen.“, 2× „Nächst. Lv“ → „Nächstes Level“), dazu in den eigenen Systemzeilen „Spezial.“ → „Spezialität“, „DNA-Digit.“ → „DNA-Digitation“, „alle Tech“ → „alle Techniken“ |
| einzelne Restwörter auf eigener Fensterseite | 8 (kurze vollständige Sätze) |

- **Gelesen:** Eine Stichprobe von 22 Dialogen wurde Satz für Satz gegen das Englische gelesen. Gefunden wurde nur der Leerzeichenfehler oben.
- **Nicht geprüft:** Eine vollständige sprachliche Durchsicht aller 2582 Texte von Hand hat **nicht** stattgefunden.
- **Nicht übersetzt (339 Inventurtexte):** Namen und Kürzel, die im Deutschen gleich lauten (Itemnamen wie „Wolf EG-1“, „HP“, „Rookie“, „Status“), sowie Platzhalter des Spiels. Ungeklärte Bedeutungen: keine gefunden.

**Layoutprüfung** (`tools/i18n/layout_check.py`: echte Glyphenbreiten aus der Spielschrift, Namen mit Höchstbreite):

- 2300 Texte passen wie gesetzt.
- 273 sind länger als die englische Zeile und werden vom PC-Layout eingepasst.
- 9 Menüwörter passen nur, weil ihr Fenster wächst: Speichern, Benutzen (3×), Karosserie (2×), Kaufen, Verkaufen, Geschäft. 7 davon sind auf den Fotos geprüft. Benutzen im Server-Menü und Karosserie im Shop-Menü sind nicht fotografiert.
- Keine Dialogzeile über 31 Spalten.

### 3. Wortgrafiken und Menübeschriftungen

- Die 18 Menütexte, die das Spiel als Bild im Font hat, stehen deutsch als normaler Text in `texts.tsv`. Das PC-Layout passt sie ein, deshalb jetzt **ohne Abkürzung**: „Spezialität“, „DNA-Digitation“, „Gegenangriff“, „Zustand“, „Kampf“, „Kanone“, „Wählen“, „Montage“, „Autopilot“, „Eltern“, „Wichtig“.
- Im Spiel geprüft sind Gegenangriff, Zustand, Wichtig, Kampf, Spezialität, DNA-Digitation, Digitieren, Montage und Speichern.
- Die Neuzeichnung der Wortbilder aus 1.4.0-dev bleibt für Texte, die die Bilder weiter nutzen (`patch_words`).
- **Englisch bleiben:** die großen Kampfschriftzüge „BATTLE“, „FIGHT“, „Attack“, „Damage“, „WIN“, „TARGET“ und die Attackennamen im Effekt. Das sind Bilder der Spielgrafik, kein Text. Ersetzen ginge nur über ein Texturpaket. Abgeleitete Grafiken dürfen aber nicht veröffentlicht werden, deshalb ist das nicht Teil des Ports.

### Phase 3 – HD-Texturpakete

**Umgesetzt** (`backend/texrep.c`, Anleitung `doc/TEXTURPAKETE.md`):

- Stabile Textur-ID je VRAM-Upload (FNV-1a 64 über Größe und Pixel), bei 4/8 Bit wahlweise je Palette.
- PNG laden, Faktor 1–16× geprüft, Transparenz über Alpha, Halbtransparenz vom Original-Texel.
- Fehler protokolliert (`Mods/Textures/textures_log.txt`), Original als Rückfall, Speichergrenze 1 GiB.
- `--dump-textures`, `--no-texture-packs`, Pakete als Ordner, Anzeige in F1 → Modding.
- Abtastung pro HD-Pixel innerhalb des Original-Texels. Dadurch keine UV-Verschiebung und kein Ausbluten in Nachbartexturen.

**Getestet** (`tests/hd_test.c`, erzeugte Szene, Linux und Windows-CI):

- Dumpnamen für 4-, 8- und 16-Bit
- 4×-Ersatz auf Sprites texelgenau: 0 von 12288 Pixeln abweichend, auch bei Skalierung 2
- 3D-Polygon nutzt den Ersatz
- defekte PNG und falsche Größe werden ignoriert und gemeldet
- 1×-Bild bleibt bitgleich
- Software = GPU-Vergleich weiter bestanden

**Grenzen:**

- Nur der Software-Renderer zeichnet Ersatztexturen.
- Nur Polygone ganz innerhalb eines Uploads, ohne Texturfenster.
- Spätere VRAM-Schreibzugriffe über einem Upload verwerfen den Ersatz.
- Ausgabe in 15 Bit.
- **GPU-Renderer:** Ersatztexturen bräuchten dort einen eigenen Texturatlas je Paket und eine zweite Abtastung im Shader. Das ist für 1.4.0 nicht umgesetzt: Mit GPU-Renderer zeigt das Spiel die Originaltexturen. Hier gibt es kein Vulkan-Gerät, also auch keinen Test.

**Mit echten Spieltexturen getestet (1.4.0):**

- 32 Texturen des Titelbilds mit `--dump-textures` ausgegeben, invertiert, 4× vergrößert und als Paket eingelegt.
- Alle 32 wurden geladen („512x1024 (4x)“), das Titelbild wurde mit den Ersatztexturen gezeichnet, die Transparenz am Logo blieb erhalten.
- Ohne Paket ist das Bild unverändert.

### Regressionstests 1.4.0 (Linux, echtes Image)

| Test | Ergebnis |
|---|---|
| Englische Referenzbilder (`ui_style=classic`) | 6/6 bitgleich |
| Kampf: Beschreibungszeile der Techniken und Items | auf den Fotos nach 12 Zeichen stehend („Schießt eine“), im Englischen genauso („Shoots a Fir“): Verhalten des Spiels bzw. des Testbots, keine Folge der Übersetzung |
| Speichertest (Speichern über Menü, Laden, 4 beschädigte Karten) | 9/9 |
| Cheattest (mit Spielstand mit Digimon in Platz 0, `card_party`) | 7/7, die 2 Level-Prüfungen aus 1.4.0-dev sind damit bestanden |
| Domaintest Deutsch, Domains 0–7 | 8/8 geladen, 7 Kämpfe beendet, kein Absturz |
| Story-Lauf Deutsch: neues Spiel → Intro → Boot Domain → Stadt | erreicht, 2 Kämpfe |
| `textscript_test` | 90/90 |
| `hd_test` (Renderer, Texturpakete) | alle bestanden, GPU-Teil übersprungen (kein Vulkan hier) |
| `gte_test` | 2 Fehler im Build ohne Disc: die Tabelle `ratan_tbl` ist dort ein Platzhalter, das Spiel lädt sie zur Laufzeit aus `dw2.pak`. Kein Fehler des Spiels, mit Disc-Build nicht wiederholt |
| CI Windows (MinGW) und Linux | siehe Release-Lauf |

**Nicht geprüft:**

- Kein vollständiger Story-Durchlauf. Geprüft sind der Anfang bis zur Stadt, die Stadtgespräche, die Domains 0–7 und die Menüs.
- Windows nur Build und automatische Tests in der CI, kein Klicktest auf echter Hardware.

## Neu in 1.3.0: Textinventur und deutsche Tabellentexte

### Textinventur

`dw2.exe --export-texts DIR` schreibt `texts_en.tsv` mit Schlüssel `DATEI:ZEIGER`, Kategorie und englischem Text. Steuercodes bleiben als Platzhalter erhalten (`{NL}`, `{PAGE}`, `{ARG1}`, `{B:..}`).

Erfasst sind **1201 Texte**:

| Kategorie | Anzahl |
|---|---|
| System, Kampf und Menüs (SYSTEXT) | 417 |
| Itemnamen | 264 |
| Itembeschreibungen | 264 |
| Attackenbeschreibungen | 256 |

Alle sind ohne Skriptbefehle und damit sicher übersetzbar.

### Übersetzt und im Spiel getestet

- **Umfang:** Übersetzt sind 879 dieser Texte, darunter alle Kampf-, Menü-, Fallen-, Bug-, Shop-, Speicherkarten-, Digitations- und Statustexte, die Itembeschreibungen, alle 189 verschiedenen Attackenbeschreibungen und die Itemnamen, für die es natürliche deutsche Namen mit höchstens 10 Zeichen gibt. Technische Teilenamen wie „Wolf EG-1“ oder „Ant RAM“ bleiben englisch.
- **Integration:** Die Texte werden über `Languages/German/texts.tsv` eingespielt. Die Textbereiche werden dafür neu gepackt. Ergebnis: 879 von 879 eingesetzt, 0 Fehler. Jeder Text hat dieselben Steuercodes in derselben Reihenfolge wie das Original, das wird beim Bauen geprüft.
- **Im Spiel gesehen:**
  - Kampfmenü: „Befehle geben / Kanone / Fliehen“, „Angriff / Unterbrechen / Hilfe“, Beschreibung „Schießt einen Feuerball“
  - Statusfenster: „Typ Serum / Stufe Rookie / Keins / Angriff / Abwehr / Tempo / Nächst. Lv“
  - Hauptmenü: „Speichern“
- **Gefundener Fehler:** „Nächstes Level“ wurde abgeschnitten und ist jetzt gekürzt.
- **Englischer Modus:** Die 6 Referenzbilder sind unverändert.

### Warum `texts.tsv` nicht im Repository liegt

Die Übersetzung beruht auf den Texten des Spiels. Das Repository ist öffentlich, deshalb wird `texts.tsv` nicht veröffentlicht (`.gitignore`). Sie wurde dir direkt übergeben. Lege sie nach `Languages/German/` neben `dw2.exe`.

### Noch offen (nicht umgesetzt)

- **Dialoge und Story:** Sie stehen in Ereignisskripten mit den Befehlen F3, F4, F6 und F7. Deren Argumentlänge hängt von Unterbefehlen ab (`src/main/text.c`). Erst wenn sie exakt modelliert sind, können diese Texte sicher ersetzt werden. Inventur und Übersetzung der Dialoge fehlen daher noch.
- **Wortgrafiken:** Begriffe wie „Specialty“, „Counter Attack“, „Battle“, „Digimon“ oder „Items“ sind fertig gezeichnete Wörter in der Schrift-Textur. Lösungsweg: Die Wörter aus den Spielglyphen neu zeichnen, so wie die Umlaute. Noch nicht gebaut.
- **Weiteres:**
  - Originalmenüs neu gestalten
  - 3D-Studio im Spiel
  - HD-Ersatztexturen
  - Eigener Launcher
  - Story-Bot über Mission 2 hinaus

  Alles unverändert offen, siehe 1.2.0.
- **Nicht wiederholt:** Speicher- und Domaintests liefen für 1.3.0 nicht erneut. Geändert wurde nur die Sprach-Ebene, im englischen Modus ist der Weg unverändert.


## Neu in 1.2.0: deutsche Spieltexte (Stand und Grenzen)

### Getestet (Linux, dein Image)

- **Attackennamen:** `Languages/German/attacks.csv` aus deiner Tabelle hat 182 Einträge. Nach den Regeln (erste Anime-Bezeichnung, sonst anderes Spiel, sonst Englisch) haben 43 einen deutschen Namen. Alle 43 wurden in der Attacken-Tabelle des Spiels (256 Attacken) über den englischen Namen gefunden und ersetzt. 213 Attacken bleiben englisch, 0 Fehler. Die Liste je Attacke steht in `Languages/German/report.txt`.
- **Im Kampf geprüft:** Im Technikmenü steht „Kleine Flamme“ statt „Pepper Breath“, mit einer Testzeile für Umlaute.
- **Typen:** Im Digimon-Statusfenster des Spiels steht „Type Serum“ bei Agumon. Data wird zu Datei, Virus bleibt Virus.
- **Umlaute:** Ä Ö Ü ä ö ü ß werden in beiden Spielschriften (klein und groß) aus deiner Schrift erzeugt. Die Zielzellen wurden vorher als leer geprüft. Die Glyphen sind pixelweise kontrolliert und im Kampfmenü dargestellt.
- **Englischer Modus:** keine Sprach-Ebene, die 6 Referenzbilder sind unverändert.

### Nicht umgesetzt

Dafür gibt es keine belegten Texte, und eine eigene Übersetzung war für Attacken ausdrücklich ausgeschlossen. Für die übrigen Texte ist sie in diesem Umfang nicht geleistet.

- Dialoge und Story
- Itemnamen und -beschreibungen, Attackenbeschreibungen
- Kampfmeldungen
- Menüwörter des Spiels, die als fertige Wortgrafiken in der Schrift liegen, etwa „Digi-Beetle“, „Counter Attack“ oder „Type“
- Städte- und Domainnamen

Die Technik für weitere Texte steht bereit: deutsche Texte in die Spieldateien schreiben (an Ort und Stelle oder in den freien Bereich) und Umlaut-Glyphen. Eine vollständige Text-Inventur aller Spieltextdateien gibt es noch nicht.

### Weiter offen aus der 1.2.0-Aufgabe

- **Originalmenüs neu gestalten:** nicht umgesetzt. Die Spielmenüs bleiben im Original, im Remastered-Profil geglättet hochskaliert.
- **3D-Studio im Spiel:** nicht umgesetzt. Modelle gibt es weiterhin über den Export und den DW2 3D Viewer.
- **Hochauflösende Ersatztexturen:** nicht umgesetzt. Der Renderer zeichnet PS1-VRAM-Texturen. HD-Ersatz bräuchte eine eigene Texturersetzung im Renderer.
- **Eigenes Launcher-Programm:** nicht umgesetzt. Die Startzentrale ist der F1-Reiter „Start“.
- **Story-Bot über Mission 2 hinaus:** nicht weiterentwickelt.

Alle Tests aus 1.1.0 gelten weiter (gleiche Codepfade im englischen Modus). Für 1.2.0 erneut ausgeführt: Referenzbilder (6/6 gleich) und Cheattest (7/7). Der Speichertest wurde für 1.2.0 nicht erneut ausgeführt.


Alle Ergebnisse unten stammen aus tatsächlich ausgeführten Tests. Wo nichts getestet werden konnte,
steht das ausdrücklich dabei.

## Testumgebungen

| Umgebung | Was dort lief |
|---|---|
| Linux x64 (Cloud, gcc 13, SDL 3.4.8, Xvfb ohne Fenstermanager, Software-Renderer) | alle Spieltests mit einem verifizierten USA-Image |
| Windows Server (GitHub Actions, MinGW, SDL 3.4.8, Mesa lavapipe = Software-Vulkan) | Build, Renderer-Test Software und Vulkan, Import-Ablehnung. Ohne Spieldaten: das Image darf nicht hochgeladen werden |
| Dein Windows-PC | **noch nicht getestet**. Dafür gibt es `DW2-Test.bat` |

## 1. Implementiert und erfolgreich getestet

| Bereich | Test | Ergebnis |
|---|---|---|
| Keine Spieldaten im Programm | 109 Stichproben aus den Disc-Tabellen, 135 aus den STAG-Overlays, im Binary gesucht | 0 Treffer |
| Import des eigenen Images | `dw2.pak` aus dem Image, alle 3675 Dateien gegen SHA-256 geprüft | bestanden |
| Import-Fehlerfälle | fehlendes Paket, falsches Image (Linux und Windows-CI) | Exit 1, verständliche Meldung, kein Paket geschrieben |
| Classic-Profil = Original | Bilder bei Boot, VBlank 300 und 700 gegen den Upstream-Build mit eingebauten Disc-Daten | MD5-gleich |
| Reproduzierbarkeit | Titel → Neues Spiel → Intro → Namenseingabe, 6 Bilder (`tests/referenz_md5.txt`) | mehrfach identisch, auch nach allen Änderungen |
| Speichern/Laden | `tools/autoplay/save_test.py`: Speichern über das Spielmenü, neuer Start, Laden, Vergleich des Spielstands | 9/9 bestanden, Spielstand nach dem Laden byte-gleich |
| Beschädigte Speicherstände | Zufallsbytes, abgeschnitten, leer, verfälschter Block | kein Absturz, Spiel bleibt bedienbar |
| Spielablauf (vom Bot gespielt) | Neues Spiel, Intro, Boot Domain mit Kämpfen und Etagen, zurück in die Stadt; alle 7 Stadtziele mit Räumen und Gesprächen; Gold-Hawk-Beitritt (Agumon); Domain-Karte; Kampf, Flucht, Game Over | ohne Absturz |
| Domains (Sprungtest) | Domains 11–32 einzeln geladen, Etagen gelaufen, Kämpfe | alle geladen, kein Absturz |
| 4K | Fenster 3840×2160, Remastered-Profil | 3D automatisch 9× (2158 Zeilen), UI-Skala 2,4 |
| Renderer Windows | `hd_test.exe` auf Windows: Software gegen Vulkan (lavapipe), Texturfilter, 2D-Glättung | alle bestanden (≤ 0,08 % Pixel abweichend) |
| Audio-Regler | Ausgabe mit SDL-Datei-Audiotreiber gemessen (RMS je Sekunde) | „Ton aus“ → 0; Musik 0 entfernt die Titelmusik; Effekte 0 lässt sie unverändert |
| Asset-Export | `--export-assets` | 3641 Dateien mit Index, 1452 TIM-Texturen als PNG, 194 Digimon-Ordner mit Daten aus der Spieltabelle, Modell und den von seinen Polygonen benutzten Paletten |
| Textur-Rundlauf | exportiertes PNG unverändert zurück importieren | 0 Pixel geändert, Datei byte-gleich |
| Textur-Mod im Spiel | bemalte Agumon-Textur über `Mods/files`, Kampfbild mit und ohne `--no-mods` | Unterschied nur im Bereich von Agumon |
| Cheat-Backend | `tools/autoplay/cheat_test.py` | 7/7 bestanden, siehe unten |
| Deutsche PC-Menüs | Bilder aller Reiter mit `--lang de` | Umlaute und ß korrekt, Hinweise umbrechen, kein abgeschnittener Text |

Die 7 Cheat-Prüfungen im Einzelnen:

1. Mit ausgeschalteten Cheats wird jede Änderung abgelehnt.
2. Beim Einschalten werden die Memory Cards gesichert, als identische Kopie.
3. Bits lassen sich setzen.
4. Das Level eines Digimon lässt sich setzen.
5. Level 250 liegt außerhalb des Bereichs und wird abgelehnt.
6. „Rückgängig“ stellt den Spielstand byte-genau wieder her.
7. Die Memory Card bleibt ohne Speichern im Spiel unverändert.

Dabei wurden drei Fehler gefunden und behoben, bevor die Tests bestanden:

- Änderungen waren in Release-Builds standardmäßig erlaubt.
- Der Rückgängig-Stand wurde zu früh gemerkt.
- Der Test-Bot hat Änderungen falsch zeitlich eingeplant.

## 2. Implementiert, aber noch nicht getestet

- **Windows mit echter Grafikkarte:** Fenster, Vollbild (randlos/exklusiv), GPU-Renderer und flüssige Bilder über 60 FPS. Unter Linux/Xvfb ohne Fenstermanager wird randloses Vollbild nicht angewendet. Das ist eine Grenze der Testumgebung.
- **Controller:** Xbox und PlayStation über SDL3, Tastenbelegung im F1-Menü, Anzeige verbundener Controller. Ohne Gerät nicht prüfbar.
- **Ton an echten Lautsprechern unter Windows.**
- **Längere Spielsitzungen unter Windows.**
- **Bedienelemente im F1-Menü:** Fenstergrößen-Voreinstellungen und FPS-Begrenzung. Die Funktionen dahinter sind gebaut, die Bedienelemente selbst sind nicht per Klick getestet.
- **Cheat-Reiter per Maus:** Er nutzt denselben Änderungsweg wie der getestete Bot. Das Bild des Reiters ist geprüft, ein Klick-Test nicht.
- **Export aus dem F1-Menü („Jetzt exportieren“):** Er ruft dieselbe Funktion auf wie die getestete Befehlszeile.

## 3. Teilweise funktionsfähig

- **Story-Durchlauf:** Der Bot erreicht den Anfang von Mission 2. Danach verliert Agumon (Level 1) gegen zwei Level-5-Gegner. Ein Bot-Durchlauf bis zum Abspann bräuchte Kampftaktik, Leveln, Rekrutieren und Domain-Rätsel. Das ist nicht gebaut. **Das Spiel wurde nicht durchgespielt.** Der Spielcode selbst ist die unveränderte, vollständige Dekompilierung von Upstream.
- **Texturen ersetzen:** Nur in gleicher Größe und mit der Originalpalette. Farben werden auf den nächstliegenden Palettenton abgebildet, ein reines Rot wird bei Agumon also zu einem Orangeton. Höher aufgelöste Ersatztexturen (512², 1024²) unterstützt der Renderer nicht.
- **3D-Modelle:** Modelldateien werden je Digimon roh exportiert. glTF mit Animationen liefert der DW2 3D Viewer über `tools/viewer_bridge/export_assets.py`. Ein in das Spiel eingebauter 3D-Viewer existiert nicht. Animationen sind nicht in Einzeldateien zerlegt.
- **Startzentrale:** Sie ist ein Reiter im F1-Fenster mit Spielen, Spieldaten, Ordnern, Diagnose, Modding und Cheats. Ein eigenes Launcher-Programm gibt es nicht.
- **Moderne Schrift:** Scharf ist sie nur in den PC-Menüs (F1). Die Spielmenüs selbst nutzen die Originalschrift, im Remastered-Profil kantengeglättet hochskaliert. Neu gestaltet sind die Spielmenüs nicht.
- **Dateikategorien:** Sicher zugeordnet sind Digimon-Modelle mit Texturen, die Tabellen und die Overlays. Rund 3150 Dateien sind als „unknown“ markiert, statt geraten.

## 4. Fehlerhaft oder nicht implementiert

- **Deutsche Spieltexte:** Dialoge, Story, Items und Attacken sind nicht übersetzt, und eine Terminologie-Datenbank gibt es nicht. Nötig wären dafür:
  - die Spieltext-Kodierung und das Neuschreiben der Textdateien
  - eine Schrift mit Umlauten im Spiel, denn die PS1-Schrift hat keine
  - sorgfältig belegte offizielle Namen

  Übersetzt sind nur die PC-Menüs.
- **Kantenglättung (Anti-Aliasing)** als eigene Option.
- **Exportformate:** Audio (Musik, Effekte) wird nicht als WAV exportiert. Die 34 Ton- und Videostreams (Form 2) stehen nur im Index.
- **Mausbedienung in den Spielmenüs:** Die Maus funktioniert nur im F1-Fenster.

## Unterstützte Auflösungen und Bildformate

- **Fenstergrößen:** 1280×720, 1600×900, 1920×1080, 2560×1440, 3840×2160, 2560×1080, 3440×1440 als Voreinstellung. Jede andere Größe geht durch Ziehen am Fenster. Im exklusiven Vollbild sind alle Modi des Bildschirms wählbar.
- **3D-Auflösung:** 1× bis 12× oder automatisch nach Fensterhöhe (1080p 5×, 1440p 6×, 4K 9×).
- **Seitenverhältnis:** 4:3, 16:9 und 21:9 mit echtem erweitertem 3D-Sichtfeld. 2D-Menüs bleiben 4:3 mit Rand. Es wird nichts gestreckt.

## Import- und Exportformate

| Richtung | Formate |
|---|---|
| Import | Disc-Image als `.cue`/`.bin`, `.img` oder rohes `.iso` (USA, SLUS-01193) |
| Export | Originaldateien `.bin`, Texturen PNG (RGBA), Tabellen JSON, Index CSV |
| Textur-Import | PNG (8 Bit RGB/RGBA) |
| Mods | `Mods/files/<ID>.bin` |

## Windows-spezifische Risiken

- Der GPU-Renderer braucht Vulkan. Fehlt ein Treiber, im F1-Menü unter Renderer „Software“ wählen.
- Das Programm ist nicht signiert. Windows SmartScreen kann deshalb warnen.
- Ordner mit Sonderzeichen im Pfad sind unter Windows nicht getestet.

## Reproduzierbare Tests

| Test | Aufruf |
|---|---|
| Linux, Speichern | `tools/autoplay/save_test.py EXE dw2.pak WORK` |
| Linux, Cheats | `tools/autoplay/cheat_test.py EXE dw2.pak CARD WORK` |
| Linux, Domains | `tools/autoplay/domains_test.py` |
| Referenzbilder | Aufruf wie in `tools/wintest/DW2-Test.ps1`, Abschnitt 4 |
| Windows | `DW2-Test.bat` im Ordner `DW2-PC`; Ergebnis in `test_out\testbericht.txt` |
