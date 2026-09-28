param(
    [Parameter(Mandatory)][string]$Csv,
    [string]$DisplayCsv
)
$ErrorActionPreference = 'Stop'
$rows = @(Import-Csv -LiteralPath $Csv | Where-Object phase -eq 'sample')
if ($rows.Count -lt 2) { throw 'No sampled frame intervals.' }
$intervals = [System.Collections.Generic.List[double]]::new()
for ($i = 1; $i -lt $rows.Count; $i++) {
    $gap = ([double]$rows[$i].present_call_ns - [double]$rows[$i - 1].present_call_ns) / 1e6
    if ($gap -gt 0) { $intervals.Add($gap) }
}
$ordered = @($intervals | Sort-Object)
$frameCoverage = ([double]$rows[-1].present_call_ns -
    [double]$rows[0].present_call_ns) / 1e9
$largestFrameGap = if ($ordered.Count) { $ordered[-1] } else { $null }
$frameGateEligible = $frameCoverage -ge 59 -and
    $null -ne $largestFrameGap -and $largestFrameGap -le 1000
$tailCount = [Math]::Max(1, [Math]::Ceiling($ordered.Count * 0.01))
$median = $ordered[[int][Math]::Floor($ordered.Count / 2)]
$tail = (@($ordered | Select-Object -Last $tailCount | Measure-Object -Average)[0]).Average
$gpu = @($rows | Where-Object { [long]$_.gpu_frame_id -gt 0 })
$displayIntervals = [System.Collections.Generic.List[double]]::new()
$displayCoverage = $null
$displaySampleCount = 0
$largestDisplayGap = $null
if ($DisplayCsv -and (Test-Path -LiteralPath $DisplayCsv)) {
    $displayRows = @(Import-Csv -LiteralPath $DisplayCsv | Where-Object phase -eq 'sample' |
        Sort-Object { [long]$_.display_present_ns })
    $displaySampleCount = $displayRows.Count
    if ($displayRows.Count -ge 2) {
        $displayCoverage = ([double]$displayRows[-1].display_present_ns -
            [double]$displayRows[0].display_present_ns) / 1e9
        for ($i = 1; $i -lt $displayRows.Count; $i++) {
            $gap = ([double]$displayRows[$i].display_present_ns -
                [double]$displayRows[$i - 1].display_present_ns) / 1e6
            if ($gap -gt 0) { $displayIntervals.Add($gap) }
        }
    }
}
$displayOrdered = @($displayIntervals | Sort-Object)
$largestDisplayGap = if ($displayOrdered.Count) { $displayOrdered[-1] } else { $null }
$displayTailCount = [Math]::Max(1, [Math]::Ceiling($displayOrdered.Count * 0.01))
$displayMedian = if ($displayOrdered.Count) {
    $displayOrdered[[int][Math]::Floor($displayOrdered.Count / 2)]
} else { $null }
$displayTail = if ($displayOrdered.Count) {
    (@($displayOrdered | Select-Object -Last $displayTailCount | Measure-Object -Average)[0]).Average
} else { $null }
$displayCountConsistent = $displaySampleCount -le $rows.Count + 5
[pscustomobject]@{
    frames = $rows.Count
    frame_sample_coverage_seconds = [Math]::Round($frameCoverage, 2)
    frame_sample_max_gap_ms = if ($null -ne $largestFrameGap) {
        [Math]::Round($largestFrameGap, 2)
    } else { $null }
    frame_sample_gate_eligible = $frameGateEligible
    present_call_median_ms = [Math]::Round($median, 2)
    present_call_1pct_low_fps = [Math]::Round(1000 / $tail, 2)
    gpu_project_mean_us = if ($gpu.Count) { [Math]::Round(($gpu | Measure-Object gpu_project_us -Average).Average) } else { $null }
    gpu_sort_mean_us = if ($gpu.Count) { [Math]::Round(($gpu | Measure-Object gpu_sort_us -Average).Average) } else { $null }
    gpu_draw_mean_us = if ($gpu.Count) { [Math]::Round(($gpu | Measure-Object gpu_draw_us -Average).Average) } else { $null }
    display_samples = $displaySampleCount
    display_count_consistent = $displayCountConsistent
    display_coverage_seconds = if ($null -ne $displayCoverage) { [Math]::Round($displayCoverage, 2) } else { $null }
    display_max_gap_ms = if ($null -ne $largestDisplayGap) { [Math]::Round($largestDisplayGap, 2) } else { $null }
    display_median_ms = if ($null -ne $displayMedian) { [Math]::Round($displayMedian, 2) } else { $null }
    display_1pct_low_fps = if ($null -ne $displayTail) { [Math]::Round(1000 / $displayTail, 2) } else { $null }
    display_gate_eligible = $frameGateEligible -and $displayCountConsistent -and
        $null -ne $displayCoverage -and $displayCoverage -ge 59 -and
        $null -ne $largestDisplayGap -and $largestDisplayGap -le 1000
    note = 'Present-call intervals are diagnostic. Eligibility requires both streams to span 59 seconds, no gap over one second, and no excess display samples.'
}
