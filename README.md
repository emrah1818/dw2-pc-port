# Digimon World 2 – PC Port

**Eigenständige Weiterentwicklung für Windows und Linux | Entwicklungsstand: 7. November 2026**

Dieses Repository dokumentiert unsere Weiterentwicklung eines nativen PC-Ports von **Digimon World 2 (USA, SLUS-01193)**. Ziel ist es, das ursprüngliche Spiel auf modernen PCs zugänglich zu machen und Grafik, Darstellung, Schrift, deutsche Sprache und Anpassbarkeit schrittweise zu verbessern, ohne die ursprüngliche Spiellogik unnötig zu verändern.

> **Entwicklungsprojekt:** Nicht alle Funktionen sind fertig oder auf echter Windows-Hardware geprüft. Die untenstehenden Statusangaben unterscheiden zwischen implementiert, automatisiert getestet, teilweise getestet und offen.

## Download / Releases

**[⬇️ Aktuelle Version herunterladen](https://github.com/emrah1818/dw2-pc-port/releases/latest)**

**[📦 Alle Releases und ältere Versionen ansehen](https://github.com/emrah1818/dw2-pc-port/releases)**

Die Release-Seite enthält die jeweils veröffentlichten Downloads und Versionshinweise. Falls noch kein Release veröffentlicht wurde, ist dort entsprechend kein Download verfügbar. Für die Nutzung benötigst du ein eigenes kompatibles Spielimage; Spieldaten werden nicht mitgeliefert.

---

## Funktionsübersicht

| Bereich | Stand | Hinweise |
| --- | --- | --- |
| Nativer PC-Port (Windows/Linux) | Implementiert | Windows-/Linux-Builds und CI vorhanden; reale Windows-Hardware nicht umfassend getestet |
| Spielstart aus eigenem USA-Image | Implementiert und getestet | Import und Prüfung der Spieldateien; keine Spieldaten im Repository |
| Spielwelt, Kämpfe, Menüs | Implementiert, teilweise getestet | Einzelne Szenen und automatisierte Abläufe geprüft; kein bestätigter vollständiger Durchlauf |
| Speichern/Laden | Automatisiert getestet | Speichertest zuletzt 9/9 |
| Cheat-Menü | Automatisiert getestet | Cheattest zuletzt 31/31 |
| 4K, 16:9, 21:9 | Implementiert, teilweise getestet | Interne Skalierungen und mehrere Spielszenen geprüft; weitere Fenster-/Hardwaretests offen |
| Classic-Darstellung | Automatisiert getestet | Englische Classic-Referenzbilder zuletzt 6/6 |
| Deutsche Spieltexte und Dialoge | Weitgehend implementiert | Automatisierte Textprüfungen vorhanden; vollständige manuelle Story-Prüfung fehlt |
| Sprachwahl Deutsch/Englisch | Implementiert | Neustart- und Hardwareverhalten noch weiter prüfen |
| Moderne, auflösungsabhängige Schrift | Implementiert und getestet | Laufzeit-Rendering in mehreren Skalierungen geprüft |
| Deutsche Kampf-Wortgrafiken | Teilweise getestet | Einige seltene Kampfzustände noch nicht im Spiel ausgelöst |
| HD-Texturpakete | Implementiert, teilweise getestet | Software-/GPU-Pfade und synthetische Tests vorhanden; reale GPU und Lastfälle weiter prüfen |
| Modernisierte Fenster/Bedienoberfläche | In Entwicklung | Panel-Design nicht allgemein freigegeben; vollständige neue Menü-Layouts fehlen |
| Asset-Export und leicht ersetzbare Ressourcen | Teilweise implementiert | Bestehende Export-/Mod-Funktionen; vollständiger strukturierter Ressourcen-Export offen |
| Integriertes 3D-Studio | Offen | Noch nicht implementiert |
| Eigenständiger Launcher | Offen | Noch nicht implementiert |

## Visueller Zwischenstand – deutsche Übersetzung (7. November 2026)

Ein aktueller, vom Projektinhaber bereitgestellter Screenshot zeigt vier Spielsituationen: zweimal Agumons Statusansicht, die Technik-Auswahl und die Digimon-Teamübersicht. Sichtbar sind bereits deutsche Bezeichnungen wie **Status**, **alle Techniken**, **Zurück**, **Kleine Flamme**, **Unterbrechen**, **Gegenangriff**, **Hilfe**, **Angriff**, **Abwehr**, **Tempo**, **Nächstes Level** sowie die Angriffsbeschreibung **„Schießt einen Feuerball“** und **„MP-Verbrauch 8“**.

Der Screenshot dokumentiert einen **Zwischenstand der deutschen Lokalisierung und Menügestaltung**, keine abgeschlossene Übersetzung oder Freigabe aller Ansichten. Er bestätigt für sich genommen weder die Performance noch die vollständige Funktionsfähigkeit sämtlicher Menüs. Das Originalbild wurde im Projektchat bereitgestellt; eine Bilddatei ist mit diesem Dokumentations-Commit noch nicht ins Repository hochgeladen worden.

---

## Bekannte Probleme und Einschränkungen

- **Performance beim Nachladen von Texturen:** Aus einem aktuellen Praxistest wurden deutliche Framerate-Einbrüche beim erstmaligen Anzeigen neuer Texturen und übersetzter Angriffsbanner gemeldet. Ursache und vollständige Behebung sind noch nicht bestätigt. Frühere automatisierte HD-Tests ohne Hänger ersetzen diesen Praxistest nicht.
- **Modern UI:** Ein neuer Panel-Stil wurde entwickelt, ist jedoch noch deaktiviert, weil nicht alle Ergebnis- und Menüfenster korrekt erkannt werden. Neue Status-, Lexikon-, Attacken- und Digitation-Layouts fehlen.
- **Grafikdetails:** Einzelne PGXP-/Clipping-Fälle am Bildrand sind offen; manche 3D-Räume zeigen in breiten Seitenbereichen Schwarz.
- **Schrift und Bildtexte:** Einzelne Zahlensprites und mehrzellige Wörter sind noch nicht vollständig modernisiert. Seltene Kampfanzeigen (u. a. Konter, Unterbrechen, NIEDERLAGE) benötigen zusätzliche In-Game-Prüfungen.
- **Hardwaretests:** Windows mit echter GPU, Controller, Audio, Vollbild, VSync und Langzeitstabilität sind nicht abschließend geprüft.
- **Story:** Ein vollständiger Durchlauf vom Spielbeginn bis zum Ende ist nicht bestätigt.

## Installation und Nutzung

1. Lade ein passendes Build aus den [Releases](../../releases), sofern verfügbar.
2. Entpacke das Archiv.
3. Lege dein **eigenes** kompatibles USA-Disc-Image (`.bin`/`.cue`, unterstütztes rohes `.img` oder `.iso`) bereit.
4. Starte `dw2.exe` unter Windows beziehungsweise die entsprechende Linux-Datei. Beim ersten Start wird das benötigte Datenpaket aus deinem Image erstellt.
5. Öffne die PC-Einstellungen mit **F1**. Sprache, Darstellung, Steuerung und weitere Funktionen lassen sich dort konfigurieren.

**Wichtig:** Das Repository enthält keine kommerziellen Spieldaten. Ein eigenes kompatibles Spielimage ist erforderlich. Die deutsche Sprach- und Mod-Ebene kann zusätzliche lokal erzeugte Dateien benötigen; nicht alle Übersetzungsdateien werden öffentlich bereitgestellt.

## Entwicklungsprotokoll und Tests

Die ausführlichen technischen Nachweise stehen in:

- [TESTBERICHT.md](TESTBERICHT.md) – Tests, Versionsstände, Fehler und offene Prüfungen
- [PROJECT_STATUS.md](PROJECT_STATUS.md) – Architektur, Anforderungen und offene Aufgaben
- [README_PORT.md](README_PORT.md) – Ausführliche Bedienung und Port-spezifische Funktionen
- [GRAFIKTEXTE.md](GRAFIKTEXTE.md) – Lokalisierte Grafiktexte
- [CHEATS.md](CHEATS.md) – Cheat-Funktionen und Einschränkungen

**Zuletzt dokumentierte automatisierte Ergebnisse:** Classic 6/6, Textskripte 90/90, Speichern 9/9 und Cheats 31/31. Diese Ergebnisse gelten für die jeweils im Testbericht genannten Commits; sie sind keine pauschale Bestätigung des gesamten Spiels oder jedes späteren Builds.

### Nächste Entwicklungsziele

1. Nachlade-Ruckler auf echter Hardware reproduzieren, messen und beheben.
2. Modern UI auf alle relevanten Spielbildschirme ausweiten und freigeben.
3. Deutsche Grafiktexte, Schrift und seltene Kampfzustände vollständig prüfen.
4. Export und Austausch von Texturen, Texten und weiteren Ressourcen vereinfachen.
5. Echte Windows-/GPU-/Controller-Tests und einen vollständigen Story-Durchlauf durchführen.
6. Später: eigenständiger Launcher und integriertes 3D-Studio.

## Projektursprung und Rechte

Diese Weiterentwicklung basiert auf dem Open-Source-Projekt [Wyrelade/DW2-PC](https://github.com/Wyrelade/DW2-PC) (im Projekt dokumentierter Ausgangscommit `1dbb32a`, CC0). Diese Herkunftsangabe ist keine Übernahme der ursprünglichen README oder deren Entwicklungsversprechen.

Die Lizenz des Repository-Codes ist in [LICENSE](LICENSE) dokumentiert. **Digimon World 2** und die zugehörigen Spielinhalte gehören ihren jeweiligen Rechteinhabern. Dieses Repository stellt keine Spielkopie bereit.
