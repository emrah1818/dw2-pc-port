# DW2 PC-Port (Version 1.3.0-port)

Dies ist eine native PC-Fassung von **Digimon World 2 (USA, SLUS-01193)** für Windows x64 und Linux x64. Sie bietet:

- 4K und Ultrawide
- Profile Classic und Remastered
- deutsche und englische PC-Menüs
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

Unter **F1 → Start → Sprache** (oder F1 → Grafik) stellst du Deutsch oder English ein. Die Wahl wird gespeichert, für die Spieltexte gilt sie nach einem Neustart. Beim ersten Start folgt die Sprache der Systemsprache.

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
- **Kampf-, Menü-, System-, Item- und Attackenbeschreibungstexte:** aus `Languages/German/texts.tsv`. Die Datei wird nicht veröffentlicht, weil sie auf den Spieltexten beruht. Eigene Texte gehen so: `dw2.exe --export-texts DIR` erzeugt die englische Inventur, dann zeilenweise `SCHLÜSSEL<Tab>Deutsch` eintragen.
- **Dialoge und Story (ab 1.4.0, Technik fertig, Übersetzung offen):** `--export-texts DIR` erfasst jetzt auch die Ereignistabellen der Gespräche (Kategorie `dialog eN bN digiN`, Art `script`). Skriptbefehle stehen als `{B:..}` in der Zeile und müssen in der Übersetzung unverändert und in derselben Reihenfolge bleiben (`tools/i18n/build_texts.py` prüft das). Zusätzlich entsteht `dialog_files.tsv`: je Datei, ob ihr Textbereich sicher verstanden ist und wie viel Platz frei ist. Passt eine Übersetzung nicht, bleibt der englische Text; eine Datei mit nicht verstandenem Bereich wird gar nicht verändert.
- **Noch englisch:** Dialoge und Story (Übersetzung folgt), Wortgrafiken in der Schrift („Specialty“, „Counter Attack“ …), technische Itemnamen. Siehe TESTBERICHT.md.
- **Eigene Übersetzungen:** `attacks.csv` und `types.csv` sind einfache UTF-8-Dateien (`englisch;deutsch;...`). Du kannst sie bearbeiten, beim nächsten Start gelten sie.

## 4. Grafik

- **Profile:** Classic ist das Originalbild. Remastered bietet 3D in Fensterauflösung, 16:9, kein Wackeln, gefilterte Texturen, geglätteten Text und flüssige Bilder.
- **Auflösung:** Automatisch nach Fensterhöhe (Full HD 5×, WQHD 6×, 4K 9×) oder fest 1× bis 12×. Fenstergrößen-Voreinstellungen reichen von 720p bis 4K, dazu 2560×1080 und 3440×1440.
- **Seitenverhältnis:** 4:3, 16:9 oder 21:9. Breitbild zeigt mehr von der 3D-Welt und streckt nichts. Menüs und 2D-Bilder bleiben 4:3.
- **Bildrate:** Die Spiellogik läuft immer im Originaltempo. Flüssige Bilder werden mit der Monitorrate zwischen den Spielbildern überblendet. Dazu gibt es eine FPS-Begrenzung.

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

**HD-Texturpakete (ab 1.4.0):** Eigene PNG-Texturen, auch in höherer Auflösung, ohne die Spieldateien zu verändern. Ordner `Mods\Textures\<Paket>\`, Dateinamen = Textur-ID. Die IDs erzeugst du mit `dw2.exe --dump-textures DIR`. Gezeichnet werden sie vom Software-Renderer ab Skalierung 2. Abschalten: `--no-texture-packs`. Anleitung: [doc/TEXTURPAKETE.md](doc/TEXTURPAKETE.md).

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
| Mod-Ebene, Asset-Export, Textur-Import | `host/pak.c`, `host/assets.c` |
| Cheat-Backend (Upstream-Editor, im Release standardmäßig aus) | `host/cheat.c`, `host/devedit.c`, `host/devdata.c` |
| Audio: getrennte Lautstärken, Ton aus | `host/audio.c` |
| Update-Prüfung abgeschaltet (Upstream-Updates würden den Fork ersetzen) | `host/update.c` |
| Test-Bot, Speicher-, Cheat- und Domaintests | `host/bot.c`, `tools/autoplay/` |
| Windows-Tests | `tools/wintest/`, `tests/hd_test.c`, `.github/workflows/build.yml` |
