# Grafiktexte – Inventar (Stand 1.6.0, mit moderner Schrift aus PR #1)

Texte, die das Spiel nicht als Schrift, sondern als **Bild** zeigt (Wortgrafiken). Der Port malt sie beim Start neu (`host/picwords.c`): Deutsch mit deutschen Wörtern, Englisch mit den englischen. Seit PR #1 stammen die Buchstaben aus den eingebetteten Schriften: **Orbitron Bold** für Kampf- und Titelwörter, **Exo 2** für Hinweise, Copyright und Titelcode, **Roboto Mono** für Zahlen und Werte ([doc/FONTS.md](doc/FONTS.md)). Die Farbe ist die Körperfarbe des Originalbilds (seine häufigste Farbe ab drei Pixeln Abstand vom Rand), eine Farbe je Bild. Classic UI auf Englisch bleibt das PS1-Bild (Referenzbilder 6/6). Im Repository liegen weder Bilder noch Spieldaten, nur der Code und die Wörter.

**Laufzeitschrift (7.11.2026):** Ab Skalierung 2 zeichnet der Renderer diese Bilder nicht mehr als vergrößerte 1×-Texel, sondern rastert sie in Ausgabeauflösung neu aus den Schriftumrissen, mit demselben Layout und durch die Spielpalette (Pfad und Grenzen: [doc/FONTS.md](doc/FONTS.md)). Im Spiel in 4K geprüft: Kampfwörter mit Zoom, Attackennamen, LEVELAUFSTIEG/SIEG-Bildschirm, Titel-Copyright, Titelcode.

Geprüft in 1.6.0 (nach dem Zusammenführen von PR #1): Deutsch in 4:3, 16:9 und 21:9 im laufenden Spiel (`battle_words.py`, Sieg und Niederlage), Englisch Modern UI und Classic in 4:3. Bilder, die der Testlauf nicht erreicht, wurden aus den gemalten Dateien (`DW2_PICDUMP`) mit ihren Zellen und der Palette des Bildes gerendert und neben dem Original verglichen.

Spalten: **Ort** = Datei:Eintrag der Bildliste; **Form** = wie das Bild entsteht; **Test**:
- *im Spiel* = im laufenden Spiel auf Bildschirmfotos geprüft
- *Textur* = gemalte Datei (`DW2_PICDUMP`) mit den Zellen des Bildes gerendert und neben dem Original geprüft (Form, Größe, Abschneiden); die Szene hat der Testlauf nicht erreicht. Die Farbe im Spiel setzt die Laufzeitpalette, sie ist dort nicht geprüft. Konter und die Unterbrechungsfrage erscheinen nur bei bestimmten Gegneraktionen; auch mit umgeschriebenen Zügen (`--poke-turns`) hat der Testbot sie nicht ausgelöst.

## Kampf

| Englisch | Deutsch | Ort | Form | Test |
|---|---|---|---|---|
| BATTLE | KAMPF | 0x313:1 | Orbitron | im Spiel (Kampfbeginn, 4:3, 16:9, 21:9) |
| FIGHT | LOS! | 0x313:4 | Orbitron | im Spiel (4:3, 16:9, 21:9) |
| Run Away | Flucht | 0x313:2 | große Schrift | im Spiel (1.6.0) |
| Unable to / Run Away | Keine / Flucht | 0x313:3 | große Schrift, zwei Bilder | Textur |
| GAME OVER | SPIEL VORBEI | 0x313:5 | Orbitron, zwei Bilder | im Spiel (verlorener Kampf, 4:3, Farbwechsel wie im Original) |
| LOSE | NIEDERLAGE | 0x313:6 | Orbitron, klein (10 Buchstaben im Bild von „LOSE“; schmaler gestaucht schloss sich die 1-px-Lücke zwischen N und I) | Textur |
| WIN | SIEG | 0x25C:10 | Orbitron | im Spiel (Kampfende, 4:3, 16:9, 21:9) |
| LEVEL UP | LEVELAUFSTIEG | 0x25C:7 | schmale Spielschrift, in freie Texturfläche verlegt, 78 px, rechtsbündig im Wertefeld hinter „Nächstes Level“ (das „!“ des Originals bleibt) | im Spiel (1.6.0, Ergebnisbildschirm) |
| TARGET | ZIEL | 0x13B:3 | große Schrift | im Spiel (Zielauswahl) |
| SELECT | WÄHLE | 0x13B:22 | große Schrift | Textur |
| Damage | Schaden | 0x13B:2 | Orbitron | im Spiel (4:3, 16:9, 21:9) |
| Missed | Verfehlt | 0x1A0:3 | Orbitron | im Spiel („Attacke Verfehlt“, 4:3, 16:9) |
| Attack | Attacke | 0x1A0:4 | Orbitron | im Spiel (4:3, 16:9, 21:9) |
| Counter Attack | Konter Attacke | 0x1A0:5 | Orbitron, zwei Wörter | Textur |
| Interrupt | Unterbrechen | 0x1A0:6 | Orbitron | Textur |
| Assist | Hilfe | 0x1A0:7 | Orbitron | im Spiel (4:3, 16:9) |
| Guard | Abwehr | 0x1A0:8 | Orbitron | im Spiel (4:3, 21:9) |
| Do you want to interrupt? | Willst du unterbrechen? | 0x1A0:10 | eine Zeile über beide Zellen (vorher je Zelle ein Wort: „unterbrechen?“ viel kleiner als „Willst du“) | Textur |
| Yes / No | Ja / Nein | 0x1A0:1 / 2 und 11 / 12 | Orbitron | Textur |
| Critical | – | – | kommt im Spiel nicht vor: kein Bild, kein Text und keine Stelle im Kampfcode (`src/stag3000`), 1.6.0 geprüft | entfällt |

**Zustandsmeldungen im Kampf** (Datei 0x22B, Einträge 1–72): Diese sind keine Bilder, sondern Listen einzelner Buchstabenbilder. Der Port setzt dafür deutsche Buchstabenlisten ein, z. B. „Vergiftet“, „Gift geheilt“, „Gelaehmt“, „Voll geheilt“. Das sind 68 Meldungen, Eintrag 46 ist ein Platzhalter. Umlaute werden als ae/oe/ue geschrieben, weil diese Schrift keine hat. Test: im Spiel gesehen „wurde giftig“ (Gift-Attacke des Gegners, 4:3 und 16:9, lesbar); die übrigen Meldungen als Textur und Zellen.

**Attackennamen in den Animationen:** Jede Attacke hat ein Namensbild in ihrer Effektdatei. Die Zuordnung läuft über die ID in der Attackentabelle (0x25B, +0x0C). Gemalt wird nur, was in der geprüften Tabelle `attacks.csv` des Spielers einen deutschen Namen hat. Es werden keine Namen erfunden.
- Bilder: 43 von 43 Bilder gemalt, kein Fehler.
- Liste: 213 Attacken haben keinen geprüften deutschen Namen. Sie stehen mit „no verified German name“ im Bericht `Languages/German/report.txt`.
- Test (1.6.0, `tools/autoplay/battle_words.py`): im Spiel geprüft „Kleine Flamme“, „Megafeuer“, „Magisches Feuer“ (Gegner), englisch geblieben „Electric Shock“ (kein geprüfter Name).
- Behoben in 1.6.0:
  - Lange Namen wurden auf die halbe Breite zusammengedrückt. Das Innere der Buchstaben verschmierte dabei zu einer zweiten, kleineren Schrift („Magisches Feuer“). Jetzt werden die Buchstaben eher niedriger gesetzt, höchstens 20 % schmaler und mittig.
  - Klarere Formen für m, w, W, a, g und i: „Kleine Flamme“ las sich vorher wie „Flaooe“.

## Titel

| Englisch | Deutsch | Ort | Form | Test |
|---|---|---|---|---|
| PRESS START BUTTON | STARTTASTE DRÜCKEN | 0xB64:1 | Orbitron, drei Bilder | im Spiel |
| NEW GAME | NEUES SPIEL | 0xB64:2 | Orbitron, auf 100 px verbreitert | im Spiel |
| CONTINUE GAME | FORTSETZEN | 0xB64:3 | Orbitron | im Spiel |
| BATTLE MODE | KAMPFMODUS | 0xB64:4 | Orbitron | im Spiel |
| TM & © … / © 2000 BANDAI | unverändert (Wortlaut) | 0xB64:5 | Exo 2, zwei Zeilen | im Spiel (DE und EN, 4:3, 16:9, 21:9) |
| Please, plug a Controller … | unverändert (Wortlaut) | 0xB64:6 | Exo 2, zwei Zeilen | Textur (erscheint nur ohne zweiten Controller im Kampfmodus) |
| Programmcode im Hintergrund („static int AttackControl(WORK *p)“ …) | unverändert (Code, nicht übersetzt) | 0x269:1, 19 von 25 Zellen | Exo 2 in voller Zeilenhöhe, bis 2,5× schmaler gestaucht; die 6 Zellen unter 16 Texeln (Klammern, „t);“, „ge“) bleiben original. Roboto Mono war in diesen sehr schmalen Zellen nicht lesbar („WORK“ verklumpte). | im Spiel (DE und EN, 4:3, 16:9, 21:9) |
| Binärspalten (0/1) im Hintergrund | unverändert | 0x8A (369 Layouts) | bleiben original: um 90° gedrehte Ziffern, der Maler setzt keinen gedrehten Text | – |
| Logo „Digimon Digital Monsters / Digimon World 2“ | unverändert | 0xB5E–0xB62, Bootbild | bleibt original: Markenlogo (gezeichnete Grafik, keine Schrift) | – |

**Boot und Intro:** Das Bootbild ist eine Illustration mit Logo und eingemaltem grünem Code (ein Bild, keine Textzellen); es bleibt unverändert. Die Intro-Filme (nur mit `--movies` und Disc) haben ihre Schrift ins Video eingebrannt; eine Schriftart lässt sich dort nicht austauschen. In 16:9 und 21:9 steht der Titel unverzerrt in 4:3 mit schwarzen Seiten wie bisher.

Historisch (Maler vor PR #1, 1.5.0):
- M und N der Kursivschrift: Vorher sah „KAMPF“ wie „KAAPF“ aus und „NIEDERLAGE“ wie „HIEDERLAGE“.
- S und G der schmalen Schrift: Vorher waren sie von 8 und 0 kaum zu unterscheiden.
- Schmale Bilder mit weniger als etwa 8,5 Pixeln je Buchstabe werden nicht mehr fett gemalt, damit die Lücken in E und N offen bleiben.

## Als Text (nicht als Bild) gezeigt

Kampfbefehle und Menüs sind normale Spieltexte aus `texts.tsv`, z. B. „Befehle geben“, „Kanone“, „Fliehen“, „Kampf“, „Abwehr“, der Befehl „Attacke“, „Gegenangriff“, „Hilfe“, „Unterbrechen“ sowie die Beschreibungen. Sie laufen über das PC-Layout und wurden im Spiel geprüft.

Der Wert „Angriff“ im Statusmenü bleibt „Angriff“: Es ist der Wert, nicht der Kampfbefehl.

## Wie prüfen

- `DW2_PICDUMP=<Ordner> dw2 --settings-set language=de` schreibt die gemalten Dateien und die Buchstabenschrift (`022B_font.bin`). `DW2_PICDEBUG=1` zeigt verlegte Bilder an.
- Im Log steht (Modern UI oder Deutsch): `[lang] modern word pictures: 24 of 24, 207 of 207 attack names`, `[fonts] title notices: 2 of 2`, `[fonts] title code: 19 of 19`. Mit Classic UI auf Englisch fehlen diese Zeilen: dort wird nichts gemalt.
- `tools/autoplay/battle_words.py EXE dw2.pak WORK CARD --hd --extra="--settings-set language=de --settings-set wide=1"` (16:9; `wide=2` 21:9; in 4:3 zusätzlich `--scale 2`) macht einen Testkampf mit Aufnahmen; `--mode lose` einen verlorenen.
