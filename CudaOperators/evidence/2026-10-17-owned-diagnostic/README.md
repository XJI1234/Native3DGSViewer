# R06: CUDA-owned read-only inputs

One-round **resource screening**, five models and five backends, 25 runs / 3,000
samples, with CUDA events. This is deliberately not formal acceptance or a
three-round timing claim: the shared-input candidate already failed quality and
budget gates in the separate round2 matrix.

`results.json` includes frozen runtime/model/shader hashes and per-run memory
samples. `input-storage.json` compares matching one-slot shared/owned plane-major
paths. All five shared/owned images are byte-identical; queued owned
images match too. Source coefficients, point count and kernel math are unchanged.

At 22,480,361 points:

- Shared input peak: 13,364,547,584 bytes.
- Owned input peak: 8,056,659,968 bytes; budget 9,736,028,160, ratio 82.75099%.
- 80% limit: 7,788,822,528 bytes; still exceeds by 267,837,440 bytes.
- Same-slot GPU span: 30.515712 → 30.450176 ms (one observation, no CI).
- SH transpose/upload segment: 10.0749 → 1.77225 seconds (one observation).

The loading segment excludes the earlier native upload/decode and is not an
end-to-end loading claim. Explicit staging is limited to 64MiB. CPU/RAM/nonlocal
peaks are preserved in memory CSVs; CUDA runtime's own internal staging is not
claimed to be bounded by the application staging limit.

The candidate remains rejected for product activation: original CUDA projection
ordering exceeds image tolerance on shengyi/jiulonghu, and the large scene still
fails the 80% DXGI budget criterion. See the independent full-order oracle for
the image root-cause experiment; its CPU path is not this rendering backend.
