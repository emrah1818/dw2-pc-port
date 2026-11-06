# Grafiktexte – Inventar (Stand 1.5.0)

Texte, die das Spiel nicht als Schrift, sondern als **Bild** zeigt (Wortgrafiken). Der Port malt sie beim Start auf Deutsch neu, und zwar aus den Daten des Spielers selbst: Schriftbild der Kampfmeldungen (Datei 0x22B) und kleine Spielschrift (0x13A), Farben aus dem englischen Originalbild gelernt (`host/picwords.c`). Im Repository liegen weder Bilder noch Spieldaten, nur der Code und die deutschen Wörter.

Spalten: **Ort** = Datei:Eintrag der Bildliste; **Form** = wie das Bild entsteht; **Test**:
- *im Spiel* = im laufenden Spiel auf Bildschirmfotos geprüft
- *Textur* = gemalte Textur geprüft (`DW2_PICDUMP`), Szene nicht im Testlauf erreicht

## Kampf

| Englisch | Deutsch | Ort | Form | Test |
|---|---|---|---|---|
| BATTLE | KAMPF | 0x313:1 | große Kursivschrift neu gemalt | im Spiel (Kampfbeginn, 4:3, 16:9, 21:9) |
| FIGHT | LOS! | 0x313:4 | große Schrift | Textur |
| Run Away | Flucht | 0x313:2 | große Schrift | Textur |
| Unable to / Run Away | Keine / Flucht | 0x313:3 | große Schrift, zwei Bilder | Textur |
| GAME OVER | SPIEL VORBEI | 0x313:5 | große Schrift, zwei Bilder | Textur |
| LOSE | NIEDERLAGE | 0x313:6 | schmale Spielschrift | Textur |
| WIN | SIEG | 0x25C:10 | große Schrift | im Spiel (Kampfende) |
| LEVEL UP | LEVELAUFSTIEG | 0x25C:7 | schmale Spielschrift, Bild in freie Texturfläche verlegt und auf 124 px verbreitert | Textur |
| TARGET | ZIEL | 0x13B:3 | große Schrift | im Spiel (Zielauswahl) |
| SELECT | WÄHLE | 0x13B:22 | große Schrift | Textur |
| Damage | Schaden | 0x13B:2 | kursiv | Textur |
| Missed | Verfehlt | 0x1A0:3 | kursiv | im Spiel („Attacke Verfehlt“) |
| Attack | Attacke | 0x1A0:4 | kursiv | im Spiel |
| Counter Attack | Konter Attacke | 0x1A0:5 + 4 | kursiv, zwei Bilder | Textur |
| Interrupt | Unterbrechen | 0x1A0:6 | kursiv | Textur |
| Assist | Hilfe | 0x1A0:7 | kursiv | Textur |
| Guard | Abwehr | 0x1A0:8 | kursiv | Textur |
| Do you want to / interrupt? | Willst du / unterbrechen? | 0x1A0:10 | kursiv, zwei Zeilen | Textur |
| Yes / No | Ja / Nein | 0x1A0:1 / 2 | kursiv | Textur |
| Critical | – | – | kein Bild „Critical“ in den durchsuchten Kampfdateien gefunden | offen |

**Zustandsmeldungen im Kampf** (Datei 0x22B, Einträge 1–72): Diese sind keine Bilder, sondern Listen einzelner Buchstabenbilder. Der Port setzt dafür deutsche Buchstabenlisten ein, z. B. „Vergiftet“, „Gift geheilt“, „Gelaehmt“, „Voll geheilt“. Das sind 68 Meldungen, Eintrag 46 ist ein Platzhalter. Umlaute werden als ae/oe/ue geschrieben, weil diese Schrift keine hat. Test: Textur und Zellen geprüft. Im Testlauf trat keine Zustandsmeldung auf.

**Attackennamen in den Animationen:** Jede Attacke hat ein Namensbild in ihrer Effektdatei. Die Zuordnung läuft über die ID in der Attackentabelle (0x25B, +0x0C). Gemalt wird nur, was in der geprüften Tabelle `attacks.csv` des Spielers einen deutschen Namen hat. Es werden keine Namen erfunden.
- Bilder: 43 von 43 Bilder gemalt, kein Fehler.
- Liste: 213 Attacken haben keinen geprüften deutschen Namen. Sie stehen mit „no verified German name“ im Bericht `Languages/German/report.txt`.
- Test: Textur. Im Testlauf endeten die Kämpfe ohne Namensbanner im Foto.

## Titel

| Englisch | Deutsch | Ort | Form | Test |
|---|---|---|---|---|
| PRESS START BUTTON | STARTTASTE DRÜCKEN | 0xB64:1 | schmale Schrift, drei Bilder | im Spiel |
| NEW GAME | NEUES SPIEL | 0xB64:2 | schmale Schrift, auf 80 px verbreitert | im Spiel |
| CONTINUE GAME | FORTSETZEN | 0xB64:3 | schmale Schrift | im Spiel |
| BATTLE MODE | KAMPFMODUS | 0xB64:4 | schmale Schrift | im Spiel |
| Logo, © / ™-Zeilen | unverändert | 0xB64:5 | – | wie verlangt nicht übersetzt |

Die Lesbarkeit wurde in 1.5.0 verbessert:
- M und N der Kursivschrift: Vorher sah „KAMPF“ wie „KAAPF“ aus und „NIEDERLAGE“ wie „HIEDERLAGE“.
- S und G der schmalen Schrift: Vorher waren sie von 8 und 0 kaum zu unterscheiden.
- Schmale Bilder mit weniger als etwa 8,5 Pixeln je Buchstabe werden nicht mehr fett gemalt, damit die Lücken in E und N offen bleiben.

## Als Text (nicht als Bild) gezeigt

Kampfbefehle und Menüs sind normale Spieltexte aus `texts.tsv`, z. B. „Befehle geben“, „Kanone“, „Fliehen“, „Kampf“, „Abwehr“, der Befehl „Attacke“, „Gegenangriff“, „Hilfe“, „Unterbrechen“ sowie die Beschreibungen. Sie laufen über das PC-Layout und wurden im Spiel geprüft.

Der Wert „Angriff“ im Statusmenü bleibt „Angriff“: Es ist der Wert, nicht der Kampfbefehl.

## Wie prüfen

- `DW2_PICDUMP=<Ordner> dw2 --settings-set language=de` schreibt die gemalten Dateien und die Buchstabenschrift (`022B_font.bin`). `DW2_PICDEBUG=1` zeigt verlegte Bilder an.
- Im Log steht: `[lang] word pictures German: 25 of 25, 43 of 43 attack names`.
