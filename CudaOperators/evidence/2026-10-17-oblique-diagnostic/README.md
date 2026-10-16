# Full-campus oblique camera after ownership fixes

Fixed yaw30°, pitch-45°, dolly2 through the existing CameraController; pan0. Five models × native/CUB three-slot backends × three alternating paired rounds =30 runs, 3600 measured frames (60 warmup/120 sample each). This is diagnostic duration. It uses the build with queue/context ownership guards, full points/SH, unchanged radial ordering and quality. All final images are byte-identical; frozen runtime hashes unchanged.

| Sources | Model | Native median ms | CUB median ms | Mean paired reduction | Nonzero-alpha area |
|---:|---|---:|---:|---:|---:|
| 170799 | changjin | .4987 | .7562 | -49.91% |17.77%|
| 804758 | shengyi |1.2959|1.3824|-6.96%|11.35%|
|1248730|tumu|1.9282|1.9523|-1.31%|9.89%|
|3914609|zhihuizhimen|5.9735|5.5009|7.92%|6.95%|
|22480361|jiulonghu|34.7382|29.6069|14.77%|12.78%|

See SUMMARY.md for whole-round intervals and results.json for exact commands, count/coverage/CPU-submit/memory/GPU boundary telemetry and hashes. The largest model now shows the full campus rather than the prior near-horizontal strip. Its visible count is all22,480,361; the3,914,609-source model has3,904,214 visible. Small and middle candidates still regress. No universal default is justified.

Next formal comparison uses this same guarded runtime and oblique camera. Do not substitute the earlier pan16.70% result for representative browsing; that pan view covers only1.42% of the viewport. All measurements here remain offscreen; no display or input latency conclusion.
