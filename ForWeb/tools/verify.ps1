param([switch]$Hardware)
$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    foreach ($gate in @('build:wasm','typecheck','lint','check:boundaries','test:unit','test:contracts','build')) {
        & pnpm run $gate
        if ($LASTEXITCODE -ne 0) { throw "Failed gate: $gate" }
    }
    if ($Hardware) {
        foreach ($gate in @('test:gpu','test:images','test:integration','test:sdk')) {
            & pnpm run $gate
            if ($LASTEXITCODE -ne 0) { throw "Failed hardware gate: $gate" }
        }
    }
} finally { Pop-Location }
