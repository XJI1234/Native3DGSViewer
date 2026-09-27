param(
    [string]$Serial = 'emulator-5560',
    [string]$BuildDirectory = 'out/android-x86_64'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$buildPath = [IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDirectory))
$deviceDirectory = '/data/local/tmp/native3dgs-tests'
$programs = @('gs_android_model_io_tests', 'gs_android_render_core_tests',
    'gs_android_core_tests', 'gs_android_engine_tests')

function Invoke-Adb {
    param([string[]]$Arguments)
    & adb -s $Serial @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "adb failed with exit code $LASTEXITCODE"
    }
}

Invoke-Adb -Arguments @('shell', 'mkdir', '-p', $deviceDirectory)
foreach ($program in $programs) {
    Invoke-Adb -Arguments @('push', (Join-Path $buildPath $program), "$deviceDirectory/$program")
    Invoke-Adb -Arguments @('shell', 'chmod', '755', "$deviceDirectory/$program")
}
Invoke-Adb -Arguments @('push', (Join-Path $buildPath 'shaders'), "$deviceDirectory/")
Invoke-Adb -Arguments @('shell', 'mkdir', '-p', "$deviceDirectory/samples")
foreach ($sample in @('hornedlizard.spz', 'racoonfamily.spz')) {
    Invoke-Adb -Arguments @('push', (Join-Path $repoRoot "third_party/spz/samples/$sample"),
        "$deviceDirectory/samples/$sample")
}
$resultsDirectory = Join-Path $buildPath 'test-results'
New-Item -ItemType Directory -Force -Path $resultsDirectory | Out-Null
$failed = @()
foreach ($program in $programs) {
    & adb -s $Serial @('shell', "GS_SHADER_DIRECTORY=$deviceDirectory/shaders",
        "GS_SAMPLE_DIRECTORY=$deviceDirectory/samples", "$deviceDirectory/$program",
        "--gtest_output=xml:$deviceDirectory/$program.xml")
    $testExit = $LASTEXITCODE
    & adb -s $Serial @('pull', "$deviceDirectory/$program.xml",
        (Join-Path $resultsDirectory "$program.xml"))
    if ($LASTEXITCODE -ne 0 -or $testExit -ne 0) { $failed += $program }
}
if ($failed.Count -gt 0) { throw "Device tests failed: $($failed -join ', ')" }
