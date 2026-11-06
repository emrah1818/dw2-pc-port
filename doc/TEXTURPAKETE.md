# Eigene Texturen und HD-Texturpakete (ab 1.4.0)

Texturpakete ersetzen Spieltexturen beim Zeichnen durch eigene PNG-Dateien, auch in höherer Auflösung. Das Datenpaket `dw2.pak` und die Originaldateien bleiben unverändert. Ohne Pakete verhält sich das Spiel genau wie vorher.

## Voraussetzungen

- **Renderer:** Software oder, ab 1.6.0, auch der GPU-Renderer (siehe unten „GPU-Renderer“). In 1.4.0 und 1.5.0 zeichnete der GPU-Renderer nur die Originaltexturen.
- **Skalierung 2 oder höher** (oder 16:9). Bei Skalierung 1 (Classic) bleibt das Bild bildgenau original.

## Schritt 1: Texturen ausgeben

```
dw2.exe --dump-textures C:\DW2\dump
```

Spiele die Stellen an, deren Texturen du ersetzen willst. Jede Textur, die das Spiel dabei zeichnet, landet einmal als PNG im Ordner. Die Dateien stammen aus deinem eigenen Spiel: **nicht veröffentlichen.**

## Schritt 2: Dateinamen verstehen

| Name | Bedeutung |
|---|---|
| `HASH_WxH.png` | 16-Bit-Textur |
| `HASH_WxH_m0_cPALETTE.png` | 4-Bit-Textur mit genau dieser Palette |
| `HASH_WxH_m1_cPALETTE.png` | 8-Bit-Textur mit genau dieser Palette |
| `HASH_WxH_m0.png` / `_m1.png` | gilt für **alle** Paletten dieser Textur |

- `HASH` ist die stabile ID der Textur (16 Hex-Zeichen). Sie hängt nur vom Inhalt ab, nicht von der Position im Videospeicher.
- `W` und `H` sind die Größe im Videospeicher. Die PNG selbst hat die Größe in Texeln.
- Dieselbe Textur mit anderer Palette (z. B. Farbvarianten eines Digimon) hat verschiedene `_c`-Namen. Willst du eine Ersatzdatei für alle Farben, entferne den `_c…`-Teil.
- Eine Datei mit `_c` hat Vorrang vor der ohne.

## Schritt 3: Ersatz malen

- Bearbeite eine Kopie der ausgegebenen PNG oder male neu.
- **Größe:** genau 1×, 2×, 3× … bis 16× der ausgegebenen Datei, in Breite und Höhe gleich. Beispiel: Dump 64×64 → erlaubt 128×128, 256×256, 512×512 …
- **Am besten passt** der Faktor deiner Skalierung (Skalierung 4 → 4×).
- **Transparenz:** Alpha unter 50 % ist durchsichtig, sonst deckend.
- Halbtransparenz übernimmt das Spiel vom Original-Texel an derselben Stelle.
- Die Aufteilung (UV-Layout) muss dem Original entsprechen. Jedes Original-Texel wird zu einem k×k-Block deiner Datei.

## Schritt 4: Paket anlegen

```
DW2-PC\
  dw2.exe
  Mods\
    Textures\
      MeinPaket\
        e545a5094e3fef15_16x64_m0.png
        …
```

- Jeder Unterordner von `Mods\Textures` ist ein Paket. Pakete werden nach Namen geladen; bei gleicher Textur gewinnt das spätere.
- Ein Ordner, der mit `_` beginnt (z. B. `_MeinPaket`), ist abgeschaltet.
- Pakete im Datenordner (`%APPDATA%\DW2-Online\Mods\Textures`) werden zuerst geladen, die neben `dw2.exe` danach.
- Neu starten. F1 → Modding zeigt die gefundenen Pakete und wie viele Dateien geladen oder fehlerhaft waren.
- Ohne Pakete starten: `dw2.exe --no-texture-packs`.

## Fehler und Protokoll

Jede Entscheidung steht in `Mods\Textures\textures_log.txt` (neben `dw2.exe`, wird bei jedem Start neu geschrieben) und in der Konsole mit `[textures]`:

- falscher Dateiname → übersprungen
- keine gültige PNG → Original bleibt
- Größe kein ganzzahliges Vielfaches → Original bleibt
- mehr als 1 GiB Ersatzpixel → weitere Dateien werden nicht geladen, Original bleibt

Das Spiel stürzt bei fehlenden oder kaputten Dateien nicht ab, es zeichnet dann das Original.

## Grenzen (Stand 1.4.0)

- Nur der Software-Renderer zeichnet Ersatztexturen.
- Ersetzt wird nur, wenn der Texturbereich eines Polygons ganz in einem Upload liegt und kein Texturfenster aktiv ist.
- Schreibt das Spiel später über eine Textur im Videospeicher (z. B. Paletten-Animation), fällt diese Textur auf das Original zurück.
- Ausgabe in 15-Bit-Farbe wie die übrigen HD-Flächen.
- Mit echten Spieldaten noch nicht geprüft (siehe TESTBERICHT.md). Geprüft ist der Renderer mit erzeugten Testtexturen bei Skalierung 2 und 4.

## Ab 1.6.0: Laden im Hintergrund und Stufen

- **Laden:** Die PNGs lädt ein eigener Thread. Bis eine Textur fertig ist, zeichnet das Spiel die Originaltextur. Bei großen Paketen kann eine Ersatztextur deshalb einige Bilder nach dem ersten Auftauchen erscheinen. Dafür steht das Spiel nicht still.
- **Stufen:** Ist eine Datei 2, 4, 8 oder 16 Mal so groß wie die Textur, entstehen beim Laden halbierte Stufen, gemittelt mit Alpha. Gezeichnet wird die Stufe, die zur internen Auflösung passt. Eine 16×-Datei bei Skalierung 4 nutzt also die 4×-Stufe. Andere Faktoren werden je Pixel blockweise gemittelt.
- **Speicher:** Die Stufen brauchen etwa ein Drittel mehr Speicher. Sie zählen zur Grenze von 1 GiB.
- **Getestet:** 32 Titeltexturen in 16× (bis 2048×4096 Pixel). Das Bild ist bei Skalierung 2 pixelgleich zum Original, ohne Hänger und ohne Tempoverlust.

## Ab 1.6.0: GPU-Renderer

Der GPU-Renderer (Vulkan über SDL_GPU) zeichnet jetzt Ersatztexturen mit denselben Paketen und Dateinamen.

- **Aufbau:** Die Ersatzpixel (die zur Skalierung passende Stufe) liegen in einem Speicherbereich auf der Grafikkarte von 128 MiB (so viel muss jede Vulkan-Karte in einem Storage-Buffer erlauben). Sie werden beim ersten Gebrauch hochgeladen, höchstens 32 MiB je Bild; was nicht mehr passt, erscheint ein paar Bilder später und bis dahin als Original. Ist der Bereich voll oder ändert sich die Skalierung, beginnt er neu. Der Fragment-Shader liest die Ersatzfarbe (Mittelung, Texelgrenzen und Transparenz wie im Software-Renderer, `rep_sample` in `hd.frag`) und nimmt das Maskenbit vom Originaltexel.
- **Laden:** wie oben, im Hintergrund; die Originaltextur dient als Rückfall (fehlende, fehlerhafte oder zu große Datei, voller Speicher).
- **Bekannter Unterschied zum Software-Renderer:** Bei 2-fach vergrößerten Sprite-Vierecken (Spieltext) interpoliert die GPU über das vergrößerte Viereck. Der Software-Renderer liest dort innerhalb jedes 1×-Pixels die ganze Texelbreite (nur bei 1:1 genau).
- **Getestet (Automatik, `hd_test`, in der CI auf Mesa lavapipe unter Linux und Windows, einem Software-Vulkan, keiner echten Grafikkarte):** Das GPU-Bild mit dem Testpaket gleicht dem Software-Bild bis auf 0,08 % der Pixel (Abweichung über 24, die vergrößerten Sprite-Vierecke ausgenommen); Sprites sind texelgenau aus der 4×-Textur gezeichnet; die 3D-Fläche und die vergrößerten Sprite-Vierecke benutzen die Ersatztextur; falsche Größe und kaputte PNG-Datei bleiben Original.
- **Nicht getestet (nur an echter Hardware prüfbar):** Spiel mit echten Texturpaketen auf einer echten Grafikkarte (NVIDIA, AMD, Intel); Tempo und Grafikspeicher bei 16×-Paketen; Verhalten bei vollem Pool im echten Spielablauf; Einfluss auf die weichen Zwischenbilder (Smooth Frames) beim Wechsel der Skalierung; Treiber, die weniger als 128 MiB je Storage-Buffer erlauben (Log: `[gpu] texture pack: no GPU memory for replacements`, dann gelten die Originale).
