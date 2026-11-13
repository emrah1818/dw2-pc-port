# DW2 PC-Port (Version 1.4.0-port)

Dies ist eine native PC-Fassung von **Digimon World 2 (USA, SLUS-01193)** für Windows x64 und Linux x64. Sie bietet:

- 4K und Ultrawide
- Grafikprofile Original, Balanced, HD und Ultra
- deutsche und englische PC-Menüs, deutsche Spieltexte aus deiner eigenen Disc (Story, Dialoge, Menüs)
- Spielfenster Modern oder Klassisch: kein Text ragt über sein Fenster
- Asset-Export und Mod-Ebene für Texturen
- ein Cheat-Menü mit Sicherung

Was getestet ist und was nicht, steht in **[TESTBERICHT.md](TESTBERICHT.md)**.

Der Port ist ein Fork von [Wyrelade/DW2-PC](https://github.com/Wyrelade/DW2-PC), Upstream-Commit `1dbb32a` (Lizenz CC0). Er beruht auf einer vollständigen Dekompilierung des Spiels.

**Spieldaten:** Programm und Repository enthalten keine Spieldaten. Du brauchst dein eigenes, rechtmäßig erworbenes Image. Alles, was der Export erzeugt, stammt aus deiner Disc: Gib es nicht weiter.

## 1. Installation und Import deines Spiels

1. **Entpacken:** `DW2-PC-Port-windows-x64.zip` von der Release-Seite laden und entpacken.
2. **Image dazulegen:** Dein Image (`Digimon World 2 (USA).cue` + `.bin`, oder `.img` / rohes `.iso`) in den Ordner `DW2-PC` legen.
3. **Starten:** `dw2.exe` starten. Beim ersten Start entstehen `dw2.pak` (etwa 406 MB) und `dw2.pak.slus`. Jede der 3675 Dateien wird gegen die SHA-256-Liste geprüft, ein falsches Image wird abgelehnt.
4. **Danach:** Ab dann startet das Spiel ohne Image. Spielstände und Einstellungen liegen in `%APPDATA%\DW2-Online\`.

Ein anderes Image importierst du so: `dw2.pak` und `dw2.pak.slus` löschen, dann neu starten.

**Optionaler Selbsttest:** `DW2-Test.bat` doppelklicken. Den Bericht findest du in `test_out\testbericht.txt`.

## 2. Bedienung

- **F1:** Einstellungen. Die Reiter sind Start, Grafik, Steuerung, Spiel, Audio, Modding und Cheats. Am Controller öffnet „Select 1 Sekunde halten“ das Fenster.
- **Funktionstasten:**

  | Taste | Funktion |
  |---|---|
  | F3 | Tempo |
  | F4 | Texturfilter |
  | F5 | Auflösung |
  | F6 | kein Wackeln |
  | F7 | Seitenverhältnis |
  | F8 | Schärfen |
  | F9 | flüssige Bilder |
  | F11 | Vollbild |
  | F12 | Screenshot |
  | Esc | Beenden (mit Rückfrage) |

- **Tastatur (Standard):**

  | Taste | PS1-Taste |
  |---|---|
  | Pfeiltasten | Steuerkreuz |
  | Z | Cross |
  | X | Circle |
  | A | Square |
  | S | Triangle |
  | Q / W / E / R | L1 / R1 / L2 / R2 |
  | Enter | Start |
  | Rücktaste | Select |

  Die Belegung ist frei änderbar unter F1 → Steuerung.

## 3. Sprache

Deutsch oder English stellst du an drei Stellen ein:
- **Erster Start:** Das Fenster „Select Language / Sprache wählen“ erscheint von selbst.
- **Titelbild:** **Select** kurz drücken, auf dem Controller oder der Tastatur.
- **Jederzeit:** F1 → Start → Sprache.

Die Wahl wird gespeichert, unabhängig vom Spielstand. Sie gilt für das Spiel, das F1-Menü und das Cheat-Menü.
- Am Titel startet das Programm sofort neu, weil die Spieltexte beim Start geladen werden.
- Im Spiel fragt es vorher „Später / Jetzt neu starten“.

Mit Deutsch ändert sich Folgendes:

- **PC-Menüs:** komplett deutsch.
- **Digimon-Typen im Spiel:** **Serum** (Vaccine), **Datei** (Data), **Virus**. Das gilt im Statusfenster, im Cheat-Menü und im Export. Die internen Typ-IDs bleiben gleich.
- **Attackennamen im Spiel:** Sie kommen aus `Languages/German/attacks.csv`, die aus deiner Tabelle `Languages/German/source/digimon_world_2_attacken_de_en.xlsx` gebaut wird (`tools/i18n/build_attacks.py`). Die Regeln:
  1. Erste Anime-Bezeichnung.
  2. Sonst die Bezeichnung aus einem anderen Spiel.
  3. Sonst bleibt der englische Name.

  Stand: 43 von 182 Attacken haben einen deutschen Namen, alle 43 wurden im Spiel gefunden und ersetzt. Schaden, MP und IDs bleiben unverändert.
- **Umlaute im Spiel:** Ä Ö Ü ä ö ü ß werden beim Start aus deiner eigenen Spielschrift erzeugt, in Zeichenzellen, die kein Text nutzt.
- **Bericht:** Jeder Start auf Deutsch schreibt `Languages/German/report.txt`. Darin steht für jede Attacke im Spiel Englisch → Deutsch oder der Grund, warum sie englisch bleibt.
- **Dialoge, Kampf-, Menü-, System-, Item- und Attackenbeschreibungstexte:** aus `Languages/German/texts.tsv` mit `dictionary.txt`. Beide liegen seit 1.6.0-preview10 im Download: eine inoffizielle Fan-Übersetzung, die nur den deutschen Text enthält. Eigene Texte gehen so: `dw2.exe --export-texts DIR` erzeugt die englische Inventur, dann zeilenweise `SCHLÜSSEL<Tab>Deutsch` eintragen.
- **Dialoge und Story (ab 1.4.0):** `--export-texts DIR` erfasst auch die Ereignistabellen der Gespräche (Kategorie `dialog eN bN digiN`). Skriptbefehle stehen als `{B:..}` in der Zeile und müssen in der Übersetzung unverändert und in derselben Reihenfolge bleiben. `dialog_files.tsv` zeigt je Datei, ob ihr Aufbau sicher erkannt ist. Längere deutsche Texte passen durch Verschieben der Tabellenblöcke und durch `Languages/German/dictionary.txt` (häufige Wörter als 2-Byte-Codes, erzeugt mit `tools/i18n/build_dict.py`). Passt eine Übersetzung trotzdem nicht, bleibt der englische Text.
- **Wortgrafiken (ab 1.4.0):** Menübegriffe, die das Spiel als Bild im Font hat („Counter Attack“, „Specialty“ …), stehen in der deutschen Fassung als normaler Text in `texts.tsv` („Gegenangriff“, „Spezialität“, „DNA-Digitation“) und werden vom PC-Layout eingepasst (Abschnitt 4). Für Texte, die die Bilder weiter nutzen, zeichnet der Port die Bilder zusätzlich aus den Spielglyphen deutsch neu (`Languages/German/words.tsv`: Zeilen `CODE<TAB>Wort`).
- **Noch englisch:** die großen Kampfschriftzüge („BATTLE“, „Attack“, „Damage“, „WIN“, Attackennamen im Effekt) sind Bilder der Spielgrafik, dazu technische Item- und Teilenamen, die im Deutschen gleich lauten. Siehe TESTBERICHT.md.
- **Eigene Übersetzungen:** `attacks.csv` und `types.csv` sind einfache UTF-8-Dateien (`englisch;deutsch;...`). Du kannst sie bearbeiten, beim nächsten Start gelten sie.

## 4. Grafik

- **Profile** (F1 → Grafik, oder `--profile original|balanced|hd|ultra`):
  - **Original:** das PS1-Bild (früher „Classic“).
  - **Balanced:** 720 Zeilen, 16:9, kein Wackeln, gefilterte Texturen, flüssige Bilder.
  - **HD:** 3D in Fensterauflösung, dazu Schärfen 25 und geglätteter Text (früher „Remastered“, Standard beim ersten Start).
  - **Ultra:** 2160 Zeilen fest, Schärfen 40. Auf kleineren Bildschirmen wird das Bild weich verkleinert; braucht einen schnellen PC.
  - Jede Einstellung lässt sich danach einzeln ändern, das Profil heißt dann „angepasst“.
- **Auflösung:** Automatisch nach Fensterhöhe (Full HD 5×, WQHD 6×, 4K 9×) oder fest 1× bis 12×. Fenstergrößen-Voreinstellungen reichen von 720p bis 4K, dazu 2560×1080 und 3440×1440.
- **Seitenverhältnis:** 4:3, 16:9 oder 21:9. Breitbild zeigt mehr von der 3D-Welt und streckt nichts.
  - Im Kampf, mit Spielfenster *Modern*, rücken Befehlsmenüs, Attackenlisten und Team-Statusfenster in die Seitenränder (ab 1.5.0), sodass die Digimon in der Mitte frei bleiben.
  - Ab 1.6.0 laufen die Kachelhintergründe von Feldmenü, Shops, Digi-Beetle-Werkstatt und DNA-Labor bis an den Rand. Im Gespräch rückt das Porträt in den Seitenrand.
  - Die Fenster von Gesprächen, Ergebnisbildschirm und Menüs bleiben mittig in 4:3-Größe, vollständig lesbar.
- **Spielfenster (F1 → Start, ab 1.4.0):** *Modern* setzt Buchstaben nach ihrer eigenen Breite (Ziffern und Tabellen bleiben in ihren Spalten), *Klassisch* behält die festen PS1-Zeichenzellen. In beiden Modi misst der Port jede Textzeile vor dem Zeichnen. Ist sie breiter als ihr Fenster, wird das Fenster nach rechts verlängert, so weit Bildschirm und Nachbarfenster es erlauben, oder der Text daneben rückt zur Seite. Erst wenn beides nicht reicht, wird die Zeile enger gesetzt. Englische Texte passen in die Originalfenster: Mit *Klassisch* sieht ein englisches Spiel genauso aus wie auf der PS1. Technik: `host/uilayout.c`, `src/main/text.c`.
- **Bildrate:** Die Spiellogik läuft immer im Originaltempo, mit 59,94 Hz. Flüssige Bilder werden mit der Monitorrate zwischen den Spielbildern überblendet. Dazu gibt es eine FPS-Begrenzung. Die FPS-Anzeige (F1 → Grafik: aus, FPS, FPS und Details) zeigt oben rechts die Bilder pro Sekunde des Spiels, mit Details auch Fensterrate, längstes Bild, Auflösung und Renderer.
  - Ab 1.5.0 werden ohne vsync nie mehr Bilder gezeichnet, als der Monitor zeigt.
  - Kommt das Spiel nicht hinterher, pausieren die Zwischenbilder.
  - Bei „Auto“-Auflösung mit dem Software-Renderer sinkt die Auflösung um eine Stufe.

## 5. Spieldateien, Texturen und 3D-Modelle

**F1 → Modding → „Jetzt exportieren“** schreibt `GameData` neben das Programm. Alternativ geht das per Befehlszeile:

```
dw2.exe --export-assets GameData
```

Die Struktur:

```text
GameData/
├── README.txt
├── Original/
│   ├── index.csv            Datei-ID, LBA, Größe, Anzahl Texturen, was die Datei ist (oder "unknown")
│   └── files/<ID>.bin       alle Disc-Dateien unverändert (ID = Datei-ID hexadezimal)
├── Textures/<ID>/<ID>_<OFFSET>[_p<N>].png   alle TIM-Bilder als PNG (N = Palette)
├── Digimon/<Name>_<id>/
│   ├── Metadata/info.json   Typ, Stufe, Spezialgebiet, gelernte Attacke, Wachstum, Modelldatei
│   ├── Models/<ID>.bin      die Modelldatei der Art (aus der Spieltabelle)
│   └── Textures/            die Textur mit jeder Palette, die das Modell benutzt
└── GameDatabase/
    ├── digimon.json
    ├── items.json
    └── skills.json
```

**Textur ersetzen:**

1. Die PNG mit einem Bildprogramm bearbeiten. Größe nicht ändern, Dateinamen nicht ändern.
2. Importieren:
   ```
   dw2.exe --import-texture GameData\Digimon\Agumon_003\Textures\005C_00347C_p56.png
   ```
   Das schreibt `Mods\files\005C.bin`. Farben werden auf die Originalpalette dieser Textur abgebildet.
3. Spiel neu starten. Der Reiter Modding zeigt die Zahl aktiver Mod-Dateien.

**Andere Dateien ersetzen:** Als `Mods\files\<ID>.bin` ablegen. Die Datei darf nicht größer sein als das Original.

**Original wiederherstellen:** Die Datei in `Mods\files` löschen oder mit `--no-mods` starten. Das Datenpaket selbst wird nie verändert.

**HD-Texturpakete (ab 1.4.0):** Eigene PNG-Texturen, auch in höherer Auflösung, ohne die Spieldateien zu verändern. Ordner `Mods\Textures\<Paket>\`, Dateinamen = Textur-ID. Die IDs erzeugst du mit `dw2.exe --dump-textures DIR`. Gezeichnet werden sie ab Skalierung 2 vom Software-Renderer und, ab 1.6.0, auch vom GPU-Renderer (dort nur in der CI auf Software-Vulkan getestet, siehe `doc/TEXTURPAKETE.md`). Abschalten: `--no-texture-packs`.
- Ab 1.6.0 lädt ein eigener Thread die PNGs. Bis eine Textur bereit ist, zeigt das Spiel das Original und wartet nicht.
- 2-, 4-, 8- und 16-fache Texturen bekommen verkleinerte Stufen, damit sie bei niedrigerer Auflösung nicht flimmern. Anleitung: [doc/TEXTURPAKETE.md](doc/TEXTURPAKETE.md).

**3D-Modelle als glTF mit Animationen:** Dafür gibt es den DW2 3D Viewer:

```
tools/viewer_bridge/export_assets.py --viewer <viewer>/app/src/main/python --disc <image.cue> OUT
```

## 6. Cheat-Menü

1. **F1 → Cheats → „Cheats einschalten“.**
   - Deine Memory Cards werden vorher nach `saves\cheat_backup_<Datum>\` kopiert.
   - Der Spielstand direkt vor der ersten Änderung wird für „Rückgängig“ gemerkt.
2. **Ändern:**
   - Bits setzen
   - Werte jedes Digimon in Team und Server ändern: Level, EXP, HP, MP, Angriff, Verteidigung, Tempo
   - alle heilen, Digi-Beetle heilen
   - Items in die Tasche legen
   - ab 1.5.0, nach der Code-Sammlung `Cheat_Digimon_World_2_SLUS`:
     - Max-Knöpfe
     - gehaltene Werte: Unendlich Bits, HP und MP des Teams (auch im Kampf), HP und EP des Beetles
     - Art, Level-Grenze und Attacken 1–12 je Digimon
     - 28 Story-Items
     - Taschenplätze

   Alle Einträge, Grenzen und nicht übernommenen Codes stehen in [CHEATS.md](CHEATS.md).

   Werte werden gegen die Grenzen des Spiels geprüft. Ungültige Werte werden abgelehnt, das Ergebnis steht unter „Letzte Ergebnisse“.
3. **Rückgängig:** „Alle Cheat-Änderungen rückgängig“ stellt den gemerkten Stand wieder her. Im Kampf oder im Kartenbildschirm wartet es, bis der vorbei ist.
4. **Wichtig:**
   - Cheats sind bei jedem Programmstart aus.
   - Gespeichert wird nur, wenn du im Spiel speicherst.
   - Für einen sauberen Spielstand nach Cheats die Sicherung zurückkopieren.

## 7. Änderungen gegenüber Upstream

| Bereich | Dateien |
|---|---|
| Disc-Tabellen zur Laufzeit statt im Programm | `host/discassets.c`, `host/pak.c`, `host/pakbuild.c`, `tools/build_native.py --no-disc` |
| Grafik: 4K/Auto, 21:9, Profile, Texturfilter, 2D-Glättung, FPS-Begrenzung | `backend/`, `host/sdl.c`, `host/settings.c` |
| PC-Menüs Deutsch/Englisch, Start-, Modding- und Cheat-Reiter | `host/settingsui.cpp` |
| Deutsche Spieltexte, Ereignisskripte, Wörterbuch, Umlaute, Wortgrafiken | `host/lang.c`, `host/textscript.c`, `tools/i18n/` |
| PC-Layout der Spielfenster (Modern/Klassisch) | `host/uilayout.c`, `src/main/text.c`, `src/main/parts.c` |
| HD-Texturpakete | `backend/texrep.c`, `doc/TEXTURPAKETE.md` |
| Mod-Ebene, Asset-Export, Textur-Import | `host/pak.c`, `host/assets.c` |
| Cheat-Backend (Upstream-Editor, im Release standardmäßig aus) | `host/cheat.c`, `host/devedit.c`, `host/devdata.c` |
| Audio: getrennte Lautstärken, Ton aus | `host/audio.c` |
| Update-Prüfung abgeschaltet (Upstream-Updates würden den Fork ersetzen) | `host/update.c` |
| Test-Bot, Speicher-, Cheat- und Domaintests | `host/bot.c`, `tools/autoplay/` |
| Windows-Tests | `tools/wintest/`, `tests/hd_test.c`, `.github/workflows/build.yml` |
