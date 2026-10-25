# E09 frame slots and ordinary CUB Graph

90 runs: five models × six backends × three paired rounds, each 60 warmup/120 measured frames, alternating model/backend order. Runtime snapshot hashes were unchanged. All 90 final images are byte-identical to D3D12. This is a short offscreen diagnostic, with no Present, display FPS or input-latency conclusion. See `results.json`, raw CSV/log/PNG and the three generated SUMMARY/analysis files.

The original native path and a native three-slot control distinguish scheduling changes from CUDA effects. Slots own independent allocators/lists/query heaps/readbacks/events; they are reused only after their final fence. Shared model/target resources remain ordered through the D3D12 queue and CUDA handoff fence. Each frame recomputes current camera projection and sorting. CPU wall includes queue residence and deferred resolution; CPU submit excludes slot-recycle waits.

Against **native three-slot** control, ordinary CUDA sorting with three slots reduces full GPU span about 7.78% for 3,914,609 points (5.987→5.514ms; paired 95% bootstrap 7.63–7.90%) and 14.86% for 22,480,361 (34.751→29.623ms; 14.71–15.11%). It regresses at the three smaller sizes. Thus it is not a universal default, and the largest case needs formal-duration measurement to assess the 15% optional-backend threshold.

CUB Graph's additional effect over the same three-slot CUDA path is 6.07% for the smallest case, which still loses to native by over 50%; about 0.74%, 0.57%, 0.19% and -0.06% for increasing sizes. Larger-scene intervals include zero for the last three. Graph therefore is not selected for the next large-model formal candidate; the simpler ordinary CUB three-slot variant tests the remaining hypothesis.

Formal follow-up uses the native three-slot backend as both timing and image reference (`--image-reference d3d12-queued`), so each round compares equal queue depth directly. The native one/three-slot images and timing comparison remain in this six-way matrix. Boundary GPU temperature/clock/memory/power snapshots will be taken outside benchmark processes; they are not continuous telemetry.

Unresolved production gates include exact shared-memory budget accounting, context ownership/fault teardown, Debug Layer fence-retirement handles, GUI resize/cancel, moving-camera image/latency verification, deployment and soak. Passing this diagnostic does not satisfy them.
