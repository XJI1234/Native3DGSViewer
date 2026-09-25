param(
    [string]$Configuration = 'Release',
    [int]$IntervalMs = 20
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$executable = Join-Path $root "out/$Configuration/Native3DGSViewer.Tests.exe"
if (-not (Test-Path -LiteralPath $executable)) {
    throw "Test executable not found: $executable"
}
foreach ($sample in @('1.ply', 'Viewer_android/public/scene/jidaoshan.spz',
                       'Viewer_android/public/scene/zhihuizhimen.spz')) {
    if (-not (Test-Path -LiteralPath (Join-Path (Split-Path $root -Parent) $sample))) {
        throw "External corpus sample not found: $sample"
    }
}

$start = New-Object System.Diagnostics.ProcessStartInfo
$start.FileName = $executable
$start.Arguments = '--gtest_brief=1 --gtest_filter=ModelIoCorpus.LoadsWorkspaceSamples'
$start.WorkingDirectory = $root
$start.UseShellExecute = $false
$process = [System.Diagnostics.Process]::Start($start)
$peak = @{
    HostWorkingSet = [long]0
    HostPrivateBytes = [long]0
    HostHandles = 0
    HelperWorkingSet = [long]0
    HelperPrivateBytes = [long]0
    HelperHandles = 0
}

try {
    do {
        try {
            $process.Refresh()
            if (-not $process.HasExited) {
                $peak.HostWorkingSet = [Math]::Max($peak.HostWorkingSet, $process.PeakWorkingSet64)
                $peak.HostPrivateBytes = [Math]::Max($peak.HostPrivateBytes, $process.PrivateMemorySize64)
                $peak.HostHandles = [Math]::Max($peak.HostHandles, $process.HandleCount)
            }
        } catch [System.InvalidOperationException] {
        }
        foreach ($helper in @(Get-Process -Name 'model-io-helper' -ErrorAction SilentlyContinue)) {
            try {
                $helper.Refresh()
                $peak.HelperWorkingSet = [Math]::Max($peak.HelperWorkingSet, $helper.PeakWorkingSet64)
                $peak.HelperPrivateBytes = [Math]::Max($peak.HelperPrivateBytes, $helper.PrivateMemorySize64)
                $peak.HelperHandles = [Math]::Max($peak.HelperHandles, $helper.HandleCount)
            } catch [System.InvalidOperationException] {
            } finally {
                $helper.Dispose()
            }
        }
        Start-Sleep -Milliseconds $IntervalMs
    } while (-not $process.HasExited)

    $process.WaitForExit()
    [pscustomobject]@{
        ExitCode = $process.ExitCode
        HostPeakWorkingSetMiB = [Math]::Round($peak.HostWorkingSet / 1MB, 1)
        HostMaxSampledPrivateMiB = [Math]::Round($peak.HostPrivateBytes / 1MB, 1)
        HostMaxSampledHandles = $peak.HostHandles
        HelperPeakWorkingSetMiB = [Math]::Round($peak.HelperWorkingSet / 1MB, 1)
        HelperMaxSampledPrivateMiB = [Math]::Round($peak.HelperPrivateBytes / 1MB, 1)
        HelperMaxSampledHandles = $peak.HelperHandles
    }
    if ($process.ExitCode -ne 0) {
        throw "Corpus test failed with exit code $($process.ExitCode)"
    }
} finally {
    $process.Dispose()
}
