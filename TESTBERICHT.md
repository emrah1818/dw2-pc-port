# DW2 PC-Port – Testbericht (Version 1.2.0-port, 05.11.2026)

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
