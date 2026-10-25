param(
    [string[]]$Targets = @('Native3DGSViewer.CudaProbe'),
    [switch]$Configure,
    [string]$ToolkitRoot = "$env:USERPROFILE/AppData/Local/Native3DGSViewer/toolchains/cuda/13.3.0"
)
$ErrorActionPreference = 'Stop'
$Targets = @($Targets | ForEach-Object { $_ -split ',' })
$project_root = (Resolve-Path "$PSScriptRoot/../..").Path
$developer_command = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat'
# Import the developer environment into this process only; do not print it.
$environment_lines = & cmd.exe /d /c "call `"$developer_command`" -arch=x64 -host_arch=x64 >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'VS developer environment failed' }
foreach ($entry in $environment_lines) {
    if ($entry -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}
# Prefer English diagnostics where that language pack exists. The raw-byte
# probe below handles hosts with only a localized MSVC installation.
$env:VSLANG = '1033'
$env:CUDA_PATH = $ToolkitRoot
$env:PATH = "$ToolkitRoot/bin;$env:PATH"
$build_directory = "$project_root/out/cmake-cuda"
# Generate first on every invocation: an implicit regeneration inside Ninja
# would otherwise overwrite the dependency prefix after it was calibrated.
# -Configure remains accepted for existing callers; generation is unconditional.
$configure_arguments = @('-S', $project_root, '-B', $build_directory, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release')
& cmake @configure_arguments `
    -DGS_ENABLE_CUDA_OPERATORS=ON "-DCMAKE_CUDA_COMPILER=$ToolkitRoot/bin/nvcc.exe" `
    "-DCUDAToolkit_ROOT=$ToolkitRoot"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& python "$PSScriptRoot/fix-msvc-dependencies.py" $build_directory
$prefix_status = $LASTEXITCODE
if ($prefix_status -ne 0 -and $prefix_status -ne 2) { exit $prefix_status }
$build_arguments = @('--build', $build_directory, '--target') + $Targets + @('--parallel', '4')
if ($prefix_status -eq 2) { $build_arguments += '--clean-first' }
& cmake @build_arguments
exit $LASTEXITCODE
