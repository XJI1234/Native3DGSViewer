# Native3DGS 0.2.2 Windows preview

This release updates the Windows 11 x64 viewer and SDK. The Android viewer and
SDK remain the 0.2.1 builds from the previous release:

- [Android SDK 0.2.1](https://github.com/XJI1234/Native3DGSViewer/releases/download/v0.2.1/native3dgs-android-0.2.1.zip)
- [Android viewer APK 0.2.1](https://github.com/XJI1234/Native3DGSViewer/releases/download/v0.2.1/Native3DGSViewer-0.2.1-arm64-v8a-preview.apk)

## Windows changes

- Increased parallel radix-sort work groups for large scenes, unrolled SH
  projection, and reduced splat draw vertices from six to four per instance.
- Increased copy upload pages to 64 MiB and packed only the selected SH degree
  and points into GPU buffers.
- Added bounded video-memory mitigation: lower SH degree first, then sample
  points at 1/2, 1/4, 1/8 or 1/16 when the current DXGI budget requires it.
  The desktop viewer reports automatic quality changes. SDK hosts can control
  the policy with `QualityConfig` and inspect actual quality in `RenderStats`.

## Verification and limits

- On RTX 3080 with 22,480,361 SH3 splats at 1920 x 1080, the earlier
  non-captured GPU stage benchmark improved from about 41.7 to 33.5 ms;
  a later 20-frame rerun ranged from 34.45 to 37.39 ms. This is a GPU
  diagnostic, not a measured end-to-end or cross-device frame-rate guarantee.
- PIX Timing Captures were collected for medium and large scenes. PIX event
  export required Windows Developer Mode, which was unavailable during this
  review. The equal-quality Web Viewer comparison, Intel/AMD and low-memory
  device validation, and clean-machine installer validation remain open.
- The Android files linked above are unchanged 0.2.1 preview binaries. The APK
  uses a development signing key and is not a production-signed distribution.

See `docs/windows-performance-verification.md` for samples, timing methods,
rollback experiments and capture hashes.
