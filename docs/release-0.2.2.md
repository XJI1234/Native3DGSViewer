# Native3DGS 0.2.2 preview

This release provides viewer and SDK packages for Windows x64 and Android
arm64-v8a. The Android APK uses a development signing key and is a preview,
not a production-signed distribution.

| Product | Platform / ABI | Release file |
| --- | --- | --- |
| Viewer | Windows x64 | `Native3DGSViewer-0.2.2-Windows-x64-Setup.exe` |
| Cloud Viewer (supplement) | Windows 11 x64 | `Native3DGSCloudViewer-0.2.2-Windows-x64-Setup.exe` |
| SDK | Windows x64 | `Native3DGS-SDK-0.2.2-Windows-x64.zip` |
| Viewer | Android arm64-v8a | `Native3DGSViewer-0.2.2-Android-arm64-v8a-preview.apk` |
| SDK | Android arm64-v8a | `Native3DGS-SDK-0.2.2-Android-arm64-v8a.zip` |

## Windows changes

- Increased parallel radix-sort work groups for large scenes, unrolled SH
  projection, and reduced splat draw vertices from six to four per instance.
- Increased copy upload pages to 64 MiB and packed only the selected SH degree
  and points into GPU buffers.
- Added bounded video-memory mitigation: lower SH degree first, then sample
  points at 1/2, 1/4, 1/8 or 1/16 when the current DXGI budget requires it.
  The desktop viewer reports automatic quality changes. SDK hosts can control
  the policy with `QualityConfig` and inspect actual quality in `RenderStats`.

## Android changes

- Enabled capability-checked, self-tested subgroup stable sorting on Adreno
  750, with the original stable path retained as fallback.
- Split SH3 scene attributes into two storage-buffer segments when they exceed
  a device's single-descriptor range. The 2,670,017-point Juyuan PLY and SPZ
  scenes loaded and presented at full point count on the tested tablet.
- Updated the viewer and SDK product version to 0.2.2. The C API remains
  version 2; SDK consumers can inspect it with `gs_android_api_version`.

## Cloud Viewer supplement

- Added a headless C++/CUDA renderer with Go concurrency/session management and SQLite-indexed disk frames. Each model revision shares one GPU instance; only GPU0 is enabled by default.
- Windows WinUI 3 receives compressed images, defaults to JPEG85, and offers JPEG90/95 or lossless RGBA. Full spherical 2-degree navigation preserves screen drag direction when the view is inverted.
- Pre-render Low/Medium/High downloads five/ten/fifteen neighboring positions per cardinal direction. The bounded in-memory cache is cleared on application exit.
- Short control-request timeouts, bounded retries and automatic lease recovery keep idle sessions alive. The session grace is 120 seconds with ten-second heartbeats.
- The separate per-user installer includes WinUI/VC runtime files and does not replace the local Windows viewer. It is unsigned. This is a trusted-network single-node preview; public TLS/RBAC and long-running load validation remain separate gates.

The original v0.2.2 tag remains unchanged. The cloud supplement is built from a later merged source revision identified in the GitHub Release notes. Build/deploy instructions and validation are in `ForServer/README.md` and `ForServer/docs/verification-cloud-022.md` at that revision.

## Verification and limits

- On RTX 3080 with 22,480,361 SH3 splats at 1920 x 1080, the earlier
  non-captured GPU stage benchmark improved from about 41.7 to 33.5 ms;
  a later 20-frame rerun ranged from 34.45 to 37.39 ms. This is a GPU
  diagnostic, not a measured end-to-end or cross-device frame-rate guarantee.
- PIX Timing Captures were collected for medium and large scenes. After Windows
  Developer Mode was enabled, a cached-draw GPU frame and its basic event list
  were exported. Counter export failed inside PIX and GPU occupancy collection
  reported a failed NVIDIA plugin initialization. The equal-quality Web Viewer
  comparison, Intel/AMD and low-memory device validation, and clean-machine
  installer validation remain open.
- On Lenovo TB710FU / Adreno 750, the 1,888,950-point SPZ test reduced the
  three-run median GPU sort mean from 34.98 to 17.84 ms and the Present-call
  median from 78.12 to 56.55 ms. Later runs reached thermal status 3. The
  measured display interval of 50 ms and 1% low of 13-15 FPS still miss the
  Mobile target; equal-quality SSIM and 30-minute thermal validation remain
  open. This is a technical preview, not a 30 FPS claim.

See `docs/windows-performance-verification.md` and
`ForAndroid/docs/verification/device-performance-2026-09-30-adreno750.md`
for samples, timing methods and open gates.
