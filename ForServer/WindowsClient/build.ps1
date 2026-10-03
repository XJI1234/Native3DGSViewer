param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$cmake=(Get-Command cmake.exe -ErrorAction Stop).Source
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$visualStudio=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$msbuild=Join-Path $visualStudio 'MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuild)) { throw 'MSBuild/VC tools not found' }
& $cmake -S (Join-Path $root 'ForServer') -B (Join-Path $root 'out\server-windows') -G 'Visual Studio 18 2026' -A x64 -DGS_SERVER_CUDA=OFF
if ($LASTEXITCODE) { throw 'CMake configuration failed' }
& $cmake --build (Join-Path $root 'out\server-windows') --config $Configuration --parallel 6
if ($LASTEXITCODE) { throw 'Image transport build failed' }
& $msbuild (Join-Path $PSScriptRoot 'Native3DGSCloud.vcxproj') /restore /m /v:minimal /p:Configuration=$Configuration /p:Platform=x64
if ($LASTEXITCODE) { throw 'WinUI build failed' }
[System.IO.File]::SetLastWriteTimeUtc((Join-Path $PSScriptRoot 'MainWindow.xaml.cpp'), [DateTime]::UtcNow)
& $msbuild (Join-Path $PSScriptRoot 'Native3DGSCloud.vcxproj') /m /v:minimal /p:Configuration=$Configuration /p:Platform=x64
if ($LASTEXITCODE) { throw 'WinUI generated implementation build failed' }
Write-Output ('Client: '+(Join-Path $root "out\cloud-client\$Configuration\Native3DGSCloud.exe"))

$licenses=Join-Path $root "out\cloud-client\$Configuration\licenses"
New-Item -ItemType Directory -Force -Path $licenses | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'third_party\zstd\LICENSE') -Destination (Join-Path $licenses 'zstd-LICENSE')
