# Native3DGS

Windows x64 C++20 3D Gaussian Splatting engine and native SDK. Supports standard binary 3DGS PLY, SPZ v1-v4, SH 0-3, D3D12 stable GPU sorting, Gaussian projection and premultiplied composition surfaces.

The engine owns background decode/render workers, scene replacement and cancellation, camera controls, device recovery and asynchronous shutdown. A desktop host supplies its UI and DXGI composition swapchain. No desktop application is included.

## Build And Test

VS 2026/MSVC 19.50, CMake 4.3+, Windows SDK 10.0.26100.0/DXC; Windows 11 x64 and a hardware D3D12 FL12.0/SM6.0/WaveOps adapter.

```powershell
git submodule update --init --recursive
cmake -S . -B out/cmake -G "Visual Studio 18 2026" -A x64
cmake --build out/cmake --config Release --parallel 8
ctest --test-dir out/cmake -C Release --output-on-failure
```

CTest separates model IO, renderer, real sample integration, engine, and an independently built relocated SDK consumer. The three reference samples reside in the parent workspace; see the verification documents for hashes and paths. The installed consumer test uses `../1.ply`.

## SDK

```powershell
cmake --install out/cmake --config Release --prefix out/sdk --component SDK
cpack --config out/cmake/CPackConfig.cmake -C Release -B out/packages
```

Consumers use `find_package(Native3DGS CONFIG REQUIRED)`, link `Native3DGS::Engine`, and call `native3dgs_deploy_runtime(target)`. See the [integration guide](docs/SDK-guide.md), [SDK specification](docs/SPEC-engine-sdk.md), and [verification record](docs/engine-sdk-verification.md).

The package uses Release `/MD` static libraries and the matching MSVC toolchain, with a helper process, compiled shaders, headers, dependency notices and a console host example. It does not promise a compiler-independent STL ABI.

## Status

First-phase non-UI components are implemented and tested on RTX 3080. Spark SSIM, visible WinUI integration, PresentMon comparison and AMD/Intel hardware acceptance remain pending. Current stage benchmarks are diagnostic evidence and do not establish the planned 20% improvement over Viewer. See [the development plan](docs/technical-development-plan.md) for later format, LoD, multi-model and editing phases.
