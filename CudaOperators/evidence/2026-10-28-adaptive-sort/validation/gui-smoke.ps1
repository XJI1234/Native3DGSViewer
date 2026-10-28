param([string]$Model, [string]$ExpectedCuda, [string]$Output)
$ErrorActionPreference = 'Stop'
$repo = (Get-Location).Path
$viewerExe = Join-Path $repo 'out/Release/Native3DGSViewer.GUI.exe'
$taskViewer = Start-Process -FilePath $viewerExe -ArgumentList ('"' + $Model + '"') -WorkingDirectory (Split-Path $viewerExe) -WindowStyle Hidden -PassThru
$deadline = [DateTime]::UtcNow.AddSeconds(35)
$matched = $false
$runLog = $null
try {
    while ([DateTime]::UtcNow -lt $deadline -and -not $taskViewer.HasExited) {
        Start-Sleep -Milliseconds 200
        $runLog = Get-ChildItem (Join-Path (Split-Path $viewerExe) 'logs') -Filter ('*-' + $taskViewer.Id + '.log') | Select-Object -First 1
        if ($runLog) {
            $records = @(Get-Content $runLog.FullName | ForEach-Object { try { $_ | ConvertFrom-Json } catch {} })
            $sample = $records | Where-Object { $_.event -eq 'render_sample' -and $_.cuda_sort_active -eq $ExpectedCuda } | Select-Object -Last 1
            if ($sample) { $matched = $true; break }
        }
    }
} finally {
    if (-not $taskViewer.HasExited) {
        [void]$taskViewer.CloseMainWindow()
        if (-not $taskViewer.WaitForExit(10000)) { throw 'The owned test viewer did not close normally.' }
    }
    if ($runLog) { Copy-Item -LiteralPath $runLog.FullName -Destination $Output }
}
if (-not $matched) { throw "Viewer did not report expected CUDA=$ExpectedCuda for $Model" }
Write-Output "GUI model=$Model cuda=$ExpectedCuda process=$($taskViewer.Id) exit=$($taskViewer.ExitCode)"
