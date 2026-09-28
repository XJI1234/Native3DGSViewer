# Android tablet performance experiments, 2026-09-28

## Device and method

PA2455, Snapdragon 8s Gen 3 / Adreno 735, Android 14, Vulkan Adreno driver,
1968 x 2800 physical display at 60 Hz. The benchmark Surface was 2800 x 1898.
The sample was `../1.ply`, 66,061,086 bytes, 1,179,648 splats,
SHA-256 `A0917F8B7D15D9D07B802BA3E937365E8557EDF292519706F70064CDE3EDB73A`.
Each run used 30 seconds of warmup and 60 seconds of continuous orbit sampling.
The device reported Android thermal status 3 throughout all recorded runs.

The per-frame CSV records `vkQueuePresentKHR` return times, not display times.
Perfetto traces were captured, but this device's FrameTimeline exposed the
Activity transaction layer rather than every Vulkan SurfaceView frame.
`dumpsys SurfaceFlinger --latency` on the SurfaceView BLAST layer supplied the
actual display intervals for the last 127 frames of each inspected run. These
short tail windows do not establish the full 60-second displayed-frame gate.

## Results

| Path | Run | Sample frames | Present-call median ms | Present-call 1% low FPS | GPU project / sort / draw mean ms | Display tail median ms |
| --- | --- | ---: | ---: | ---: | --- | ---: |
| Stable 4-bit radix | baseline-ply-01 | 1271 | 47.18 | 20.09 | 3.151 / 33.771 / 7.006 | - |
| Stable 4-bit radix | baseline-ply-02 | 1271 | 47.15 | 19.96 | 3.143 / 33.736 / 7.001 | - |
| Stable 4-bit radix | baseline-ply-03 | 1270 | 47.20 | 19.85 | 3.145 / 33.740 / 7.003 | 50.00 |
| 16-bin parallel scatter | parallel-ply-01 | 1025 | 58.55 | 16.38 | 3.150 / 45.024 / 7.009 | 66.67 |
| 8-bit radix, four passes | radix8-ply-01 | 909 | 65.97 | 14.16 | 3.150 / 52.818 / 7.002 | 66.67 |

The baseline median across three runs is 47.18 ms for Present-call intervals
and 33.740 ms for GPU sort. Both experiments passed the Adreno GPU stable-sort
self-test against a CPU reference, including equal keys and group boundaries,
but each regressed far beyond baseline run variation and was removed. The
16-bin path scanned each workgroup's keys in parallel with one invocation per
bin; the 8-bit path halved radix passes but expanded histogram/scan work to 256
bins. Neither is retained. The original 4-bit path remains the renderer.

## Evidence and limits

Local raw files are under `out/android-bench/`: each run has CSV, thermal CSV,
device JSON and (except baseline-ply-02) a Perfetto `.pftrace`. The inspected
display tails are `*-surfaceflinger-latency.txt`. Baseline run 02 completed its
CSV, but ADB briefly disconnected before the script could pull its trace; its
trace file is empty and must not be used. The script now force-stops a previous
benchmark Activity before starting and tolerates transient ADB loss while
waiting for CSV. The tablet's package installer removes the pushed APK after
manual installation, so APK hashes must be recorded from the installed
`base.apk` in future runs.

These runs locate the sort bottleneck but do not pass the mobile 33.3 ms / 30
FPS acceptance gate. Thermal status 3, incomplete full-duration display timing,
missing image comparison, SH3 SPZ and 3-million-splat samples, and the 30-minute
thermal run remain open.

## Follow-up on 2026-09-29

The installed benchmark APK SHA-256 was
`11BDDC44C058DD6A0774371FE09EA056614784CE94918BC72E5912DE09E71016`.
This build divides SH data into point-aligned storage-buffer segments to respect
the Adreno 735 `maxStorageBufferRange` of 134,217,728 bytes. The earlier
`Load failed:3` is the engine's upload error. With this APK, `tushuguan.spz`
(SHA-256 `C937734BAA13D9A6483E7A9E1989DA26B23747051DE54C95719E3B96F9D8C06C`,
1,218,232 SH3 points) completed upload and a 90-second replay. A captured
screen image, `out/android-bench/sh3-render-20260929.png`, confirms nonblank
rendering; no fixed-camera reference comparison has been performed yet.

| Path | Run | Sample frames | Present-call median ms | GPU project / sort / draw mean ms | Display median ms | Display 1% low FPS |
| --- | --- | ---: | ---: | --- | ---: | ---: |
| SH3 segmented, full | full-sh3-segments-20260929-01 | 919 | 65.27 | 19.362 / 34.912 / 7.719 | 66.67 | 14.63 |
| SH3 segmented, mobile 75% | mobile-sh3-segments-075-20260929-01 | 927 | 64.68 | 19.374 / 34.866 / 7.189 | 66.67 | 15.00 |

The display trace covers 60.13 seconds, with an 83.33 ms maximum gap; the
benchmark Surface was 2800 x 1898 at scale 1.0. The mobile trace covers
60.12 seconds with a 66.67 ms maximum gap. Its 2100 x 1423 Surface was
measured at 749 permille of the full buffer, with all 1,218,232 points active.
All 90 one-second thermal samples in each run reported status 3. Reducing the
buffer size saved about 0.53 ms of GPU draw time, with no material change to
projection, sorting, or displayed frame interval. The SH3 projection cost is
substantially higher than the PLY path, while sorting remains about 35 ms.
These are one run per setting, not three-run acceptance results. Raw CSV,
display intervals, thermal samples, device metadata, Perfetto traces, and the
full-quality screenshot are in `out/android-bench/`. Mobile/full SSIM at a
fixed camera and the 30-minute thermal test remain open.

An isolated 64-key radix-group experiment passed the GPU stable-sort checks,
including a 1,179,648-key duplicate-heavy CPU-reference comparison, but took
97.4 ms versus 95.9 ms for the 128-key baseline in that isolated comparison.
Its shader changes were removed and its candidate APK was not used for the
SH3 measurements. The million-key correctness test remains in the source tree.

## Subgroup scatter candidate, full-frame tablet replay

The 128-lane subgroup ballot scatter preserves each bin's lane order and
adds earlier subgroups' bin counts before using the existing global prefix.
It passed the Adreno 735 GPU/CPU stable-sort comparison for 1,179,648
duplicate-heavy keys, as well as small, equal-key, boundary and random cases.
On the same tablet, the isolated million-key test alternated candidate,
baseline and candidate shaders. Submit-to-fence wall times in milliseconds
were 66.519/64.739/63.952, 95.490/97.579/96.229, and
65.539/64.843/65.256. Their medians were 64.739, 96.229 and 65.256 ms.
This includes all radix passes and barriers but is not a GPU timestamp or
full-frame measurement. The baseline SPIR-V SHA-256 was
`4B21AE0CE1FEE5AF148921D9E3817BA37540AB55CB94622EAFEB12DDCCF6E58E`;
the candidate was
`FCAD619EF434059348B35FCF7C8A06B66F13FC24B0E45849F9D2BFD4D43A20BC`.

Release and benchmark builds may select the candidate after compute subgroup
basic/ballot capability checks and a GPU stable-sort self-test, limited to the
measured Adreno 735 device name. Other GPUs keep the original scatter. Its APK is
`out/android-bench/Native3DGSViewer-subgroup-benchmark-2026-09-29.apk`,
SHA-256 `803B00ECAF11BAA8CB94B98EA939CFA092CCBEC8AD6B53B37A55983EB7979890`.
The benchmark APK was pushed to `/sdcard/Native3DGSViewer-subgroup-benchmark.apk`;
the installed `base.apk` hash matched. The device logged
`Subgroup sort=1 size=64` for each candidate run. Release builds compiled with
the experimental option off; benchmark builds compiled with it on.

| Scene and mode | Run | Application frame coverage s | GPU project / sort / draw mean ms | Present-call median ms | SurfaceFlinger median ms | SurfaceFlinger 1% low FPS | Eligible capture |
| --- | --- | ---: | --- | ---: | ---: | ---: | --- |
| PLY full | subgroup-full-ply-20260929-01 | 59.97 | 3.118 / 15.932 / 6.996 | 28.91 | 33.33 | 30.00 | yes |
| PLY full | subgroup-full-ply-20260929-02 | 49.01 | 3.119 / 15.946 / 7.017 | 29.04 | 33.33 | 20.00 | no: incomplete application frames |
| PLY full | subgroup-full-ply-20260929-03 | 59.96 | 3.712 / 22.074 / 9.541 | 44.80 | 33.33 | 20.00 | yes |
| PLY mobile, fixed 75% | subgroup-mobile-ply-075-20260929-01 | 59.99 | 3.618 / 21.090 / 9.037 | 30.42 | 33.33 | 20.00 | yes |
| SH3 full | subgroup-full-sh3-20260929-01 | 59.97 | 20.023 / 19.009 / 8.952 | 46.84 | 50.00 | 13.85 | yes |

The PLY mobile Surface was 2100 x 1423, measured at 749 permille of the
2800 x 1898 full buffer; all 1,179,648 points remained active. The candidate
roughly halved the cold PLY sort time and allowed one complete run at the 30
FPS display boundary. A later complete PLY run slowed in all three GPU passes,
so the three-run mobile acceptance gate is not met. The SH3 scene uploaded and
rendered without `Load failed:3`, but projection remains about 20 ms and its
display rate remains below target. No fixed-camera SSIM comparison has been
completed.

The second PLY run had 1,689 application samples spanning only 49.01 seconds,
while SurfaceFlinger reported 1,949 layer samples across 59.97 seconds. The
benchmark summary now requires both streams to cover 59 seconds with no gap
over one second and flags excess display samples; this run is excluded from
acceptance. The third run's GPU sort varied from about 16 ms to 27 ms in
ten-second windows. Android thermal status was 3 in every run, but this
tablet's ThermalService reports a constant 30 C `test sensor`, so that status
does not establish a stable GPU temperature. Perfetto's `quiet_therm`
counter rose from about 33 C in the first PLY run to about 34 C in the third;
GPU frequency sysfs nodes were permission-denied and the trace has no GPU
frequency counter. The cause of the GPU timing variation remains unconfirmed.

The candidate is now included in the formal Release path for the measured
Adreno 735, with runtime capability/self-test and pipeline-failure fallback.
A repeatable three-run
mobile result, fixed-camera SSIM, SH3 projection optimization, and sustained
thermal characterization remain open. Raw frame CSV, display intervals,
thermal samples, device metadata, and Perfetto traces are under
`out/android-bench/`.

## Historical preparation recorded on 2026-09-28

The new benchmark APK is stored locally at
`out/android-bench/Native3DGSViewer-mobile-benchmark-2026-09-28.apk` (25,318,094
bytes, SHA-256
`4C13896F4B45E65F71DA1A1C1EBC6556646D0BA4CF4D69E17082979E63732BAE`).
It had not yet been pushed or installed on the tablet at that time. The viewer
exposed Full and Mobile modes; Mobile adapts the Surface buffer between 100%,
90%, 80%, and
75%, while the UI remains at native size. Gaussian LoD remains disabled pending
image validation. The benchmark Activity accepts `quality=full|mobile` and an
optional fixed `scale_permille` for controlled comparisons; CSV includes mode,
source/active splats, requested/actual scale, and buffer dimensions.

Local verification passed: `:sdk:assembleBenchmark`, `:GUI:testBenchmarkUnitTest`,
`:GUI:assembleBenchmark`, and `:GUI:assembleDebugAndroidTest` offline. Both
benchmark PowerShell scripts parsed, and the summary script reproduced the old
baseline's 47.20 ms Present-call median. These checks do not establish image
quality, actual scaled Surface behavior, displayed-frame coverage, or a new
performance result. The full/mobile three-run comparison, screenshot SSIM,
3-million-point scenario, and thermal run were pending. The follow-up above
supersedes the earlier pending SPZ status and records the new comparison data.
