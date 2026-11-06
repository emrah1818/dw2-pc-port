# Cheat-Menü (Stand 1.5.0)

**Wo:** F1 → Reiter **Cheats**. Bedienung mit Maus, Tastatur oder Controller, wie das ganze F1-Menü (Controller: Select halten öffnet es). Das Menü steht in der Spielsprache, Deutsch oder Englisch.

**Schutz:**
- Cheats sind bei jedem Programmstart aus.
- Beim Einschalten werden zuerst alle Memory Cards in einen Sicherungsordner kopiert. Der Pfad wird im Menü angezeigt.
- Der Spielstand von diesem Moment wird für **„Alle Cheat-Änderungen rückgängig“** gemerkt.
- Cheats speichern nichts von selbst. Erst wenn du im Spiel speicherst, landet die Änderung auf der Karte.
- Jede Änderung wird vor dem Schreiben gegen die Grenzen des Spiels geprüft. Ein falscher Wert wird abgelehnt und nicht gekürzt.
- Geschrieben wird beim nächsten Bild im Spiel-Thread, also nie mitten in einer Spielroutine.
- Nicht während der Memory-Card-Szene.
- Das Rückgängigmachen wartet, bis Kampf oder Kartenbildschirm vorbei sind.

## Kategorien

| Kategorie | Art | Werte / Grenzen |
|---|---|---|
| Tamer: Bits | Zahl + **Max** | 0–99.999.999 (Grenze des Spiels) |
| Gehaltene Werte: Unendlich Bits | Schalter | hält die Bits bei 99.999.999 |
| Gehaltene Werte: Unendlich HP / MP (Team) | Schalter | füllt die drei Team-Digimon auf ihr eigenes Maximum, auch die Kampfkopien. Ein Digimon mit 0 HP bleibt besiegt. |
| Gehaltene Werte: Unendlich HP / EP (Digi-Beetle) | Schalter | füllt auf das Maximum des Beetles |
| Digi-Beetle: HP, Max. HP, EP, Max. EP | Zahl, **Alle heilen**, **Max** | 0–9999 |
| Team und Server (alle 36 Plätze) | Tabelle: Level, EXP, HP, Max. HP, MP, Max. MP, Angriff, Verteidigung, Tempo; **Max** je Zeile; **Alle heilen** | Level 1–99, EXP 0–99.999.999, Werte 0–999 (Grenze des Spiels: Digilab, dreistellige Anzeige). Max setzt EXP auf 9.999.999 und alle Werte auf 999 (die Werte der Cheat-Liste); das Level bleibt. |
| Digimon-Details | Auswahl des Digimon: Art (Liste aller Digimon mit Grunddaten), Level-Grenze, Attacken Platz 1–12 (Liste aller Attacken oder „keine“) | Art: nur Digimon mit Daten. Grenze 1–99. Attacken: nur gültige IDs. |
| Wichtige Items (28 Story-Items, Tamer-Liz. bis SeadraRing) | Schalter je Item, mit Warnung | haben / nicht haben |
| Taschenplätze | Item je Platz wählen oder leeren | nur belegte Plätze und der erste freie, wie die Tasche des Spiels |
| Item in die Tasche | Auswahl + Hinzufügen | wie das Spiel es einräumt |
| (aus 1.4.0) Flags, Fortschritt, Rang, Namen, Teile, Lager, Warp | wie bisher | wie bisher |

Warnungen stehen direkt am Eintrag, z. B. bei den Story-Items („kann Ereignisse überspringen oder blockieren“) und bei der Art.

## Abgleich mit der Code-Sammlung `Cheat_Digimon_World_2_SLUS.txt`

Grundlage ist der Spielstand-Block `Save_GameState` (PS1-Adresse 0x8005E620). Jede Adresse der Sammlung wurde einem Feld zugeordnet und im Testlauf geprüft.

| Codes der Sammlung | Adresse | im Port | Test |
|---|---|---|---|
| 1st–6th Digimon: Digimon Modifier | 0x8005E705 + n·0x5C | Art (Digimon-Details) | bestanden |
| Current Level Modifier | …E711 | Level | bestanden |
| Level Limit Modifier | …E713 | Level-Grenze | bestanden, 0 abgelehnt |
| Exp Modifier / Max Exp | …E714 | EXP (32 Bit; die Sammlung schreibt nur 16 Bit) / Max | bestanden |
| Max HP / Infinite HP | …E718 / …E71A | Max. HP / HP und Schalter „Unendlich HP“ | bestanden |
| Max MP / Infinite MP | …E71C / …E71E | Max. MP / MP und Schalter „Unendlich MP“ | bestanden |
| Max Attack / Defense / Speed | …E720 / E722 / E724 | Angriff / Verteidigung / Tempo, 999 | bestanden, 1000 abgelehnt |
| Slot 1–12 | …E726 … E731 | Attacken Platz 1–12 | bestanden (Platz 12) |
| Have Important Item (28) | 0x8005F5CE … F604 | Wichtige Items (Lagerzähler der Item-IDs 0xED–0x108) | bestanden (0xED und 0x108); 0x109 abgelehnt |
| Item Slot 1–48 | 0x8005E686 … E6E4 | Taschenplätze | bestanden (Platz 1) |
| Misc: Infinite / Max Bits | 0x8005E628 | Schalter / Max | bestanden |
| Misc: Infinite HP / Max HP, Infinite EP / Max EP | 0x8005E644 / 646 / 648 / 64A | Beetle-Schalter / Max | bestanden |

Die Sammlung zählt die Digimon 1–6. Das sind die Roster-Plätze 0–5: die drei Team-Digimon und die ersten drei im Server. Der Port bietet alle 36 Plätze an.

**Abweichungen, mit Absicht:**
- „Infinite HP/MP“ schreibt in der Sammlung ständig 999, auch über das Maximum. Der Port füllt nur bis zum eigenen Maximum des Digimon, damit die Anzeige nicht über dem Maximum steht. Zusätzlich gilt es im Kampf, denn dort rechnet das Spiel mit Kopien des Teams.
- „Infinite Bits“ hält 99.999.999 statt nur die unteren 16 Bit.

**Nicht übernommen:**
- **Item Slot 49–64** (0x8005E6E6–0x8005E704): Die Tasche des Spiels hat 48 Plätze (`bagItems[0x30]`). Die Adressen dahinter sind ein ungenutzter Bereich, der zweite Beetle-Name und mit Slot 64 das erste Byte des ersten Roster-Eintrags. Wer dort schreibt, beschädigt den Spielstand.
- Codes mit „??“ (Auswahlwerte) werden nicht als Text eingegeben, sondern über Listen gewählt. So bleiben nur gültige Werte möglich.

## Test

`tools/autoplay/cheat_test.py EXE dw2.pak CARD WORK` mit echtem Spielstand: **31 von 31 Prüfungen bestanden**.
- Cheats aus: jede Änderung wird abgelehnt.
- Die Sicherung ist eine identische Kopie.
- Jede Kategorie oben ist geprüft, dazu Grenzwerte und gehaltene Werte (z. B. Bits auf 5 gesetzt und vom Schalter auf 99.999.999 zurückgesetzt).
- Rückgängig stellt den Spielstand-Block bytegleich wieder her (Spielzeit ausgenommen).
- Die Memory Card bleibt unverändert.

Im Kampf: In zwei Kämpfen ohne Schalter fiel das MP auf 991/999 und 987/999. Mit „Unendlich HP/MP“ blieben alle drei Digimon bei 999/999. Schaden an HP gab es in beiden Läufen nicht.

Bot-Befehle für Tests: `devedit`, `cheathold MASK`, `cheatundo`, `settings cheats SCROLL` (öffnet den Reiter für Fotos).
