# Regression for the real report writer, including Windows PowerShell 5.1's -f precedence.
$ErrorActionPreference = "Stop"
$script = Join-Path $PSScriptRoot "DW2-Leistung.ps1"
$tmp = Join-Path ([IO.Path]::GetTempPath()) ("dw2-report-" + [Guid]::NewGuid().ToString("N"))
$culture = [Threading.Thread]::CurrentThread.CurrentCulture
try {
    foreach ($name in "de-DE", "en-US") {
        [Threading.Thread]::CurrentThread.CurrentCulture = [Globalization.CultureInfo]::GetCultureInfo($name)
        $out = Join-Path $tmp $name
        New-Item -ItemType Directory -Force $out | Out-Null
        $rows = @(1,2 | ForEach-Object {
            $r = [ordered]@{}
            foreach ($key in ("frame,t_ms,frame_ms,flush_ms,publish_ms,finds,hits,new_tiles,new_kpx,full,drops,raster_ms,conv_ms,style_ms,gpu_ups,gpu_kib,gpu_up_ms,gpu_deferred,tiles,tile_kib,sleep_ms,vblanks,used_kib,mag1,mag2,mag3,mag4,submit_ms,fence_wait_ms,acquire_ms,build_ms,record_ms,submits,fence_waits,package_ms,packages,gpu,scale,smooth,late,vsync_setting,fps_limit,language,ui_style,sharpen,smooth2d,version,mailbox_pacing,window_present_ms,window_presents" -split ",")) {
                $r[$key] = "0"
            }
            $r.frame = "$_"; $r.t_ms = "$(1000 * $_).0"; $r.frame_ms = "16.683"
            $r.vblanks = "1"; $r.version = "test-fixture"; $r.mailbox_pacing = "1"
            [pscustomobject]$r
        })
        $rows | Export-Csv -LiteralPath (Join-Path $out "leistung.csv") -NoTypeInformation -Encoding ASCII
        & $script -ReportOnly -OutputDirectory $out
        $report = Get-Content -LiteralPath (Join-Path $out "leistung.txt") -Raw
        if ($report -notmatch 'Bilder \(VBlank-Wartepunkte\): 2, Dauer 1[,.]0 s') { throw "Duration/count report failed ($name)" }
        if ($report -notmatch 'Build: test-fixture') { throw "Version missing ($name)" }
        if ($report -notmatch 'Mailbox mit Fensterthread-Taktung: 2 Intervalle') { throw "Mailbox summary missing ($name)" }
        Write-Host "PASS performance report: $name"
    }
} finally {
    [Threading.Thread]::CurrentThread.CurrentCulture = $culture
    if (Test-Path $tmp) { Remove-Item -LiteralPath $tmp -Recurse -Force }
}
