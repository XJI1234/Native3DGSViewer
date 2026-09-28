# Native3DGS 0.2.1 preview

This release contains the Windows 11 x64 viewer and SDK, plus the Android
arm64-v8a viewer and SDK. The Android viewer APK is a Release build distributed
**unsigned** because no production signing key is configured. It cannot be
installed directly. The development-key signed candidate was rejected by the
target tablet and has been removed from the release. An application publisher
must sign the unsigned APK with its own release certificate before installation.

## Changes

- Android Vulkan uses stable subgroup ballot scatter on the tested Adreno 735
  after capability checks and a GPU/CPU stable-sort self-test. Other GPUs and
  failed candidate pipelines use the original stable radix path.
- Android SH3 data is uploaded in storage-buffer-range-safe segments. GPU
  projection, sorting and drawing timestamps are available for completed frames.
- Android viewer offers Full and Mobile modes. Mobile supports a 0.75-1.0
  Surface render scale; unvalidated Gaussian LoD remains disabled.
- Mobile image acceptance is now strictly SSIM > 0.80 at equal camera and final
  display size, with separate checks for edges, holes and temporal changes.

## Verification and limits

- Windows Release solution build and CTest 5/5 passed. The relocated SDK
  consumer rendered 1,179,648 splats.
- Android Release AAR/unsigned APK and C/Kotlin SDK consumers built. PA2455 / Adreno 735
  native render-core tests, including million-key stable sorting, passed.
- The measured subgroup candidate reduced cold PLY GPU sorting from about
  33.7 ms to 15.9 ms. Later warm runs slowed. Three complete comparable runs,
  fixed-camera SSIM, 30-minute thermal behavior, low-memory and cross-driver
  acceptance are not complete; this preview does not claim the 30 FPS target.
- OCR produced no findings in the completed subset, but its render-core and
  Android bridge groups timed out. The review is incomplete.

See `ForAndroid/docs/verification/device-performance-2026-09-28.md` and
`ForAndroid/docs/verification/optimization-roadmap-2026-09-29.md` for raw
measurement context and the next experiments.
