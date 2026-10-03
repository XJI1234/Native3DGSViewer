import argparse
import json
import signal
import subprocess
import time
import urllib.request
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description="One resident Linux process per CUDA device; loopback ports only")
    parser.add_argument("--server", type=Path, required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--devices", default="0")
    parser.add_argument("--base-port", type=int, default=8080)
    parser.add_argument("--logs", type=Path, default=Path("worker-logs"))
    args = parser.parse_args()
    devices = [int(device) for device in args.devices.split(",")]
    if not devices or len(set(devices)) != len(devices) or min(devices) < 0:
        parser.error("Device indices must be unique nonnegative integers")
    if not 0 < args.base_port <= 65536 - len(devices):
        parser.error("Ports outside range")
    if not args.server.is_file() or not args.model.is_file():
        parser.error("Server and model must exist")
    args.logs.mkdir(parents=True, exist_ok=True)
    workers, logs = [], []
    stopping = False

    def stop(signum, frame):
        nonlocal stopping
        stopping = True

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)
    try:
        for index, device in enumerate(devices):
            if stopping:
                break
            port = args.base_port + index
            log = (args.logs / f"gpu-{device}.log").open("w", encoding="utf-8")
            logs.append(log)
            worker = subprocess.Popen([str(args.server.resolve()), "serve", "--model", str(args.model.resolve()),
                                       "--device", str(device), "--port", str(port)], stdout=log, stderr=log)
            workers.append(worker)
            deadline = time.monotonic() + 180
            while time.monotonic() < deadline and not stopping:
                if worker.poll() is not None:
                    raise RuntimeError(f"GPU {device} failed; see {log.name}")
                try:
                    with urllib.request.urlopen(f"http://127.0.0.1:{port}/health", timeout=1) as response:
                        if response.read() == b"ready":
                            if response.headers.get("X-GS-Worker-Pid") != str(worker.pid):
                                raise RuntimeError("Port belongs to another process")
                            print(json.dumps({"device": device, "pid": worker.pid, "port": port, "state": "ready"}), flush=True)
                            break
                except OSError:
                    time.sleep(0.1)
            else:
                if not stopping:
                    raise TimeoutError(f"GPU {device} readiness timeout")
        while not stopping:
            if any(worker.poll() is not None for worker in workers):
                raise RuntimeError("Worker exited unexpectedly; stopping pool")
            time.sleep(0.1)
    finally:
        for worker in workers:
            if worker.poll() is None:
                worker.terminate()
        for worker in workers:
            try:
                worker.wait(timeout=15)
            except subprocess.TimeoutExpired:
                worker.kill()
                worker.wait()
        for log in logs:
            log.close()


if __name__ == "__main__":
    main()
