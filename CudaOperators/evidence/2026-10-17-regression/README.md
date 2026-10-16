# Production configuration regression

The full `Native3DGSViewer.sln` Release x64 build completed successfully, including the WinUI host and native tests: zero errors, two existing NuGet restore warnings (no restorable projects). Exact output is in `msbuild-release.log`.

`out/cmake/CMakeCache.txt` has `GS_ENABLE_CUDA_OPERATORS:BOOL=OFF`. `ctest --test-dir out/cmake -C Release --output-on-failure` passed 5/5 groups in 85.88 seconds: ModelIo, RenderCore, ModelRenderIntegration, Engine and InstalledSDK. The SDK group installs and builds/runs the external consumer. Output is in `native-ctest.log`.

The optional CUDA tree separately passed 8/8 groups; see the adjacent hlsl-tile directory. These runs finished before formal GPU timing started. Formal timing has no concurrent build, sanitizer or other project GPU test.

This is build/automated regression evidence. No manual WinUI session, installer exercise, injected device removal, 100 GUI resize/cancel cycles or two-hour production soak was performed in this slice. No CUDA renderer backend is enabled or installed into the public SDK.
