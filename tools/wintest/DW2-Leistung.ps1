# DW2-PC-Port: Leistungsmessung. Start: DW2-Leistung.bat (Doppelklick) im Ordner DW2-PC.
# Startet dw2.exe mit DW2_PERF_LOG: eine Zeile pro Bild des Spiel-Threads (Bildzeit, Zeichnen,
# Laufzeit-Schrift-Kacheln, GPU-Uploads). Nach dem Beenden: Zusammenfassung in test_out\leistung.txt.
# Die Dateien enthalten nur Zahlen, keine Spieldaten.
$ErrorActionPreference = "Continue"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $here
$out = Join-Path $here "test_out"
New-Item -ItemType Directory -Force $out | Out-Null
$csv = Join-Path $out "leistung.csv"
$rep = Join-Path $out "leistung.txt"
if (Test-Path $csv) { Remove-Item -Force $csv }
if (Test-Path $rep) { Remove-Item -Force $rep }
function Log([string]$s) { Write-Host $s; Add-Content -Path $rep -Value $s -Encoding UTF8 }

Write-Host "Das Spiel startet. Bitte normal spielen (z. B. einen Kampf mit mehreren Attacken) und dann beenden."
$env:DW2_PERF_LOG = $csv
$p = Start-Process -FilePath (Join-Path $here "dw2.exe") -PassThru -Wait
Remove-Item Env:\DW2_PERF_LOG

Log "DW2-PC-Port Leistungsmessung, $(Get-Date -Format 'yyyy-MM-dd HH:mm')"
try {
    Get-CimInstance Win32_VideoController | ForEach-Object { Log "GPU: $($_.Name), Treiber $($_.DriverVersion), $($_.CurrentHorizontalResolution)x$($_.CurrentVerticalResolution)" }
    Get-CimInstance Win32_Processor | ForEach-Object { Log "CPU: $($_.Name)" }
} catch { }
if (-not (Test-Path $csv)) { Log "Keine Messdatei (das Spiel hat nicht gestartet?)"; exit 1 }
$rows = Import-Csv $csv
if ($rows.Count -lt 2) { Log "Zu wenige Bilder gemessen"; exit 1 }
$inv = [Globalization.CultureInfo]::InvariantCulture
function N($v) { [double]::Parse($v, $inv) }
$ft = $rows | ForEach-Object { N $_.frame_ms } | Sort-Object
function Q($q) { $ft[[int][Math]::Floor(($ft.Count - 1) * $q)] }
$late = ($rows | Where-Object { [int]$_.vblanks -ge 2 }).Count
Log ("Bilder (VBlank-Wartepunkte): {0}, Dauer {1:N1} s" -f $rows.Count, ((N $rows[-1].t_ms) - (N $rows[0].t_ms)) / 1000)
Log ("Bildzeit ms: Median {0:N2}, 95 % {1:N2}, 99 % {2:N2}, Maximum {3:N2}" -f (Q 0.5), (Q 0.95), (Q 0.99), $ft[-1])
Log ("Wartepunkte mit mehr als einem faelligen VBlank (Spiel-Thread zu spaet): {0}" -f $late)
$sum = @{}
foreach ($k in "flush_ms","publish_ms","raster_ms","conv_ms","style_ms","gpu_up_ms","sleep_ms") { $sum[$k] = ($rows | ForEach-Object { N $_.$k } | Measure-Object -Sum).Sum }
foreach ($k in "new_tiles","new_kpx","hits","full","drops","gpu_ups","gpu_kib","gpu_deferred") { $sum[$k] = ($rows | ForEach-Object { [double]$_.$k } | Measure-Object -Sum).Sum }
$maxtile = ($rows | ForEach-Object { [double]$_.tile_kib } | Measure-Object -Maximum).Maximum
Log ("Zeichnen (Flush) gesamt {0:N0} ms, Bild ausgeben {1:N0} ms, Warten auf VBlank {2:N0} ms" -f $sum.flush_ms, $sum.publish_ms, $sum.sleep_ms)
Log ("Schrift-Kacheln: {0} neu ({1:N1} Mio. Pixel), {2} aus dem Speicher; Rastern {3:N0} ms, Umrechnen {4:N0} ms, Panel-Stil {5:N0} ms" -f $sum.new_tiles, ($sum.new_kpx / 1000), $sum.hits, $sum.raster_ms, $sum.conv_ms, $sum.style_ms)
Log ("Kachelspeicher maximal {0:N1} MiB; voll: {1}, komplett geleert: {2}" -f ($maxtile / 1024), $sum.full, $sum.drops)
Log ("GPU-Uploads: {0} ({1:N1} MiB, {2:N0} ms); zurueckgestellt/abgelehnt: {3}" -f $sum.gpu_ups, ($sum.gpu_kib / 1024), $sum.gpu_up_ms, $sum.gpu_deferred)
Log ""
Log "Die 15 langsamsten Bilder (Bild, ms gesamt, Flush, neue Kacheln, Rastern ms, GPU-Uploads KiB, faellige VBlanks):"
$rows | Sort-Object { - (N $_.frame_ms) } | Select-Object -First 15 | ForEach-Object {
    Log ("  {0,6}  {1,8:N2}  {2,8:N2}  {3,4}  {4,7:N2}  {5,7}  {6}" -f $_.frame, (N $_.frame_ms), (N $_.flush_ms), $_.new_tiles, (N $_.raster_ms), $_.gpu_kib, $_.vblanks)
}
Log ""
Log "Bitte test_out\leistung.txt und test_out\leistung.csv an den Entwickler schicken."
