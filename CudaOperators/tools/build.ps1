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
$env:CUDA_PATH = $ToolkitRoot
$env:PATH = "$ToolkitRoot/bin;$env:PATH"
$build_directory = "$project_root/out/cmake-cuda"
if ($Configure -or !(Test-Path "$build_directory/CMakeCache.txt")) {
    & cmake -S $project_root -B $build_directory -G Ninja -DCMAKE_BUILD_TYPE=Release `
        -DGS_ENABLE_CUDA_OPERATORS=ON "-DCMAKE_CUDA_COMPILER=$ToolkitRoot/bin/nvcc.exe" `
        "-DCUDAToolkit_ROOT=$ToolkitRoot"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
& cmake --build $build_directory --target @Targets --parallel 4
exit $LASTEXITCODE
