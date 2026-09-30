# Adreno 750 tablet baseline and large-scene compatibility, 2026-09-30

## Device and method

Lenovo TB710FU (`HA28L0L4`), Android 15, Snapdragon SM8650Q / Adreno 750,
driver `0762.31`, 11.5 GB physical RAM, 2000 x 3200 display at 144 Hz.
Vulkan reports a 134,217,728-byte `maxStorageBufferRange` and an
11,787,599,872-byte heap budget. The fixed benchmark rotates the camera for
30 seconds of warmup and 60 seconds of sampling. Each CSV, thermal CSV,
device JSON, and Perfetto trace is under `out/android-bench/`.

The baseline scene is `/sdcard/danganguan_v1.spz` (35,245,308 bytes,
1,888,950 splats, SHA-256
`b7ab62c0a2be0bedb08f306a6024f74e0bd997a071ef36e3dc529033e67d3eb2`).
The baseline APK hash is
`394703cbd27e7925537bfb90667d73726512ff6fafa6e89b9875d9ff29463686`.
The final test APK hash is
`91e3e0cd4094633cf16c218a205b90fe293623094de292156e4f01b3643a0264`.
The final APK is saved as
`out/android-bench/Native3DGSViewer-adreno750-segmented-benchmark-2026-09-30.apk`.

## Fixed-trajectory results

| APK/path | Run suffix | Thermal samples | Present-call median ms | 1% low FPS | GPU project / sort / draw mean ms | Display median ms |
| --- | ---: | --- | ---: | ---: | --- | ---: |
| Baseline stable radix | 01 | 0:90 | 70.19 | 11.48 | 21.06 / 31.95 / 13.96 | unavailable |
| Baseline stable radix | 02 | 0:90 | 78.12 | 10.17 | 22.53 / 35.16 / 15.69 | unavailable |
| Baseline stable radix | 03 | 0:90 | 78.31 | 10.76 | 22.48 / 34.98 / 15.64 | 83.33 |
| Final subgroup | 01 | 0:90 | 56.55 | 13.93 | 19.54 / 17.84 / 16.08 | 50.00 |
| Final subgroup | 02 | 0:34, 3:56 | 56.48 | 13.82 | 19.54 / 17.84 / 16.06 | 50.00 |
| Final subgroup | 03 | 0:37, 3:53 | 56.66 | 13.75 | 19.56 / 17.82 / 16.06 | 50.00 |

The baseline three-run median Present-call interval is 78.12 ms and GPU sort
mean is 34.98 ms. The final APK's corresponding three-run medians are 56.55 ms
and 17.84 ms, reductions of about 28% and 49%. These are diagnostic
comparisons: only final run 01 stayed at thermal status 0, and baseline display
intervals are available only for run 03. A controlled three-run displayed-frame
acceptance comparison remains open. The measured final display interval of
50 ms and 1% low of 13-15 FPS miss the 33.3 ms / 30 FPS Mobile gate.

The earlier subgroup-only APK ran twice at thermal status 0, with 56.24 and
56.60 ms Present-call medians and 17.34 and 17.84 ms GPU sort means. Its
hash is `3d0281797ad0fcc31aaa62b3c36d578475e4d7559e01bddfcd2aea7843540af8`;
these are supporting evidence, not additional repeats of the final APK.
`adreno750-subgroup-full-dang-03` began at thermal status 3 and is excluded
from normal-condition comparisons.

At a fixed 0.75 Surface scale, Mobile quality still submitted all 1,888,950
points. `adreno750-subgroup-mobile075-dang-01` measured a 56.19 ms
Present-call median and 19.60 / 17.85 / 15.38 ms GPU stages. Its actual
buffer was 2400 x 1500, so the scale change took effect; the remaining work
is mainly projection and sorting. Visible-set compaction or validated
screen-error LoD is the next experiment. A global point drop has not been
accepted without image and temporal checks.

## Juyuan admission and rendering

Both `/sdcard/juyuan_v2.spz` (SHA-256
`24133da7801ff5986c350dcc96064cd8e32ecba5226507a68420d698f2c54f2c`)
and `/sdcard/juyuan_v2.ply` (SHA-256
`37ee2dbfeebeb1ff27f5592ebab25cc7fc63a7a8de4551a02d5f8befd5c72129`)
decode to 2,670,017 splats with SH degree 3. Before the change, the SPZ
decoded and imported, then failed `Scene admission`. Its 149,520,952-byte
contiguous attribute buffer exceeded the device's 128 MiB descriptor range;
the 128,160,816-byte projected buffer fits. The changed renderer preserves
every point and coefficient, splitting attributes into 134,217,728- and
15,303,224-byte segments and selecting a separate projection shader only for
that layout. The final APK logged `attribute_segments=2`, successful upload,
and a presented first frame for SPZ. The same full-count upload and first
frame succeeded for PLY. A SPZ 60-second sample is
`out/android-bench/juyuan-segmented-full.csv`: 66.37 ms Present-call median,
21.16 / 24.49 / 17.75 ms GPU project/sort/draw means. A visual capture is
`out/android-bench/juyuan-segmented-full.png`. The production ViewerActivity
also opened `/sdcard/juyuan_v2.ply` through the system document picker and
reached Ready with the model name displayed. The default camera looks along
the thin side of this particular scene; after an orbit gesture, the scene is
visible in `out/android-bench/juyuan-viewer-ply-oblique.png`.

This layout currently supports at most two attribute segments. The projected
buffer still limits this device to 2,796,202 points, and a device with fewer
than ten compute-stage storage descriptors cannot use the segmented path.
Those limits remain explicit admission failures. No point thinning, SH
reduction, or resolution change was applied to the Juyuan full-quality test.

## Verification and open gates

`AndroidRenderCore` passed all five native tests on this tablet, including
million-key stable sorting and subgroup self-test. Gradle built
`:sdk:assembleRelease`, `:GUI:assembleBenchmark`, and `:GUI:assembleRelease`.
The benchmark APK was installed with ADB. The release APK output is unsigned;
it was built but not installed. `sdk-release.aar` SHA-256 is
`f0cf100714d5fb15468c7e4cca126cef8007825ab7df63300af0e4438f25813c`.

The 30-minute thermal browse, fixed-camera pixel comparison for the new
segmented shader, and 30 FPS Mobile gate remain open. Thermal status
transitions in final runs 02-03 must be retained when
interpreting the current benchmark. No SSIM result is claimed: the new
segmentation preserves inputs, while Mobile point LoD has not been enabled.
