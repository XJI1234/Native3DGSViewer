# Native3DGS

Native3DGS is a Windows-native 3D Gaussian Splatting (3DGS) rendering engine and C++ SDK. It loads standard binary 3DGS PLY and SPZ scenes, projects and sorts splats on the GPU, and renders them through a Direct3D 12 composition swapchain. The SDK provides asynchronous scene loading, camera controls, surface management, diagnostics, and device recovery so a desktop host can focus on its user interface.

**Project status:** the engine, model I/O, renderer, tests, and installable SDK are implemented. This repository does **not** yet contain a desktop viewer or a visible WinUI window. The included console host exercises the native rendering path without displaying a window. Current validation is on an NVIDIA RTX 3080; image parity with Spark, end-to-end viewer performance, and other GPU vendors remain open acceptance work. See the [verification record](docs/engine-sdk-verification.md).

## Features

- Standard binary 3DGS PLY and SPZ v1-v4 input, with spherical harmonics (SH) degrees 0-3 and a shared immutable scene representation.
- Isolated decoder helper with format validation, resource limits, progress, cancellation, and structured errors.
- D3D12 projection, visibility filtering, stable GPU radix sorting, Gaussian rasterization, SH color, and premultiplied-alpha composition.
- Double-precision orbit and fly cameras with pan, dolly, fit-to-scene, and reset controls.
- Asynchronous load and upload, transactional scene replacement, bounded events, render statistics, and device-loss recovery coordinated with the host.
- Relocatable CMake SDK package containing static libraries, public headers, compiled shaders, decoder helper, example host, and third-party notices.

The current SDK supports one local scene at a time. Compressed PLY variants, SPLAT/KSPLAT/SOG, LoD, multiple models, editing, animation, remote loading, and the desktop UI are future work; see the [development plan](docs/technical-development-plan.md).

## Architecture and technology

```text
PLY / SPZ file
    -> model-io helper: inspect, decode, validate
    -> immutable SplatScene
    -> engine: asynchronous request, camera, upload, surface lifecycle
    -> render-core: D3D12 upload, project/cull, GPU sort, draw, Present
    -> host-owned DXGI composition swapchain
```

| Component | Technology and responsibility |
| --- | --- |
| `splat-types` | C++20 immutable scene data and shared contracts in `include/splat-types/`. |
| `model-io` | [miniply](https://github.com/vilya/miniply), [Niantic SPZ](https://github.com/nianticlabs/spz), zlib, and zstd; decoder runs in a constrained helper process. No D3D12 dependency. |
| `render-core` | Direct3D 12, DXGI, HLSL Shader Model 6.0, Windows SDK DXC, and [FidelityFX Parallel Sort](https://github.com/GPUOpen-Effects/FidelityFX-ParallelSort). No file decoder or WinUI dependency. |
| `native3dgs-engine` | C++20 camera and worker coordination, request transactions, snapshots/events, and recovery protocol. |
| SDK and tests | CMake/CPack, MSVC, GoogleTest, an independent SDK consumer, and GPU image/lifecycle tests. |

The engine uses a dedicated loading thread and render thread. The host owns its UI and DXGI composition swapchain; it supplies a swapchain created from the engine's command queue. Public entry points are `gs::engine::create_engine`, `gs::render::create_renderer`, and `gs::io::make_model_loader`. Most applications should start with the engine API. The [SDK guide](docs/SDK-guide.md) documents queue ownership, attach/detach, resizing, shutdown, and device recovery.

## Requirements

- Windows 11 x64; a hardware D3D12 adapter supporting Feature Level 12.0, Shader Model 6.0, and WaveOps. The renderer has no software fallback.
- Visual Studio 2026 with the **Desktop development with C++** workload (MSVC 19.50). Release libraries use the dynamic MSVC runtime (`/MD`); SDK consumers need a matching toolchain and VC runtime.
- Windows SDK **10.0.26100.0**, including `dxc.exe`. The current CMake build refers to this SDK's DXC path explicitly.
- CMake 4.3+ recommended (4.3.2 verified; `CMakeLists.txt` declares a 3.30 minimum), Git, and PowerShell. The build uses the `Visual Studio 18 2026` generator.
- Internet access for the first recursive submodule checkout. The pinned revisions and licenses are listed in [third_party/README.md](third_party/README.md).

The project currently builds only for Windows x64. RTX 3080 is the tested GPU; AMD, Intel, and other NVIDIA devices still need hardware validation.

## Build from source

Run these commands in PowerShell. The current engine and SDK live on `codex/engine-sdk` until that branch is merged into `main`. If you already cloned the repository, switch to that branch and run `git submodule update --init --recursive` from its root before configuring.

```powershell
git clone --recurse-submodules https://github.com/XJI1234/Native3DGSViewer.git
cd Native3DGSViewer
git switch codex/engine-sdk
cmake -S . -B out/cmake -G "Visual Studio 18 2026" -A x64
cmake --build out/cmake --config Release --parallel 8
```

Executables, the decoder helper, and compiled shaders are placed under `out/Release/`. You can also open `Native3DGSViewer.sln` in Visual Studio or build it with MSBuild:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' .\Native3DGSViewer.sln /restore /m /p:Configuration=Release /p:Platform=x64
```

The solution invokes the same CMake build. There is no desktop executable to launch yet.

## Run the example host

Install the SDK locally, build its independent consumer, then give the console host a standard 3DGS PLY or SPZ file:

```powershell
cmake --install out/cmake --config Release --prefix out/sdk --component SDK
cmake -S out/sdk/examples/sdk-host -B out/sdk-consumer -G "Visual Studio 18 2026" -A x64 "-DCMAKE_PREFIX_PATH=$((Resolve-Path out/sdk).Path)"
cmake --build out/sdk-consumer --config Release
$scene = 'C:\models\example.ply'  # Replace with an existing PLY or SPZ path.
& .\out\sdk-consumer\Release\sdk-host.exe $scene
```

The host creates a DXGI composition swapchain, loads the scene, renders frames, and exits after checking that splats reached the GPU. A successful run prints `SDK OK frames=... active=... splats=...`. It has no visible viewport; a desktop application must attach the swapchain to its own UI surface. The example source is in [examples/sdk-host](examples/sdk-host).

The reference `1.ply` and SPZ scenes used by some tests are **not distributed with this repository**. Place a compatible PLY at `..\1.ply` to run the full installed-SDK test, or use your own file with the example command above. Reference sample hashes are recorded in [model-io verification](docs/model-io-verification.md).

## Test and benchmark

The normal test suite is divided into model I/O, render core, model/render integration, engine, and installed SDK groups:

```powershell
# Runs the four in-repository groups without the external ../1.ply sample.
ctest --test-dir out/cmake -C Release --output-on-failure -E InstalledSDK

# Requires a valid standard 3DGS PLY at ../1.ply.
ctest --test-dir out/cmake -C Release --output-on-failure
```

The render and engine tests need the D3D12 hardware described above. Real-scene cases in the first four groups may skip when their external samples are absent; the model I/O suite can use additional reference SPZ files from the parent workspace when present. For the Visual Studio test bridge, run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe' .\out\Release\Native3DGSViewer.Tests.dll /Platform:x64
```

For GPU diagnostics, build Release and run the standalone benchmarks; their results are not substitutes for equal-quality viewer comparisons:

```powershell
$scene = 'C:\models\example.ply'  # Replace with an existing PLY or SPZ path.
& .\out\Release\Native3DGSViewer.SortBench.exe 8000000 100
& .\out\Release\Native3DGSViewer.SceneBench.exe $scene cached 300
& .\out\Release\Native3DGSViewer.SceneBench.exe $scene force 300
```

See [engine verification](docs/engine-sdk-verification.md) and [render-core verification](docs/render-core-verification.md) for hardware, sample hashes, test evidence, and benchmark conditions.

## Use the SDK in another project

Create a Release x64 CMake project with the matching MSVC toolchain and point `CMAKE_PREFIX_PATH` at the installed SDK (or an extracted SDK ZIP). A minimal target looks like this:

```cmake
find_package(Native3DGS 0.1 CONFIG REQUIRED)
add_executable(my-viewer main.cpp)
target_compile_features(my-viewer PRIVATE cxx_std_20)
target_link_libraries(my-viewer PRIVATE Native3DGS::Engine dxgi d3d12)
native3dgs_deploy_runtime(my-viewer)
```

`native3dgs_deploy_runtime` copies `model-io-helper.exe` and the compiled shaders next to the host executable. `Native3DGS::ModelIo` and `Native3DGS::RenderCore` are also exported for hosts managing their own loading/render loops. The package is relocatable but does not promise a compiler-independent C++ STL ABI. See [docs/SDK-guide.md](docs/SDK-guide.md) for the complete host lifecycle and [docs/SPEC-engine-sdk.md](docs/SPEC-engine-sdk.md) for API contracts.

To create the distributable ZIP:

```powershell
cpack --config out/cmake/CPackConfig.cmake -C Release -B out/packages
```

The result is `out/packages/Native3DGS-SDK-0.1.0-windows-x64-SDK.zip`. The independent package-consumer test in [tests/sdk/installed-sdk.ps1](tests/sdk/installed-sdk.ps1) can unpack, relocate, build, and run it with a scene file.

## Work on the source

| Area | Start here | Tests and contract |
| --- | --- | --- |
| Scene values and loaders | `include/splat-types/`, `include/model-io/`, `src/model-io/` | [Model I/O spec](docs/SPEC-model-io.md), `tests/model-io/` |
| D3D12 device, upload, sort, and draw | `include/render-core/`, `src/render-core/`, `shaders/` | [Render-core spec](docs/SPEC-render-core.md), `tests/render-core/` |
| Camera and asynchronous engine | `include/native3dgs/`, `src/engine/` | [Engine/SDK spec](docs/SPEC-engine-sdk.md), `tests/engine/` |
| Packaging and host example | `CMakeLists.txt`, `cmake/`, `examples/sdk-host/` | `tests/sdk/`, [SDK guide](docs/SDK-guide.md) |

Keep the dependency boundaries above when making changes. Update the owning spec before changing a public contract, and update host-side buffer definitions together with HLSL layouts. Add focused GoogleTest coverage for behavior changes; run the relevant CTest group before the full suite. Benchmark changes at the same resolution, quality settings, scene, camera path, and GPU before drawing performance conclusions. [AGENTS.md](AGENTS.md) records repository conventions, and [third_party/README.md](third_party/README.md) records dependency pins and notices.

## Troubleshooting

- **CMake reports missing source dependencies:** run `git submodule update --init --recursive` and reconfigure. Dependency directories must contain their pinned source files.
- **CMake cannot find DXC:** install Windows SDK 10.0.26100.0 with the DirectX Shader Compiler. The build currently checks `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\dxc.exe`.
- **Device creation or GPU tests fail:** check Feature Level 12.0, Shader Model 6.0, WaveOps, and the graphics driver. A software adapter is not supported.
- **Installed SDK test fails on a missing scene:** provide a standard binary 3DGS PLY at `..\1.ply`, or use the `-E InstalledSDK` CTest option shown above while testing without the external sample.
- **An SDK host cannot decode or render:** call `native3dgs_deploy_runtime` on its CMake target and verify that `model-io-helper.exe` and `shaders/` are beside the executable. Use a matching MSVC runtime.

## Known limitations and roadmap

- The WinUI 3 desktop shell, file picker, drag-and-drop, input mapping, and visible `SwapChainPanel` integration are not implemented.
- Fixed-camera Spark image comparison (SSIM), equal-quality PresentMon measurements, long-running stability, clean-machine runtime deployment, and AMD/Intel compatibility remain unverified.
- The current benchmark measures GPU stages on an RTX 3080; it does not establish the planned 20% improvement over the existing Viewer.
- Later format families, multi-model rendering, LoD, editing, and animation require separate contracts and tests.

The [technical development plan](docs/technical-development-plan.md) and [system design](docs/system-technical-design.md) describe those milestones and acceptance criteria.

## Contributing

Before a pull request, keep changes scoped to their module, update its specification when contracts change, include relevant tests, and report the build/test commands used. Rendering changes should include fixed-camera image evidence or equal-quality benchmark data. See [AGENTS.md](AGENTS.md) for naming, testing, and commit conventions.

## License

This repository does not currently declare a project-level license in a root `LICENSE` file. Do not infer permission to redistribute this project's source or SDK from the licenses of its dependencies. Third-party components retain their own licenses, listed in [third_party/README.md](third_party/README.md) and included in the SDK package.
