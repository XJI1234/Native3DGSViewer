param(
    [switch]$SkipBuild,
    [string]$IsccPath
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..\..')).Path
$release = Join-Path $repo 'out\cloud-client\Release'
$outRoot = Join-Path $repo 'out'
$stage = [System.IO.Path]::GetFullPath((Join-Path $outRoot 'cloud-installer-stage')).TrimEnd('\')
$installer = Join-Path $outRoot 'cloud-installer'
if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot '..\build.ps1') -Configuration Release
}
foreach ($name in @('Native3DGSCloud.exe', 'Native3DGSCloud.pri', 'App.xbf', 'MainWindow.xbf', 'Microsoft.WindowsAppRuntime.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $release $name))) {
        throw "Missing cloud runtime: $name"
    }
}
$resolvedOut = (Resolve-Path -LiteralPath $outRoot).Path.TrimEnd('\')
if (-not $stage.StartsWith($resolvedOut + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Cloud installer staging path is outside build output.'
}
if (Test-Path -LiteralPath $stage) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}
New-Item -ItemType Directory -Path $stage, $installer -Force | Out-Null
Get-ChildItem -LiteralPath $release -File |
    Where-Object { $_.Extension -in '.exe', '.dll', '.pri', '.xbf', '.json' -and $_.Name -ne 'unins000.exe' } |
    ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $stage }
Get-ChildItem -LiteralPath $release -Directory |
    Where-Object { $_.Name -notin 'out', 'logs' } |
    ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $stage -Recurse }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = (& $vswhere -latest -products * -requires Microsoft.Component.MSBuild Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if (-not $vsRoot) { throw 'Visual Studio with runtime redistributables is required.' }
$crt = Get-ChildItem -LiteralPath (Join-Path $vsRoot 'VC\Redist\MSVC') -Directory |
    Where-Object { $_.Name -match '^\d+\.\d+\.' } |
    Sort-Object { [version]$_.Name } -Descending |
    ForEach-Object { Join-Path $_.FullName 'x64\Microsoft.VC145.CRT' } |
    Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1
if (-not $crt) { throw 'Visual C++ x64 runtime files were not found.' }
Copy-Item -Path (Join-Path $crt '*.dll') -Destination $stage
$licenses = Join-Path $stage 'licenses'
New-Item -ItemType Directory -Path $licenses -Force | Out-Null
$assetsPath = Join-Path $repo 'ForServer\WindowsClient\obj\project.assets.json'
if (-not (Test-Path -LiteralPath $assetsPath)) { throw 'NuGet restore metadata missing; build the client first.' }
$assets = Get-Content -LiteralPath $assetsPath -Raw | ConvertFrom-Json
$packageKey = $assets.libraries.PSObject.Properties.Name | Where-Object { $_ -like 'Microsoft.WindowsAppSDK/*' } | Select-Object -First 1
if (-not $packageKey) { throw 'Windows App SDK missing from restore metadata.' }
$packageRelative = $assets.libraries.$packageKey.path
$windowsAppSdk = $assets.packageFolders.PSObject.Properties.Name |
    ForEach-Object { Join-Path $_ $packageRelative } |
    Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1
if (-not $windowsAppSdk) { throw 'Restored Windows App SDK package was not found.' }
foreach ($name in 'license.txt', 'NOTICE.txt') {
    Copy-Item -LiteralPath (Join-Path $windowsAppSdk $name) -Destination (Join-Path $licenses "WindowsAppSDK-$name")
}
Copy-Item -LiteralPath (Join-Path $repo 'third_party\zstd\LICENSE') -Destination (Join-Path $licenses 'zstd-LICENSE')
if (-not $IsccPath) {
    $compiler = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    $IsccPath = if ($compiler) { $compiler.Source } else { Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe' }
}
if (-not (Test-Path -LiteralPath $IsccPath)) { throw "Inno Setup compiler not found: $IsccPath" }
& $IsccPath "/DStageDir=$stage" "/DOutputDir=$installer" (Join-Path $PSScriptRoot 'cloud-viewer.iss')
if ($LASTEXITCODE -ne 0) { throw 'Cloud installer compilation failed.' }
$artifact = Join-Path $installer 'Native3DGSCloudViewer-0.2.2-Windows-x64-Setup.exe'
Get-Item -LiteralPath $artifact
Get-FileHash -LiteralPath $artifact -Algorithm SHA256
