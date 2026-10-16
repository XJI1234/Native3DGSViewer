# Native3DGS SDK 0.2.2

逐项接口、调用语法、返回值和线程/所有权约束见
[Windows SDK 接口手册](SDK-API-reference.md)。

Windows 11 x64, VS 2026/MSVC 19.50, C++20, Release `/MD`. Install the matching VC runtime on the destination machine. The SDK supplies native static libraries; consumers must use the matching compiler/runtime. Desktop UI is owned by the consumer.

```powershell
cmake --build out/cmake --config Release --parallel 8
cmake --install out/cmake --config Release --prefix out/sdk --component SDK
cmake -S out/sdk/examples/sdk-host -B out/sdk-consumer -A x64 -DCMAKE_PREFIX_PATH="$PWD/out/sdk"
cmake --build out/sdk-consumer --config Release
./out/sdk-consumer/Release/sdk-host.exe ../1.ply
cpack --config out/cmake/CPackConfig.cmake -C Release -B out/packages
```

Link `Native3DGS::Engine` and call `native3dgs_deploy_runtime(host_target)` to place the helper and compiled shaders next to the executable. `Native3DGS::ModelIo` and `Native3DGS::RenderCore` are available for hosts that manage their own worker threads. The package includes pinned dependencies and notices; building a consumer needs neither decoder source nor DXC.

Create an engine, obtain its snapshot's surface generation, then call `addref_surface_queue(generation)`. The returned pointer has one owned COM reference. Use this queue for a DXGI composition swapchain: BGRA8 UNORM, premultiplied alpha, flip sequential, stretch, single sample, two or three buffers. Pass `IDXGISwapChain3` to `attach_swapchain`; engine retains its own reference. Attach the surface to a WinUI `SwapChainPanel` on the host UI thread. The console example tests the native composition contract without creating a desktop UI.

`open` returns a request ID. Poll `snapshot` for real decode/upload progress and errors. Scene/camera replacement commits after the first successful Present. A failed/cancelled replacement retains the active scene. `camera_command` provides orbit, pan, dolly, look, fly, mode, fit, reset, FlipX, FlipY and FlipZ. For each axis command, set `CameraCommand::flip_enabled` to the desired Boolean state. Axes can be combined; when an odd number are enabled, the host must mirror the composed image horizontally. Even combinations need no screen mirror. This changes only the view, never the model. Other inputs use physical pixels and seconds. `resize` accepts physical dimensions; 0x0 pauses presentation. Call `close` and wait for `SceneCleared` before assuming memory is released.

Renderer creation runs a bounded GPU radix-sort readback test before the engine becomes ready. `snapshot().stats.sort_shader_mode` reports `Standard`, `FixedWave32` or `WaveAgnostic`; `sort_self_test_passed` confirms the selected variant. `wave32_fallback_hr` is nonzero if fixed wave32 pipeline setup or validation failed and the wave-agnostic variant passed. A failed final self-test returns `ShaderFailure` from `create_engine` and no surface queue is exposed.

Set `EngineConfig::quality.point_stride` to 1, 2, 4, 8 or 16 for a fixed source-point interval. `max_point_stride` bounds automatic sampling (default 16); `allow_memory_mitigation=false` disables automatic SH reduction and sampling while retaining manual limits. These values are validated when the engine is created and cannot change on a running engine. After `SceneReady`, compare `snapshot().stats.active_source_splats` and `active_source_sh_degree` with `active_splats`, `active_sh_degree` and `active_point_stride`. `memory_mitigation` means the budget forced an additional quality reduction. The frame counters are empty until a completed frame belongs to `active_ticket`.

Poll `poll_events` regularly. The bounded queue reports dropped events in the snapshot; snapshots remain authoritative. On `DeviceLost`, unbind the old UI surface and release every old swapchain/queue/device reference, then acknowledge the reported generation. Recovery pauses for at most ten seconds. Wait for `SurfaceRebindRequired`, acquire the new generation's queue, create and attach a fresh surface. Ready resumes after Present. A timeout is a persistent failure; shut down and create a new engine after resolving the cause.

For an intentional surface detach, wait for `SurfaceDetached` before releasing host references or recreating buffers. `request_shutdown` returns immediately; poll Stopped or call `wait_until_stopped` from a background thread. Destroy after Stopped. Destruction joins workers and can block if invoked early. Do not issue commands concurrently with object destruction.
The console validation host ends its process with exit code 2 if shutdown exceeds its 15-second watchdog, so CI does not wait indefinitely on a stuck engine. Desktop hosts should present a persistent failure state and handle process-level recovery according to their own policy.

See `SPEC-engine-sdk.md`, `GUI/README.md` and verification records for scope and evidence. Spark SSIM, PresentMon and AMD/Intel gates remain external acceptance work.

## Model measurement

After a model is Ready, call `set_measurement_enabled(true)` and `import_metric(path)`.
Poll `snapshot().measurement` until `importing` is false; check `error` and `calibration`.
Any valid v1 metric JSON may be used with PLY or SPZ: model/provenance correspondence is deliberately not checked.
The loader validates the scale and uniform similarity matrix, with a 1 MiB / 64-level input bound.
It uses Windows system JSON and needs no additional redistributed library.

Call `measure_at(x_px, y_px)` twice, waiting for `picking` to become false after each call.
Coordinates are physical pixels relative to the render viewport, before the host's display mirror.
For an odd number of axis flips, undo the host's horizontal mirror once with `width - 1 - floor(x_px)`.
The snapshot supplies `a`/`b` in RUB world and original source coordinates, `distance_m`, `missed`, and structured errors.
No hit keeps existing endpoints; the completed pair rejects a third point. Camera/viewport changes cancel pending picks.
`clear_measurement()` cancels pending work and clears points while retaining calibration;
`set_measurement_enabled(false)` also clears points, and successful model replacement clears the entire session.
Failed imports/replacements retain the prior calibration. A pick times out after five seconds.

`<native3dgs/measurement.h>` also provides bounded blocking `load_metric`, `parse_metric`,
`source_from_rub`, and checked `distance_meters` for worker-thread consumers.
`<render-core/picking.h>` supplies double projection/unprojection and frustum-clipped line projection for overlays.
Visible-surface depth uses contribution-weighted median Gaussian depth; this is an estimate and independent length accuracy needs validation.
The sample accepts an optional second argument: `sdk-host.exe model.ply scene.metric.json`.
The installed consumer test imports a deliberately unrelated calibration and completes two GPU picks.

The published ZIP can be verified independently with `tests/sdk/installed-sdk.ps1 -BuildDirectory out/cmake -PackageArchive out/packages/Native3DGS-SDK-0.2.2-Windows-x64.zip -Scene ../1.ply`. It extracts and relocates the package in the system temp directory, builds/runs the consumer, and removes its temporary tree in a checked finally block. Build/run logs are written to the caller's console. The archive also contains `share/Native3DGS/docs/` with the complete development specifications and verification records, plus `share/Native3DGS/GUI/README.md` for the desktop host.
