param([string]$Version = '0.2.2')
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$android = Join-Path $repo 'ForAndroid'
$outRoot = [IO.Path]::GetFullPath((Join-Path $repo 'out/android-sdk'))
$package = [IO.Path]::GetFullPath((Join-Path $outRoot "Native3DGS-SDK-$Version-Android-arm64-v8a"))
if (-not $package.StartsWith($outRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Package output escapes out/android-sdk'
}
if (-not $env:ANDROID_HOME) { throw 'ANDROID_HOME is required' }
$ndk = Join-Path $env:ANDROID_HOME 'ndk/27.2.12479018'
if (-not (Test-Path -LiteralPath $ndk)) { throw 'NDK 27.2.12479018 is required' }

& (Join-Path $android 'gradlew.bat') -p $android :sdk:assembleRelease
if ($LASTEXITCODE -ne 0) { throw 'Release AAR build failed' }
& cmake -S $android -B (Join-Path $repo 'out/android-arm64') -G Ninja `
    "-DCMAKE_TOOLCHAIN_FILE=$ndk/build/cmake/android.toolchain.cmake" `
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'arm64 CMake configure failed' }
& cmake --build (Join-Path $repo 'out/android-arm64') --target gs_android_shaders
if ($LASTEXITCODE -ne 0) { throw 'Shader build failed' }

if (Test-Path -LiteralPath $package) { Remove-Item -LiteralPath $package -Recurse -Force }
New-Item -ItemType Directory -Force -Path $package | Out-Null
$aar = Join-Path $android 'sdk/build/outputs/aar/sdk-release.aar'
if (-not (Test-Path -LiteralPath $aar)) { throw 'Release AAR missing' }
$extract = Join-Path $outRoot '_aar-extract'
if (-not ([IO.Path]::GetFullPath($extract)).StartsWith($outRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Extraction output escapes out/android-sdk'
}
if (Test-Path -LiteralPath $extract) { Remove-Item -LiteralPath $extract -Recurse -Force }
New-Item -ItemType Directory -Force -Path $extract | Out-Null
& tar -xf $aar -C $extract
if ($LASTEXITCODE -ne 0) { throw 'AAR extraction failed' }
$native = Join-Path $extract 'jni/arm64-v8a/libgs_android_decoder.so'
$stdlib = Join-Path $extract 'jni/arm64-v8a/libc++_shared.so'
if (-not (Test-Path -LiteralPath $native) -or -not (Test-Path -LiteralPath $stdlib)) {
    throw 'Required native libraries missing from AAR'
}
New-Item -ItemType Directory -Force -Path (Join-Path $package 'aar'),
    (Join-Path $package 'lib/arm64-v8a'), (Join-Path $package 'include/native3dgs'),
    (Join-Path $package 'cmake'), (Join-Path $package 'shaders'),
    (Join-Path $package 'licenses') | Out-Null
Copy-Item -LiteralPath $aar -Destination (Join-Path $package 'aar/native3dgs-sdk-release.aar')
Copy-Item -LiteralPath $native,$stdlib -Destination (Join-Path $package 'lib/arm64-v8a')
Copy-Item -LiteralPath (Join-Path $android 'include/native3dgs/android_engine.h') `
    -Destination (Join-Path $package 'include/native3dgs')
Copy-Item -LiteralPath (Join-Path $android 'packaging/Native3DGSAndroidConfig.cmake') `
    -Destination (Join-Path $package 'cmake')
Copy-Item -Path (Join-Path $repo 'out/android-arm64/shaders/*') `
    -Destination (Join-Path $package 'shaders')
Copy-Item -LiteralPath (Join-Path $android 'docs') -Destination $package -Recurse
Copy-Item -LiteralPath (Join-Path $android 'examples') -Destination $package -Recurse
$consumer = Join-Path $package 'examples/kotlin-consumer'
Copy-Item -LiteralPath (Join-Path $android 'gradlew'),
    (Join-Path $android 'gradlew.bat') -Destination $consumer
New-Item -ItemType Directory -Force -Path (Join-Path $consumer 'gradle') | Out-Null
Copy-Item -LiteralPath (Join-Path $android 'gradle/wrapper') `
    -Destination (Join-Path $consumer 'gradle') -Recurse
Copy-Item -LiteralPath (Join-Path $android 'README.md') -Destination $package
Copy-Item -LiteralPath (Join-Path $repo 'third_party/spz/LICENSE') `
    -Destination (Join-Path $package 'licenses/spz-LICENSE')
Copy-Item -LiteralPath (Join-Path $repo 'third_party/zstd/LICENSE') `
    -Destination (Join-Path $package 'licenses/zstd-LICENSE')
Copy-Item -LiteralPath (Join-Path $ndk 'NOTICE.toolchain') `
    -Destination (Join-Path $package 'licenses/android-ndk-NOTICE.toolchain')

$entries = Get-ChildItem -LiteralPath $package -File -Recurse | ForEach-Object {
    @{ path = $_.FullName.Substring($package.Length).TrimStart('\','/').Replace('\','/');
       sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
}
@{ version = $Version; api = 2; abi = @('arm64-v8a'); min_sdk = 29;
   ndk = '27.2.12479018'; files = @($entries) } |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $package 'manifest.json') -Encoding utf8
$zip = "$package.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -LiteralPath $package -DestinationPath $zip -CompressionLevel Optimal
Remove-Item -LiteralPath $extract -Recurse -Force
Write-Output $zip
