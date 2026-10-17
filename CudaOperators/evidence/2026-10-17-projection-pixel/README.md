# R07: projection image error and ordering oracle

All runs use full source quality, 1920×1080, radial 32-bit keys, yaw30/pitch−45/
dolly2. No tolerance or source point count changes. These are post-measurement
diagnostics, **not** accelerated rendering or formal performance samples.

`comparison.json` compares the original frozen-runtime pixel traces at shengyi
(1073,558). All 933 candidate indexes match; relative order differs and 19 have
different absolute ranks. Geometry/color differences are tiny. The newer
`*-keys.csv` also records actual sorted keys: 112 of those 933 candidates differ
in key bits. CUB stability is independently verified; its input keys differ.

`causal-order-result.json` keeps CUDA ellipses/colors and permutes only those
933 local candidates into the D3D12 order. The target pixel changes from
BGRA(232,233,235,255) to the exact reference (222,222,225,255), removing its 11/255
error. Another pixel still fails, so this was not declared a whole-image fix.

`full-order-oracle.json` then replaces the complete permutation while preserving
CUDA ellipses/colors:

| Model | Count | Different sorted positions | Original RGB max | Full-order RGB max | All image gates |
|---|---:|---:|---:|---:|---|
| shengyi | 804,758 | 21,510 | 11/255 | 6/255 | pass |
| jiulonghu | 22,480,361 | 7,168,514 | 21/255 | 5/255 | pass |

Both have zero remaining pixels above 8/255. Exact SSIM/RMSE/p99/alpha errors,
image hashes and order-dump hashes are in JSON. Runtime hashes are in
`full-oracle-runtime.json`. PNGs preserve the original and oracle images. This
establishes the causal effect of order differences for these two captures; it
does not identify a specific compiler instruction or prove another GPU path.

The oracle does CPU readback and uploads an externally supplied permutation.
It runs only after all measured frames and preserves the original `.bgra`;
its result is `.order-corrected.bgra`. It is never selected by `collect.py` and
must not be used to claim CUDA speedups. CSV references validate the same unique
local candidate set; `.u32` references validate exact size and a full permutation.
`reject-duplicate-order.log` verifies a duplicate full-order index is rejected
before the oracle upload, with the expected "not a permutation" error.

Reproduce from the repository root, after building CudaBench (replace the model
name to repeat jiulonghu):

```powershell
$model = 'C:/Users/21544/Desktop/zhishan/shengyi_v1.ply'
$common = @('--model', $model, '--frames', '1', '--warmup-frames', '1', '--yaw-degrees', '30', '--pitch-degrees', '-45', '--dolly-steps', '2', '--debug-pixel-x', '1073', '--debug-pixel-y', '558')
& out/cmake-cuda/cuda/Release/Native3DGSViewer.CudaBench.exe @common --backend d3d12-queued --output out/order-reference.csv
& out/cmake-cuda/cuda/Release/Native3DGSViewer.CudaBench.exe @common --backend cuda-project-256 --output out/order-cuda.csv --debug-order-reference out/order-reference.csv.indexes.u32
```

Large `.u32` dumps remain in ignored `out/`, rather than duplicating model-scale
binary data in Git. The raw per-pixel traces, source code, commands, dump hashes,
original images and oracle images make the observation reviewable/reproducible.
