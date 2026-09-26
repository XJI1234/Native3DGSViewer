param(
    [switch]$SkipBuild,
    [string]$MSBuildPath,
    [string]$IsccPath
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$release = Join-Path $repo 'out\Release'
$outRoot = Join-Path $repo 'out'
$stage = Join-Path $outRoot 'installer-stage'
$installer = Join-Path $outRoot 'installer'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer (vswhere.exe) is required.' }
$vsRoot = (& $vswhere -latest -requires Microsoft.Component.MSBuild -property installationPath | Select-Object -First 1)
if (-not $vsRoot) { throw 'Visual Studio with MSBuild was not found.' }

if (-not $SkipBuild) {
    if (-not $MSBuildPath) { $MSBuildPath = Join-Path $vsRoot 'MSBuild\Current\Bin\MSBuild.exe' }
    if (-not (Test-Path -LiteralPath $MSBuildPath)) { throw "MSBuild not found: $MSBuildPath" }
    & $MSBuildPath (Join-Path $repo 'Native3DGSViewer.sln') /restore /m /p:Configuration=Release /p:Platform=x64
    if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
}

$shaders = @('count', 'reduce', 'scan', 'scan_add', 'scatter', 'reset_args',
             'project', 'vertex', 'pixel') | ForEach-Object { "shaders\$_.cso" }
foreach ($required in (@('Native3DGSViewer.GUI.exe', 'model-io-helper.exe', 'App.xbf',
                         'MainWindow.xbf', 'Native3DGSViewer.GUI.pri') + $shaders)) {
    if (-not (Test-Path -LiteralPath (Join-Path $release $required))) {
        throw "Missing release file: $required"
    }
}

$resolvedOut = (Resolve-Path -LiteralPath $outRoot).Path.TrimEnd('\')
$resolvedStage = [System.IO.Path]::GetFullPath($stage).TrimEnd('\')
if (-not $resolvedStage.StartsWith($resolvedOut + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Installer staging path is outside the build output.'
}
if (Test-Path -LiteralPath $resolvedStage) {
    Remove-Item -LiteralPath $resolvedStage -Recurse -Force
}
New-Item -ItemType Directory -Path $resolvedStage, $installer -Force | Out-Null

foreach ($name in @('Native3DGSViewer.GUI.exe', 'model-io-helper.exe')) {
    Copy-Item -LiteralPath (Join-Path $release $name) -Destination $resolvedStage
}
Get-ChildItem -LiteralPath $release -File |
    Where-Object { $_.Extension -in @('.dll', '.pri', '.xbf', '.json') -and
                   $_.Name -ne 'Native3DGSViewer.Tests.dll' } |
    ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $resolvedStage }
Get-ChildItem -LiteralPath $release -Directory |
    Where-Object { $_.Name -ne 'out' } |
    ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $resolvedStage -Recurse }

$crt = Get-ChildItem -LiteralPath (Join-Path $vsRoot 'VC\Redist\MSVC') -Directory |
    Where-Object { $_.Name -match '^\d+\.\d+\.' } |
    Sort-Object { [version]$_.Name } -Descending |
    ForEach-Object { Join-Path $_.FullName 'x64\Microsoft.VC145.CRT' } |
    Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1
if (-not $crt) { throw 'Visual C++ x64 runtime files were not found.' }
Copy-Item -Path (Join-Path $crt '*.dll') -Destination $resolvedStage

$licenses = Join-Path $resolvedStage 'licenses'
New-Item -ItemType Directory -Path $licenses | Out-Null
$notices = @{
    'miniply-LICENSE.md' = 'third_party\miniply\LICENSE.md'
    'spz-LICENSE' = 'third_party\spz\LICENSE'
    'zlib-LICENSE' = 'third_party\zlib\LICENSE'
    'zstd-LICENSE' = 'third_party\zstd\LICENSE'
    'parallel-sort-LICENSE.txt' = 'third_party\parallel-sort\LICENSE.txt'
    'dependency-manifest.md' = 'third_party\README.md'
}
foreach ($entry in $notices.GetEnumerator()) {
    $source = Join-Path $repo $entry.Value
    if (-not (Test-Path -LiteralPath $source)) { throw "Missing license notice: $source" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $licenses $entry.Key)
}
$windowsAppSdk = Join-Path $env:USERPROFILE '.nuget\packages\microsoft.windowsappsdk\1.8.260921001'
foreach ($name in @('license.txt', 'NOTICE.txt')) {
    $source = Join-Path $windowsAppSdk $name
    if (-not (Test-Path -LiteralPath $source)) { throw "Missing Windows App SDK notice: $source" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $licenses "WindowsAppSDK-$name")
}

if (-not $IsccPath) {
    $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($command) { $IsccPath = $command.Source }
    else {
        $IsccPath = Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'
    }
}
if (-not (Test-Path -LiteralPath $IsccPath)) { throw "Inno Setup compiler not found: $IsccPath" }
& $IsccPath "/DStageDir=$resolvedStage" "/DOutputDir=$installer" (Join-Path $PSScriptRoot 'viewer.iss')
if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }
Get-Item -LiteralPath (Join-Path $installer 'Native3DGSViewer-Setup-x64.exe')
