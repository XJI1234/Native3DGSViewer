# R01 / R05: retirement and external fault isolation

These investigations do **not** resolve the product retirement gate. No fence
workaround was retained in Context, and unknown Event handles are never closed.
The original `before + 4` handle threshold remains unchanged.

## Failed retirement attempts

The explicit Debug Layer 100-context test fails in `red.log` (605 → 705 handles).
`tail.log`, `cpu-ack.log` and `dual.log` retain the native queue tail signal,
post-completion CPU acknowledgement, and split-direction fence attempts. All
fail the same threshold; patches are preserved alongside the logs and withdrawn
from runtime code. Build logs are historical diagnostic records, not proof that
the current source contains those attempts.

`final-debug-red.log` repeats the explicit gate after the final build: 605 → 701,
still failing. The final 13/13 registered CTest result does not include this RED.

## Public-API isolation probe

`probe-*.log` uses five warmups and ten measured lifetimes in separate processes.
Counts below are **Event handles**, obtained using the Windows Process Snapshot
API. The optional native handle tracing yields open/close traces but no usable
stacks on this host; its private layout is diagnostic only and proves no owner.

| Mode | Events before → after | Observation |
|---|---:|---|
| native shared fence | 137 → 137 | no growth in this run |
| Context construction only | 202 → 202 | no growth |
| raw CUDA semaphore import/destroy | 202 → 202 | no growth |
| raw D3D signal / CUDA wait | 202 → 202 | no growth |
| raw CUDA signal / D3D queue Wait roundtrip | 205 → 208 | growth, timing dependent |
| same roundtrip + SharingContract notification | 205 → 215 | growth persists |
| full Context / buffer / kernel | 207 → 217 | growth persists |

`ID3D12SharingContract` was available (HRESULT 0); notification was issued only
after actual CUDA completion and a completed native queue tail. It is not used
to satisfy an unresolved dependency. This narrows the reproduction to the
external CUDA signal / D3D queue Wait path, without attributing a specific DLL.
It does not establish a driver or Debug Layer root cause.

Run one non-stalling probe:

```powershell
out/cmake-cuda/cuda/Release/Native3DGSViewer.CudaRetirementProbe.exe raw-roundtrip
```

## Bounded external termination experiment

```powershell
python CudaOperators/tools/verify-fault-isolation.py --output out/fault-isolation.json
```

The supervisor launches a worker with its own D3D12/CUDA resources. After a real
warmup, the worker inserts an unresolved queue fence, verifies wait_idle(10)
times out, then intentionally blocks in cudaStreamSynchronize. Only the
supervisor should launch `fault-stall`; it deliberately never releases its gate.

Ten measured workers (plus one warmup) stayed blocked for at least one second,
then Windows process termination and process-handle completion took
64.98–77.85 ms. Parent handle count stayed 137 → 137. Exact per-worker times,
exit codes, binary hash and logs are in `fault-isolation.json`.

This proves external supervision for this injected unresolved fence on this
host. It is not a product worker integration, automatic scene/device recovery,
GPU memory reclamation measurement, or a bound for arbitrary driver failures.
The CUDA documentation permits any API to block; Windows TerminateProcess is
asynchronous and pending I/O can affect completion. In-process cudaFree and
external import cleanup remain best effort.
