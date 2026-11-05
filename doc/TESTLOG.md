# DW2 PC-Port – Arbeits- und Testprotokoll (laufend)

Stand-Notizen für den Testbericht. Kategorien: GETESTET / IMPLEMENTIERT-UNGETESTET / TEILWEISE / FEHLT.

## Ergebnisse bisher (05.11.2026)
- Klassik-Bild bytegleich mit Upstream-Build mit Disc-Daten (boot, vb300, vb700), `--profile classic`.
- Deterministischer Lauf Titel→Neues Spiel→Namenseingabe: tests/referenz_md5.txt, 2 Läufe identisch.
- Speichertest (tools/autoplay/save_test.py): 9/9 PASS (speichern über Menü, laden, 4 kaputte Karten ohne Absturz).
  Früherer FAIL war Testskript (Kartenmenü verlässt man mit Dreieck).
- Import: fehlendes Paket / falsches Image -> saubere Ablehnung Exit 1, kein Paket (Linux; Windows-CI-Schritt).
- Windows-CI (echtes Windows Server): hd_test Software-Renderer 5/5 PASS; GPU via lavapipe: ICD-Registry-Fix läuft.
- 4K: Fenster 3840x2160 -> auto 9x (2158 Zeilen), UI-Skala 2.40, Remastered (16:9, PGXP, bilinear, 2D-Glättung).
- Borderless-Fullscreen unter Xvfb ohne Fenstermanager bleibt 1281x720: Umgebungsgrenze (X11 ohne WM), unter Windows nicht geprüft.

## Story-Bot (tools/autoplay/story.py)
- Neues Spiel -> Intro -> Boot Domain -> Stadt: gespielt (to_city.py).
- Stadt: alle 7 Ziele, Räume, Gespräche: Gold Hawk beitreten -> Agumon (id 3), progress 0->1, Flags +256 +394 +398.
- Main Gate Gespräch (11,10) -> Domain-Karte 0x32A: [(0,0x301,2) Stadt, (629,0x200,29) SCSI?, (600,0x200,0) Boot].
- SCSI: erster Kampf mit Agumon L1 und "Attack" verloren -> Game Over (0x401). Braucht Kampf-KI + Leveln.
- Karten: st2/card_round0.mcd = nach Beitritt (Agumon L1).

## Szenenmodi
0x301 Main Gate, 0x302 Mechaniker, 0x303 Gold Hawk (0x304/0x305/0x306 Räume), 0x307 Blue Falcon,
0x30B Black Sword, 0x30F Tamer's Club, 0x310 Digimon Center, 0x311 Coliseum (0x312), 0x32A Domain-Karte,
0x32D Stadtkarte, 0x32F Digi-Lab, 0x330-0x332 Item-Shop, 0x333+ Beetle-Shop, 0x200 Dungeon, 0x5xx Kampf,
0x401 Game Over, 0x601 Speicherkarte, 0x602 Laden.
