# Digitation, DNA-Digitation und Digimon-Informationen: technischer Zwischenbericht

Stand 7.11.2026, Commit nach 2afc8663 (1.6.0-preview6). Ermittelt aus dem Code dieses Repositorys.
Es enthält keine Spieldaten. Die Spielregeln (Stufen, DP, DNA-Ergebnisse) werden **nicht** verändert:
Modern UI ändert nur Darstellung (Fensterdesign, Schrift, Anordnung).

## 1. Normale Digitation und DNA-Digitation: das Digi-Labor (Stadt, Overlay `stag2000`)

Beide Funktionen sitzen im selben Modul der Stadt-Bühne (Tabelle `Stg20_TaskDescs` in
`src/stag2000/walker.c`). Der Einstieg ist das Digi-Labor-Objekt `Stg20_DigiLabDesc`.

| Datei | Funktion(en) | Aufgabe |
|---|---|---|
| `src/stag2000/digilab.c` | `Stg20_DigiLabUpdate` / `Stg20_DigiLabDraw` | Ablauf des Labors (Moduswahl, Untermenüs, Bestätigung) |
| `src/stag2000/digilab.c` | `Stg20_LabDigivolve` (Zeile ≈487) | normale Digitation: Digimon wählen, Prüfung, Ziel, Bestätigung (Text 0x116 „Digimon zum Digitieren wählen“) |
| `src/stag2000/digilab.c` | `Stg20_LabDnaDigivolve` (Zeile 10) | DNA-Digitation: zwei Digimon wählen, Prüfung, Ergebnis, Bestätigung (Text 0x117) |
| `src/stag2000/dna.c` | `Stg20_GetDnaTypeIndex`, `Stg20_GetDnaResult` | **Regeln** der DNA-Digitation (Ergebnis-Digimon aus beiden Eltern): bleibt unverändert |
| `src/stag2000/labmodesel.c` | `Stg20_LabModeSelDraw` | Moduswahl „Digitation / DNA-Digitation“ (Texte 0x102 / 0x103) |
| `src/stag2000/labroster.c` | `Stg20_LabRosterUpdate/Draw`, `…SetText`, `…FillSlots` | Digimon-Liste des Labors (Name, Art, Stufe, Level als Zahl) |
| `src/stag2000/labinfo.c` | `Stg20_LabInfoUpdate/Draw` | Werte des gewählten Digimon: HP, MP, Level, Angriff, Abwehr, Tempo, EXP, nächstes Level, **DP** (`Gfx_SetPartsNumber` 0x002…0x800); Meldungen 0x118, 0x11A, 0x11B, 0x11D, 0x12A |
| `src/stag2000/labskills.c` | `Stg20_LabSkillsGroup/SetText/Update/Draw` | Techniken des Digimon (Kategorien wie im Status-Menü) |
| `src/stag2000/labpair.c` | `Stg20_LabPairUpdate/Draw` | Paaranzeige bei DNA (die beiden Eltern) |
| `src/stag2000/labcaption.c` | `Stg20_LabCaptionUpdate/Draw` | Überschrift |
| `src/stag2000/labdigimodel.c` | `Stg20_LabDigiModelUpdate/Draw` | 3D-Modell des Digimon (Einblendung 0 → 0x1000, `Gfx_DrawTexModel`) |
| `src/stag2000/labjogbg.c` | `Stg20_LabJogBgUpdate/Draw` | Hintergrund |
| `src/stag2000/msgwin.c` | `Stg20_MsgWinShowSysMsg(id)` | Meldungsfenster des Labors (Systemtexte 01FD) |

**Texte** (Datei 01FD, deutsch über die private `texts.tsv`):
- 0x102, 0x103: Modusnamen.
- 0x115–0x11D: Auswahl, Fehlermeldungen und Bestätigungen, z. B. 0x119 „Digitation zu {ARG1} Fortfahren? JA NEIN“, 0x11A „Rookie-Digimon können keine DNA-Digitation machen.“
- 0x12A: weitere Meldung.

Die Tastensymbole kommen aus dem Textsystem (`{G:3F}` = ✕) und haben in Modern schon die neuen PlayStation-Symbole.

**Grafik:** Texturdatei **0xD12**, Einträge 0–10:

| Eintrag | Inhalt |
|---|---|
| 0 | Moduswahl |
| 1 | Überschrift |
| 2 | Liste |
| 7 | Paar |
| 8 | Werte |
| 9, A | Techniken |

Gezeichnet wird mit den Spiel-„Parts“ (`Gfx_DrawParts`), Textfeldern (`Text_Open*`) und dem 3D-Modell (`Gfx_DrawTexModel`). Alles läuft über denselben HD-Renderer wie die übrigen Menüs. Es gibt keine eigene Pipeline.

Die Zahlen (HP, MP, Level, DP …) werden in `Gfx_SetPartsNumber` registriert (`src/main/parts.c` → `Fonts_RegisterNumber`) und erscheinen daher in Modern bereits in Exo 2 Bold.

**Nicht im Modern-Fensterdesign:** 0xD12 steht nicht in der Liste der Panel-Sheets (`host/uipanels.c`, `sheet_ids`). Das Labor hat deshalb noch die Original-Fenster.

## 2. Digimon-Informationen im Hauptmenü („Digimon“)

| Datei | Funktion | Aufgabe |
|---|---|---|
| `src/main/digilist.c` | `Menu_DigiListTask`, `Menu_SetDigiListMode` (Modi 0–8) | Digimon-Liste des Feldmenüs (Digi-Beetle / Server), Digi-Line, Transfer, Item-Ziel, Kampfauswahl |
| `src/main/digistatus.c` | `Menu_DigiStatusInit/Task/Draw` | Status eines Digimon: 3D-Modell, Typ, Stufe, Spezialität, LV, HP, MP, Angriff, Abwehr, Tempo, EXP, nächstes Level, Eltern. **Modern:** Modell eingepasst, Reiter Status/Techniken (preview1–6) |
| `src/main/skilllist.c` | `Menu_SkillListTask`, `Menu_SkillListOpenNames` | „alle Techniken“: 4 Kategorien, Beschreibung, MP-Verbrauch. **Modern:** MP pro Zeile, Reiter |

Grafik: Sheets 0x513 (Layouts 0x513001C–0x5130026) und das Menü-Fenstersheet. Diese sind bereits im Modern-Fensterdesign.

## 3. Lexikon

**Im Code gibt es keine eigenständige Lexikon- oder Enzyklopädie-Funktion.** Durchsucht wurden `src/` und `include/` nach Lexikon, Encyclopedia, Digipedia, Dictionary, Library und Dex sowie die Texttabellen. Die einzigen Informationsbildschirme zu Digimon sind:
- der Status im Hauptmenü (oben),
- die Werte- und Technikseite im Digi-Labor.

Ein „Lexikon“ wäre also eine **neue PC-Funktion**. Sie dürfte nur aus vorhandenen Spieldaten zur Laufzeit gefüllt werden (Namen, Typ, Stufe, Techniken). Sie wird nicht ohne Freigabe gebaut, weil „fehlende Anforderungen nicht erfinden“ gilt.

## 4. Erforderliche Änderungen für Modern UI (geplant, noch nicht umgesetzt)

1. **0xD12 ins Fensterdesign** (`host/uipanels.c`):
   - Zuerst die Klassifikation prüfen: Füllung, Rahmen, Kanten zwischen Teilen.
   - Wortbilder ausnehmen.
   - Vorher-/Nachher-Bilder machen.
2. **Texte einpassen:** Die vorhandene Zeilen-Einpassung (`host/uilayout.c`) gilt automatisch. Die deutschen Meldungen 0x115–0x12A sind im Labor zu prüfen.
3. **3D-Modell im Labor:** dieselbe Einpassung wie im Status-Menü (`Gfx_ModelScreenBounds`, `src/main/model.c`), falls es dort zu klein oder zu groß ist.
4. **Breitbild:** Das Labor ist eine Stadt-Szene (keine Kampf-Tabelle 5). Die Anordnung in 16:9/21:9 ist zu prüfen.
5. **DP-Anzeige:** Die Zahl ist schon modern. Lesbarkeit und Farbe auf dem neuen Fenster prüfen, so wie bei der Level-Zahl im Kampf.
6. **Spielregeln unverändert:** Keine Änderung an `dna.c`, `Stg20_LabDigivolve` oder `Stg20_LabDnaDigivolve` außer reiner Darstellung.

## 5. Vorschau vor der Integration

Die Vorschau wird hinter einem Schalter gebaut (`DW2_MODERN_LAB=1`, standardmäßig aus). Sie wird erst nach Freigabe Standard.

Zum Prüfen wird ein Spielstand gebraucht, mit dem man das Digi-Labor erreicht. Der Test-Spielstand dieses Repos-Arbeitsplatzes steht am Spielanfang. Der Spielstand bleibt lokal und wird nie eingecheckt.
