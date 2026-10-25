# R02: CUB offset width

Only CUB's public item-count type changes (`int64_t` vs `int32_t`), while all
32 key bits and stable uint32 payloads are preserved. Default stays Int64.
Int32 capacities above INT32_MAX are rejected before allocation.

`green.log` covers boundaries, four distributions, 1M, 8M and 22,480,361 items
against full-array CPU stable_sort. `memcheck.log`: both Int32 tests pass, zero
errors. `ctest.log` records seven passing groups and a missing CudaProbe after a
clean build; it is **not** an 8/8 pass. `build-probe.log` then rebuilds the probe
and header consumers; `toolchain.log` verifies the previously missing group.

Each `micro-COUNT-MODE.csv` restores original keys and values before every
iteration (outside timing), alternates native/CUDA order for three rounds and
validates all outputs for random/sorted/reverse/equal. Each file has 480 measured
rows (20 samples × 3 rounds × 4 distributions × 2 backends). `micro-summary.csv`
summarizes the CUDA kernel samples; these are short diagnostics, not formal
30/60-second paired performance acceptance.

| Count | Random i64 → i32 (ms) | Kernel reduction |
|---|---|---|
| 1,000,000 | 0.178176 → 0.163840 | 8.05% |
| 8,000,000 | 0.894976 → 0.907264 | −1.37% |
| 22,480,361 | 2.435585 → 2.452480 | −0.69% |

No universal benefit is established. Four-distribution results and native/CUDA
interop spans remain in raw CSVs. Whole-frame evidence is in the separate
`2026-10-17-round2-diagnostic` collection, using its own frozen runtime hashes.

Reproduce one microbenchmark from the repository root:

```powershell
out/cmake-cuda/cuda/Release/Native3DGSViewer.CudaSortBench.exe 22480361 out/sort-i32.csv i32
```

Build dependency diagnosis is also retained here: `dependencies-after.log`
shows ownership_tests.cpp has 30 dependencies including sort.h, compared with
zero before the repair; `header-invalidation.log` shows a touched sort.h triggers
all its consumers. The raw-byte prefix probe fixes the actual localized
`/showIncludes` prefix. VSLANG=1033 alone did not fix this Chinese-only MSVC host.
