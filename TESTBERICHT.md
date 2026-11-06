# DW2 PC-Port – Testbericht (Stand: 1.4.0, 06.11.2026)

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
