# CUDA projection math ablations

36 runs: small and largest real PLY, 6 backends, 3 paired rounds; 60 warmup / 120 measured frames, CUDA events ON. Frozen runtime hashes unchanged. This is diagnostic timing, not formal or display acceptance. CPU-only tile test compilation overlapped some runs; formal measurements must exclude builds.

All 30 candidate images passed RGB and alpha gates (maximum 1/255). Seven projection tests cover all four math modes, including HLSL comparison, extreme scales and subnormal camera delta. Compute Sanitizer memcheck: 7/7 tests, 0 errors.

On 22,480,361 points the reference combined projection+CUB reduces native full GPU time by 8.91%; rcp by 3.94%; rsqrt by 8.89%; rcp+rsqrt by 8.65%. Sort-only remains faster at 14.20%. On 170,799 points every CUDA variant loses to native. No tuned math variant improves the reference: do not select it for production or expand to the five-model formal matrix. Keep the reference implementation and explicit experimental controls for reproducibility. See SUMMARY.md / analysis.json for paired data.

No global fast-math, reduced SH, point removal or changed acceptance threshold. The reciprocal variant preserves per-component division when the common reciprocal is nonfinite. These results do not resolve memory-accounting or fault-recovery gates.
