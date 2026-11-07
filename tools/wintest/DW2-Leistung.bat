@echo off
rem DW2-PC-Port: Leistungsmessung (schreibt test_out\leistung.csv und test_out\leistung.txt)
rem Spiel normal spielen (zum Beispiel einen Kampf mit Attacken), dann das Spiel beenden.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0DW2-Leistung.ps1"
echo.
echo Fertig. Bericht: %~dp0test_out\leistung.txt
pause
