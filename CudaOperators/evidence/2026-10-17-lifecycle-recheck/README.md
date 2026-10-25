# Debug Layer retention remains reproducible

After formal timing and the final Graph contract fix, the existing lifecycle binary was run with `--gtest_shuffle --gtest_random_seed=1`. This puts the Debug Layer queue-stall test first, then the100-context handle test in the same process. The first test passes; the second **fails the unchanged before+4 threshold**, with592→692 handles. See `debug-retention-seed1.log`. D3D12 Debug Layer activation is process global, so later devices inherit that configuration.

The registered isolated ordinary-configuration `CudaLifecycleHandles` test then passes in a fresh process (`normal-isolated.log`). Neither result supersedes the other. The previously recorded classification locates the condition at Debug Layer/CUDA interop fence retirement; the exact retaining implementation is still unproven. Do not close handles without ownership, relax the threshold, disable validation to claim a pass, or call this production stability acceptance.

Reproduce sequentially from repository root:

```powershell
& ./out/cmake-cuda/cuda/Release/Native3DGSViewer.CudaLifecycleTests.exe --gtest_shuffle --gtest_random_seed=1
ctest --test-dir out/cmake-cuda -C Release --output-on-failure -R '^CudaLifecycleHandles$'
```

The first command is a deliberately retained failing diagnostic, not a green suite result. Device-removal/fault teardown and GUI/soak gates remain unverified.
