"""Exercise external termination of a CUDA worker blocked by an unresolved fence.

This is an experiment, not a product renderer or proof about arbitrary driver
failures. Each worker owns its own D3D12/CUDA resources; the supervisor uses no
CUDA API and never waits for normal GPU resource destructors after the deadline.
"""

import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import queue
import subprocess
import threading
import time


def handle_count():
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetCurrentProcess.restype = ctypes.c_void_p
    kernel.GetProcessHandleCount.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong)]
    count = ctypes.c_ulong()
    if not kernel.GetProcessHandleCount(kernel.GetCurrentProcess(), ctypes.byref(count)):
        raise ctypes.WinError(ctypes.get_last_error())
    return count.value


def run_worker(binary):
    messages = queue.Queue()
    lines = []
    process = subprocess.Popen([str(binary), "fault-stall"], stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")

    def read_output():
        for line in process.stdout:
            lines.append(line)
            messages.put(line.strip())
        messages.put(None)

    reader = threading.Thread(target=read_output, daemon=True)
    reader.start()
    record = {}
    try:
        deadline = time.monotonic() + 20
        while True:
            message = messages.get(timeout=max(.001, deadline - time.monotonic()))
            if message == "BLOCKING_CUDA_API_READY":
                break
            if message is None or time.monotonic() >= deadline:
                raise RuntimeError("Worker failed to reach blocked CUDA API")
        try:
            process.wait(timeout=1)
            raise RuntimeError("Injected unresolved fence unexpectedly completed")
        except subprocess.TimeoutExpired:
            record["blocked_for_at_least_seconds"] = 1
        start = time.monotonic()
        process.terminate()  # Windows TerminateProcess, no CUDA/COM destructors.
        process.wait(timeout=5)
        record["termination_ms"] = (time.monotonic() - start) * 1000
        record["exit_code"] = process.returncode
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=5)
        reader.join(timeout=2)
        process.stdout.close()
    record["worker_log"] = "".join(lines)
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("out/cmake-cuda/cuda/Release/Native3DGSViewer.CudaRetirementProbe.exe"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repetitions", type=int, default=10)
    args = parser.parse_args()
    if args.repetitions < 1:
        parser.error("Positive repetitions required")
    binary = args.binary.resolve(strict=True)
    # Warm Python's own process/pipe/thread bookkeeping before handle accounting.
    warmup = run_worker(binary)
    before = handle_count()
    runs = []
    result = {"scope": "unresolved fence; external worker termination; no product integration",
              "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
              "warmup": warmup, "parent_handles_before": before, "runs": runs}
    for index in range(args.repetitions):
        runs.append(run_worker(binary))
        print(f"worker {index + 1}: terminate={runs[-1]['termination_ms']:.1f} ms", flush=True)
        args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    after = handle_count()
    result["parent_handles_after"] = after
    result["passed"] = after <= before + 4 and all(run["termination_ms"] < 5000 for run in runs)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    if not result["passed"]:
        raise RuntimeError("External isolation deadline or parent resource check failed")


if __name__ == "__main__":
    main()
