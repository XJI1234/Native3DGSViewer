# Captured CUB Graph rejects zero-count rebinding

RED: `GraphRejectsChangedBindingsAndCapacity` captured513 items, then submitted0. The required invalid_argument was absent; the test failed (`red.log`). The early zero-count return bypassed the fixed-count Graph contract.

Fix: reject count0 when an executable Graph already exists. An uncaptured zero-capacity workspace still returns selector0 without launching work. Tests now exercise that valid no-op, changed-to-zero rejection and a correct513 replay after the rejected call. No GPU math, positive-count ordinary CUB algorithm, camera or quality setting changes.

GREEN: two Graph behavior tests pass (`green.log`); the same two under Compute Sanitizer memcheck pass with0 errors (`memcheck.log`). Ordinary boundaries, including0/1/511/512/513/65537 and four distributions, also pass (`ordinary-boundaries.log`). Release CudaTests/CudaBench rebuilt successfully (`build-green.log`). Builds/checks occurred only after all formal timing finished; frozen measurements retain their original runtime hashes.

Commands from repository root:

```powershell
pwsh -NoProfile -File CudaOperators/tools/build.ps1 -Targets Native3DGSViewer.CudaTests,Native3DGSViewer.CudaBench
& ./out/cmake-cuda/cuda/Release/Native3DGSViewer.CudaTests.exe '--gtest_filter=CudaSort.Graph*'
& "$env:USERPROFILE/AppData/Local/Native3DGSViewer/toolchains/cuda/13.3.0/compute-sanitizer/compute-sanitizer.exe" --tool memcheck --error-exitcode 99 ./out/cmake-cuda/cuda/Release/Native3DGSViewer.CudaTests.exe '--gtest_filter=CudaSort.Graph*'
& ./out/cmake-cuda/cuda/Release/Native3DGSViewer.CudaTests.exe --gtest_filter=CudaSort.StablePairsAtBlockBoundaries
```

The prior complete CUDA8/8 and production CUDA-OFF5/5 runs are documented separately; they were not rerun or relabeled as occurring after this focused fix. Debug Layer handle retention remains unresolved, see the adjacent lifecycle recheck.
