param(
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [string]$Configuration = 'Release',
    [string]$Scene,
    [string]$PackageArchive
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
    if ($Scene) { & $hostExecutable $Scene } else { & $hostExecutable }
    if ($LASTEXITCODE -ne 0) { throw 'Installed consumer run failed' }
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
