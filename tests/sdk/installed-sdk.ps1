param(
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [string]$Configuration = 'Release',
    [string]$Scene,
    [string]$PackageArchive,
    [ValidateSet('true', 'false')][string]$ExpectedCudaActive
)
$ErrorActionPreference = 'Stop'
$consumerRoot = Join-Path ([IO.Path]::GetTempPath()) ('Native3DGS-consumer-' + [Guid]::NewGuid().ToString('N'))
$stage = Join-Path $consumerRoot 'stage'
$package = Join-Path $consumerRoot 'relocated-sdk'
try {
    if ($PackageArchive) {
        Expand-Archive -LiteralPath $PackageArchive -DestinationPath $stage
    } else {
        & cmake --install $BuildDirectory --config $Configuration --prefix $stage --component SDK
        if ($LASTEXITCODE -ne 0) { throw 'SDK install failed' }
    }
    Move-Item -LiteralPath $stage -Destination $package
    & cmake -S (Join-Path $package 'examples/sdk-host') -B (Join-Path $consumerRoot 'build') -A x64 "-DCMAKE_PREFIX_PATH=$package"
    if ($LASTEXITCODE -ne 0) { throw 'Consumer configure failed' }
    & cmake --build (Join-Path $consumerRoot 'build') --config $Configuration --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Consumer build failed' }
    $hostExecutable = Join-Path $consumerRoot "build/$Configuration/sdk-host.exe"
    if (Test-Path -LiteralPath (Join-Path $package 'bin/gs-cuda-backend.dll')) {
        foreach ($runtimeFile in @('gs-cuda-backend.dll', 'licenses/cuda/cudart-LICENSE', 'licenses/cuda/cccl-LICENSE')) {
            if (-not (Test-Path -LiteralPath (Join-Path (Split-Path $hostExecutable) $runtimeFile))) {
                throw "Optional CUDA runtime was not deployed: $runtimeFile"
            }
        }
    }
    if ($Scene) {
        $metricPath = Join-Path $consumerRoot 'arbitrary.metric.json'
        @'
{"schema_version":1,"model":{"sha256":"deliberately-unrelated"},
"frame":{"source":"ply_local","target":"local_enu","units":"meters",
"matrix_convention":"row_serialized_column_vector_left_multiply"},
"quality":{"status":"estimated"},"meters_per_model_unit":2,
"enu_from_ply":[[2,0,0,0],[0,2,0,0],[0,0,2,0],[0,0,0,1]]}
'@ | Set-Content -LiteralPath $metricPath -Encoding utf8NoBOM
        $hostOutput = @(& $hostExecutable $Scene $metricPath)
    } else { $hostOutput = @(& $hostExecutable) }
    $hostExitCode = $LASTEXITCODE
    $hostOutput | Write-Output
    if ($hostExitCode -ne 0) { throw 'Installed consumer run failed' }
    if ($ExpectedCudaActive -and ($hostOutput -join "`n") -notmatch "cuda_sort_active=$ExpectedCudaActive(?:\s|$)") {
        throw "Installed consumer did not report expected CUDA=$ExpectedCudaActive"
    }
    Write-Output "Independent relocated SDK consumer passed: $consumerRoot"
} finally {
    if (Test-Path -LiteralPath $consumerRoot) {
        $resolvedConsumer = (Resolve-Path -LiteralPath $consumerRoot).Path
        $expectedParent = (Resolve-Path -LiteralPath ([IO.Path]::GetTempPath())).Path.TrimEnd('\')
        if (-not [String]::Equals((Split-Path $resolvedConsumer -Parent), $expectedParent, [StringComparison]::OrdinalIgnoreCase) -or
            (Split-Path $resolvedConsumer -Leaf) -notlike 'Native3DGS-consumer-*') {
            throw 'Unexpected consumer cleanup path'
        }
        Remove-Item -LiteralPath $resolvedConsumer -Recurse -Force
    }
}
