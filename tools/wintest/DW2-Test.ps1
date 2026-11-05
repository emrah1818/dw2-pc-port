# DW2-PC-Port: Windows-Testlauf. Start: DW2-Test.bat (Doppelklick) im Ordner DW2-PC.
# Schreibt test_out\testbericht.txt und test_out\*.png. Nichts davon enthaelt Spieldaten ausser
# den Bildschirmfotos (zum Ansehen; nicht weitergeben, wenn du das nicht willst).
$ErrorActionPreference = "Continue"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $here
$out = Join-Path $here "test_out"
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $out | Out-Null
$rep = Join-Path $out "testbericht.txt"
$fails = 0
function Log([string]$s) { Write-Host $s; Add-Content -Path $rep -Value $s -Encoding UTF8 }
function Result([bool]$ok, [string]$what) {
    if ($ok) { Log "PASS $what" } else { Log "FAIL $what"; $script:fails++ }
}

Log "DW2-PC-Port Windows-Test, $(Get-Date -Format 'yyyy-MM-dd HH:mm')"
Log "Windows: $([Environment]::OSVersion.VersionString), 64-bit: $([Environment]::Is64BitOperatingSystem)"
try {
    Get-CimInstance Win32_VideoController | ForEach-Object { Log "GPU: $($_.Name), Treiber $($_.DriverVersion), $($_.CurrentHorizontalResolution)x$($_.CurrentVerticalResolution) @ $($_.CurrentRefreshRate) Hz" }
    Get-CimInstance Win32_Processor | ForEach-Object { Log "CPU: $($_.Name)" }
} catch { Log "Systeminfo nicht lesbar: $_" }
Log ""

# 1. Programm startet
$v = & .\dw2.exe --version 2>&1
Result ($LASTEXITCODE -eq 0 -and "$v" -match "DW2-PC") "dw2.exe --version: $v"

# 2. GPU-Renderer (Vulkan) mit synthetischer Szene, ohne Spieldaten
if (Test-Path .\tests\hd_test.exe) {
    Copy-Item .\SDL3.dll .\tests\ -Force
    New-Item -ItemType Directory -Force (Join-Path $out "hd_test") | Out-Null
    $t = & .\tests\hd_test.exe --out (Join-Path $out "hd_test") --require-gpu 2>&1
    $t | ForEach-Object { Add-Content -Path $rep -Value "  $_" -Encoding UTF8 }
    Result ($LASTEXITCODE -eq 0) "Renderer-Test: Software = GPU, Texturfilter, 2D-Glaettung (tests\hd_test.exe)"
} else { Log "SKIP tests\hd_test.exe fehlt" }

# 3. Datenpaket vorhanden?
$pak = $null
foreach ($p in @((Join-Path $here "dw2.pak"), (Join-Path $env:APPDATA "DW2-Online\dw2.pak"))) {
    if ((Test-Path $p) -and (Test-Path "$p.slus")) { $pak = $p; break }
}
if ($null -eq $pak) {
    Log "SKIP Spieltests: kein dw2.pak mit dw2.pak.slus gefunden. Starte dw2.exe einmal mit deinem Image (.cue/.bin im Ordner), dann diesen Test erneut."
} else {
    Log "Datenpaket: $pak"
    $press = @("--press", "620:Start", "--press", "750:Up", "--press", "780:Cross", "--press", "1200:Cross",
               "--press", "1300:Cross", "--press", "1400:Cross", "--press", "2000:Cross")

    # 4. Deterministischer Lauf (Software-Renderer, ohne Fenster): Bilder muessen den Linux-Referenzen gleichen
    $det = Join-Path $out "deterministisch"
    $args1 = @("--pak", $pak, "--no-window", "--fast", "--renderer", "soft", "--no-update-check",
               "--save-dir", (Join-Path $out "saves1"), "--shot-dir", $det,
               "--shot-vb", "300", "--shot-vb", "700", "--shot-vb", "1100", "--shot-vb", "1500", "--shot-vb", "2500") + $press + @("--vblanks", "2600")
    & .\dw2.exe @args1 > (Join-Path $out "deterministisch.log") 2>&1
    Result ($LASTEXITCODE -eq 0) "Lauf Titel -> Neues Spiel -> Intro ohne Absturz (Exit $LASTEXITCODE)"
    $ref = @{}
    Get-Content .\tests\referenz_md5.txt | ForEach-Object { $a = $_ -split "\s+"; if ($a.Count -ge 2) { $ref[$a[1]] = $a[0] } }
    foreach ($k in $ref.Keys) {
        $f = Join-Path $det $k
        if (Test-Path $f) {
            $h = (Get-FileHash -Algorithm MD5 $f).Hash.ToLower()
            Result ($h -eq $ref[$k]) "Bild $k gleich der Linux-Referenz"
        } else { Result $false "Bild $k fehlt" }
    }

    # 5. Fenster, GPU-Renderer, 4K-Stufe, 16:9, Filter, 2D-Glaettung: Fensterbilder wie F12
    $gpu = Join-Path $out "gpu_fenster"
    $args2 = @("--pak", $pak, "--renderer", "gpu", "--scale", "9", "--aspect", "16:9", "--pgxp", "--texture-filter", "bilinear",
               "--smooth-2d", "--windowed", "--no-update-check", "--save-dir", (Join-Path $out "saves2"), "--shot-dir", $gpu,
               "--window-shot-vb", "700", "--window-shot-vb", "1500", "--window-shot-vb", "2500") + $press + @("--vblanks", "2600")
    & .\dw2.exe @args2 > (Join-Path $out "gpu_fenster.log") 2>&1
    Result ($LASTEXITCODE -eq 0) "GPU-Fensterlauf ohne Absturz (Exit $LASTEXITCODE)"
    $shots = @(Get-ChildItem $gpu -Filter "dw2_*.png" -ErrorAction SilentlyContinue)
    Result ($shots.Count -ge 3) "Fenster-Screenshots geschrieben: $($shots.Count)"
    Get-Content (Join-Path $out "gpu_fenster.log") | Select-String -Pattern "^\[(gpu|host|audio|input|settings)\]" |
        Select-Object -First 40 | ForEach-Object { Add-Content -Path $rep -Value "  $_" -Encoding UTF8 }
    $glog = Get-Content (Join-Path $out "gpu_fenster.log") -Raw
    Result ($glog -match "\[audio\] driver") "Audiogeraet geoeffnet"
    if ($glog -match "\[gpu\]") { Log "INFO GPU-Zeilen siehe oben" }
    if ($glog -match "\[input\] gamepad \d+ on port") { Log "INFO Controller erkannt: $([regex]::Match($glog, '\[input\] gamepad \d+ on port \d+: [^\r\n]*').Value)" }
    else { Log "INFO kein Controller erkannt (nur Tastatur). Mit angeschlossenem Controller erneut testen." }

    # 6. Asset-Export und Textur-Rundlauf (nur lokal, nichts wird weitergegeben)
    $exp = Join-Path $out "GameData"
    & .\dw2.exe --pak $pak --no-window --no-update-check --export-assets $exp > (Join-Path $out "export.log") 2>&1
    $n = @(Get-ChildItem (Join-Path $exp "Original\files") -Filter "*.bin" -ErrorAction SilentlyContinue).Count
    $d = @(Get-ChildItem (Join-Path $exp "Digimon") -Directory -ErrorAction SilentlyContinue).Count
    Result ($LASTEXITCODE -eq 0 -and $n -ge 3600 -and $d -ge 190) "Asset-Export: $n Dateien, $d Digimon-Ordner (Linux: 3641 / 194)"
    $png = Get-ChildItem (Join-Path $exp "Digimon") -Recurse -Filter "*_p*.png" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $png) {
        $mods = Join-Path $here "Mods\files"
        $had = Test-Path $mods
        $r = & .\dw2.exe --pak $pak --no-window --no-update-check --import-texture $png.FullName 2>&1 | Out-String
        Result ($r -match "0 pixels changed") "Textur-Rundlauf ohne Aenderung: 0 Pixel geaendert ($($png.Name))"
        # the round trip wrote an identical copy into Mods/files: remove it again
        $id = $png.Name.Substring(0, 4)
        Remove-Item (Join-Path $mods "$id.bin") -ErrorAction SilentlyContinue
        if (-not $had) { Remove-Item -Recurse (Join-Path $here "Mods") -ErrorAction SilentlyContinue }
    } else { Result $false "Asset-Export: keine Digimon-Textur gefunden" }

    # 7. Deutsche Menues und Cheat-Reiter (Bilder ohne Fenster)
    foreach ($tab in @("Start", "Display", "Sound", "Modding", "Cheats")) {
        $sd = Join-Path $out "ui_$tab"
        & .\dw2.exe --pak $pak --no-window --fast --no-update-check --lang de --settings-ui --settings-tab $tab --ui-shots `
            --save-dir (Join-Path $out "saves3") --shot-dir $sd --shot-vb 400 --vblanks 420 > (Join-Path $out "ui_$tab.log") 2>&1
        Result (Test-Path (Join-Path $sd "vb400_ui.png")) "Menue-Bild Reiter $tab (Deutsch): $sd\vb400_ui.png"
    }
}

Log ""
Log "Manuell (bitte selbst pruefen und hier notieren): Controller (Xbox/PlayStation) im Spiel, Ton (Musik/Effekte/Ton aus),"
Log "  Vollbild (F11) in deiner Aufloesung, F1-Menue (Sprache Deutsch, Cheats einschalten -> Sicherung), laengere Spielsitzung"
if ($fails -eq 0) { Log "ERGEBNIS: alle automatischen Pruefungen bestanden" } else { Log "ERGEBNIS: $fails Pruefung(en) fehlgeschlagen" }
Log "Bitte schicke test_out\testbericht.txt (und gern die Logs in test_out) zurueck."
