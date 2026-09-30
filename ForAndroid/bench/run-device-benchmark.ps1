param(
    [Parameter(Mandatory)][string]$Model,
    [string]$Serial,
    [string]$RunName = 'run-01',
    [ValidateSet('full', 'mobile')][string]$Quality = 'full',
    [ValidateRange(0, 1000)][int]$ScalePermille = 0,
    [string]$OutputDirectory = 'out/android-bench'
)
$ErrorActionPreference = 'Stop'
if ($ScalePermille -ne 0 -and ($Quality -ne 'mobile' -or $ScalePermille -lt 750)) {
    throw 'A fixed scale of 750-1000 requires mobile quality.'
}
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$modelPath = (Resolve-Path -LiteralPath $Model).Path
$modelName = [IO.Path]::GetFileName($modelPath)
if ($modelName -notmatch '^[A-Za-z0-9_.-]+\.(ply|spz)$') {
    throw 'Use an ASCII PLY/SPZ sample filename without spaces.'
}
if ($RunName -notmatch '^[A-Za-z0-9_-]+$') { throw 'Invalid run name.' }
$destination = [IO.Path]::GetFullPath((Join-Path $repo $OutputDirectory))
New-Item -ItemType Directory -Force -Path $destination | Out-Null

if (-not $Serial) {
    $devices = @(& adb devices | Select-String '^\S+\s+device$' | ForEach-Object {
        ($_.Line -split '\s+')[0]
    })
    if ($devices.Count -ne 1) { throw 'Specify -Serial when zero or multiple ADB devices are connected.' }
    $Serial = $devices[0]
}
function Invoke-Adb {
    param([string[]]$Arguments)
    $result = & adb -s $Serial @Arguments
    if ($LASTEXITCODE -ne 0) { throw "adb failed: $($Arguments -join ' ')" }
    return $result
}
if (-not (Invoke-Adb -Arguments @('shell', 'pm', 'path', 'org.native3dgs.viewer'))) {
    throw 'Install Native3DGSViewer-benchmark.apk on the tablet first.'
}
$packagePath = ((Invoke-Adb -Arguments @('shell', 'pm', 'path',
    'org.native3dgs.viewer') | Select-Object -First 1) -replace '^package:', '').Trim()
$apkHash = ((Invoke-Adb -Arguments @('shell', 'sha256sum', $packagePath) |
    Select-Object -First 1) -split '\s+')[0]
$deviceFiles = '/sdcard/Android/data/org.native3dgs.viewer/files'
$deviceTrace = "/data/misc/perfetto-traces/native3dgs-$RunName.pftrace"
$deviceCsv = "$deviceFiles/$RunName.csv"
Invoke-Adb -Arguments @('shell', 'mkdir', '-p', $deviceFiles) | Out-Null
Invoke-Adb -Arguments @('push', $modelPath, "$deviceFiles/$modelName") | Out-Null
$metadata = [ordered]@{
    serial = $Serial
    utc = (Get-Date).ToUniversalTime().ToString('o')
    sample = $modelName
    quality = $Quality
    fixed_scale_permille = $ScalePermille
    sample_sha256 = (Get-FileHash -LiteralPath $modelPath -Algorithm SHA256).Hash
    installed_apk_sha256 = $apkHash
    sample_bytes = (Get-Item -LiteralPath $modelPath).Length
    model = (Invoke-Adb -Arguments @('shell', 'getprop', 'ro.product.model'))
    abi = (Invoke-Adb -Arguments @('shell', 'getprop', 'ro.product.cpu.abi'))
    platform = (Invoke-Adb -Arguments @('shell', 'getprop', 'ro.board.platform'))
    android = (Invoke-Adb -Arguments @('shell', 'getprop', 'ro.build.version.release'))
    fingerprint = (Invoke-Adb -Arguments @('shell', 'getprop', 'ro.build.fingerprint'))
    display = (Invoke-Adb -Arguments @('shell', 'wm', 'size'))
    thermal_start = (Invoke-Adb -Arguments @('shell', 'dumpsys', 'thermalservice') | Select-String 'Thermal Status:' | Select-Object -First 1).Line.Trim()
}
$metadata | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $destination "$RunName-device.json")
Invoke-Adb -Arguments @('shell', 'rm', '-f', $deviceCsv, $deviceTrace) | Out-Null
Invoke-Adb -Arguments @('shell', 'am', 'force-stop', 'org.native3dgs.viewer') | Out-Null
$pidText = Get-Content (Join-Path $PSScriptRoot 'perfetto.cfg') -Raw |
    & adb -s $Serial shell perfetto --background-wait --txt -c - -o $deviceTrace
if ($LASTEXITCODE -ne 0) { throw 'Perfetto failed to start.' }
$tracePid = [int](([string]($pidText | Select-Object -Last 1)).Trim())
$displayFrames = [System.Collections.Generic.Dictionary[long, string]]::new()
$surfaceLayer = $null
try {
    Invoke-Adb -Arguments @('shell', 'am', 'start', '-n',
        'org.native3dgs.viewer/.BenchmarkActivity', '--es', 'model', $modelName,
        '--es', 'output', "$RunName.csv", '--es', 'quality', $Quality,
        '--ei', 'scale_permille', [string]$ScalePermille) | Out-Null
    $deadline = (Get-Date).AddMinutes(5)
    do {
        Start-Sleep -Seconds 2
        if (-not $surfaceLayer) {
            $layers = & adb -s $Serial shell dumpsys SurfaceFlinger --list 2>$null
            if ($LASTEXITCODE -eq 0) {
                $surfaceLayer = @($layers | ForEach-Object {
                    if ($_ -match '(SurfaceView\[org\.native3dgs\.viewer/org\.native3dgs\.viewer\.BenchmarkActivity\]\(BLAST\)#\d+)') {
                        $Matches[1]
                    }
                } | Select-Object -Last 1)[0]
            }
        }
        if ($surfaceLayer) {
            $latency = & adb -s $Serial shell "dumpsys SurfaceFlinger --latency '$surfaceLayer'" 2>$null
            if ($LASTEXITCODE -eq 0) {
                foreach ($line in @($latency | Select-Object -Skip 1)) {
                    $columns = $line.Trim() -split '\s+'
                    $presentNs = 0L
                    if ($columns.Count -ge 3 -and
                        [long]::TryParse($columns[1], [ref]$presentNs) -and
                        $presentNs -gt 0 -and $presentNs -lt 1000000000000000000L -and
                        -not $displayFrames.ContainsKey($presentNs)) {
                        $displayFrames.Add($presentNs, ($columns[0..2] -join ','))
                    }
                }
            }
        }
        $exists = & adb -s $Serial shell "if test -f '$deviceCsv'; then echo ready; fi" 2>$null
        if ($LASTEXITCODE -ne 0) { $exists = $null }
    } until ($exists -match 'ready' -or (Get-Date) -gt $deadline)
    if ($exists -notmatch 'ready') { throw 'Benchmark CSV was not produced within five minutes.' }
    Invoke-Adb -Arguments @('pull', $deviceCsv, (Join-Path $destination "$RunName.csv")) | Out-Null
    $sampleRows = @(Import-Csv -LiteralPath (Join-Path $destination "$RunName.csv") |
        Where-Object phase -eq 'sample')
    if ($sampleRows.Count -lt 2 -or
        @($sampleRows | Where-Object { $_.quality_mode -ne $Quality }).Count -gt 0 -or
        @($sampleRows | Where-Object { -not $_.actual_scale_permille }).Count -gt 0) {
        throw 'Benchmark CSV lacks the requested quality mode or v2 scale fields; check the installed APK.'
    }
    if ($ScalePermille -ne 0 -and
        @($sampleRows | Where-Object { [int]$_.requested_scale_permille -ne $ScalePermille }).Count -gt 0) {
        throw 'Benchmark CSV does not match the requested fixed scale.'
    }
    Invoke-Adb -Arguments @('pull', "$deviceFiles/$RunName-thermal.csv",
        (Join-Path $destination "$RunName-thermal.csv")) | Out-Null
    if ($displayFrames.Count -gt 0) {
        $timestamps = @($displayFrames.Keys | Sort-Object)
        $sampleStart = [long]((Import-Csv -LiteralPath (Join-Path $destination "$RunName.csv") |
            Where-Object phase -eq 'sample' | Select-Object -First 1).present_call_ns)
        $displayCsv = Join-Path $destination "$RunName-display.csv"
        @('desired_present_ns,display_present_ns,frame_ready_ns,phase') + @(
            foreach ($timestamp in $timestamps) {
                $phase = if ($timestamp -lt $sampleStart) { 'warmup' } else { 'sample' }
                "$($displayFrames[$timestamp]),$phase"
            }
        ) | Set-Content -LiteralPath $displayCsv
    }
} finally {
    & adb -s $Serial shell kill -TERM $tracePid | Out-Null
    Start-Sleep -Seconds 3
    & adb -s $Serial pull $deviceTrace (Join-Path $destination "$RunName.pftrace") | Out-Null
}
$summaryArguments = @{ Csv = (Join-Path $destination "$RunName.csv") }
$displayCsv = Join-Path $destination "$RunName-display.csv"
if (Test-Path -LiteralPath $displayCsv) { $summaryArguments.DisplayCsv = $displayCsv }
& (Join-Path $PSScriptRoot 'summarize-device-benchmark.ps1') @summaryArguments
