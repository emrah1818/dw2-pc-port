@echo off
rem DW2-PC-Port: Windows-Testlauf (schreibt test_out\testbericht.txt)
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0DW2-Test.ps1"
echo.
echo Fertig. Bericht: %~dp0test_out\testbericht.txt
pause
