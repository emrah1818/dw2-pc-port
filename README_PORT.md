# DW2 PC-Port (dw2-pc-port)

Native PC-Fassung von **Digimon World 2 (USA, SLUS-01193)** für Windows x64 und Linux x64.

Der Port ist ein Fork von [Wyrelade/DW2-PC](https://github.com/Wyrelade/DW2-PC), Stand
Upstream-Commit `1dbb32a0332a78d5e9afbe4d26c62d86cc5e3f76` (v1.0.0-beta, Lizenz CC0, siehe `LICENSE`).
DW2-PC ist eine vollständige Dekompilierung des Spiels in C mit eigener PC-Laufzeit (SDL3).

## Keine Spieldaten im Repository oder im Programm

Dieses Repository und die hier gebauten Programme enthalten **keine Daten aus dem Spiel**.
Du brauchst dein eigenes, rechtmäßig erworbenes Image von *Digimon World 2 (USA)*: ein Roh-Abbild,
also `.bin` + `.cue`, `.img` oder ein rohes `.iso`.

- **Upstream** baut 13 Tabellen aus der Disc direkt in das Programm ein (`INCLUDE_BIN`, etwa 0,5 MB),
  zum Beispiel die Dateitabelle, die Sound-Daten und den Overlay-Bereich.
- **In diesem Fork** baut `tools/build_native.py --no-disc` das Programm mit genullten Platzhaltern.
- **Beim Start** füllt `host/discassets.c` die Tabellen mit den echten Bytes aus deiner Disc. Dabei
  werden geprüft:
  - die Programmdatei `SLUS_011.93` (als `dw2.pak.slus` neben dem Datenpaket, SHA-256 aus
    `configs/USA/pack_manifest.txt`)
  - die Stage-Overlays im geprüften `dw2.pak`

## Erster Start

1. Zip entpacken: Windows `DW2-PC-Port-windows-x64.zip`, Linux `DW2-PC-Port-linux-x64.zip`.
2. Das Disc-Image (`.cue` + `.bin`) in den Ordner `DW2-PC` legen. Alternativ startest du mit
   `--disc <pfad zur .cue>` oder wählst das Image im Dateidialog.
3. `dw2.exe` (Linux: `./dw2`) starten. Beim ersten Start entstehen aus deinem Image:
   - `dw2.pak` (etwa 406 MB)
   - `dw2.pak.slus`

   Jede der 3675 Dateien wird gegen die SHA-256-Liste geprüft. Ein falsches oder verändertes Image
   wird abgelehnt.
4. Ab dann reicht der Start ohne Image. Spielstände und Einstellungen liegen im Benutzerordner
   (`%APPDATA%\DW2-Online\` bzw. `~/.local/share/DW2-Online/`).

Ein `dw2.pak` von einer älteren Version ohne `dw2.pak.slus` wird einmal aus dem Image neu gebaut.

## Änderungen gegenüber Upstream

| Datei | Änderung |
|---|---|
| `host/discassets.c` (neu) | Füllt die 13 Disc-Tabellen zur Laufzeit; meldet im Log `[disc] … loaded (k already identical)` |
| `host/pak.c` | Lädt und prüft `dw2.pak.slus`, ruft die Füllfunktionen vor allen Index-Prüfungen auf |
| `host/pakbuild.c` | Schreibt beim Paketbau die geprüfte `SLUS_011.93` als `dw2.pak.slus` |
| `tools/build_native.py` | Option `--no-disc` (eigenes Build-Verzeichnis `*_nodisc`, weigert sich bei vorhandenem `dumps/disc`) |
| `host/update.c`, `host/update.h` | Update-Prüfung abgeschaltet: Upstream-Releases würden diesen Fork sonst ersetzen |
| `.github/workflows/build.yml` (neu) | Baut Windows x64 (MSYS2 MinGW + SDL3) und Linux x64 ohne Spieldaten, veröffentlicht `test-build` |

## Getestet

Getestet am 05.11.2026 unter Linux x64 mit gcc 13 und SDL 3.4.8. Gestartet wurde headless unter
Xvfb mit Software-Renderer und einem verifizierten USA-Image (alle 3675 Dateien passen zum Manifest).

- **Selbsttest mit Disc-Daten im Build:** Die Laufzeit-Tabellen sind bytegleich mit den eingebauten
  (`7 exe tables … (7 already identical)`, `6 overlay tables … (6 already identical)`).
- **Build mit `--no-disc` in frischem Build-Verzeichnis:**
  - Stichproben aus allen Disc-Tabellen: 0 von 109 Treffern im Programm
  - Stichproben aus den STAG-Overlays: 0 von 135 Treffern
  - Beim Start werden alle 13 Tabellen geladen (`0 already identical`).
- **Bildvergleich:** Die Bildschirmfotos bei Boot, VBlank 300 und VBlank 700 sind MD5-gleich mit
  dem Build, der die Disc-Daten eingebaut hat.
- **Ohne `dw2.pak.slus`:** Das Programm will das Paket aus dem Image neu bauen.

## Noch nicht getestet

- Der Windows-Build wird nur von GitHub Actions gebaut. Ein Start unter Windows mit echtem
  Fenster, GPU-Renderer, Ton oder Controller ist hier nicht geprüft.
- Der vollständige Spielablauf über das Titelmenü hinaus ist nicht geprüft. Der Spielcode ist
  unverändert Upstream; geändert ist nur, woher die 13 Tabellen kommen.

Die weiteren Ausbaustufen stehen im Entwicklungsplan: 4K/Profile, HD-Texturen, Audio,
Steuerung, UI.
