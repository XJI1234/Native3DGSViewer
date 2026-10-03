import argparse
import json
import os
import select
import signal
import subprocess
import sys
from pathlib import Path


def ready(process):
    readable, _, _ = select.select([process.stdout], [], [], 180)
    if not readable:
        raise TimeoutError("Pool readiness timeout")
    line = process.stdout.readline()
    if not line:
        raise RuntimeError("Pool failed before readiness")
    return json.loads(line)


def gone(pid):
    try:
        os.kill(pid, 0)
        return False
    except ProcessLookupError:
        return True


def main():
    parser = argparse.ArgumentParser(description="Linux real single-card pool shutdown, collision, invalid-device cleanup")
    parser.add_argument("--server", required=True)
    parser.add_argument("--model", required=True)
    parser.add_argument("--out", type=Path, default=Path("pool-test"))
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    pool_script = str(Path(__file__).with_name("worker_pool.py"))
    results = []

    def launch(devices, name):
        error = (args.out / f"{name}.stderr").open("w")
        process = subprocess.Popen([sys.executable, pool_script, "--server", args.server, "--model", args.model,
                                    "--devices", devices, "--base-port", "8090", "--logs", str(args.out / name)],
                                   stdout=subprocess.PIPE, stderr=error, text=True)
        return process, error

    def record(name, passed):
        results.append({"name": name, "passed": passed})
        print(json.dumps(results[-1]), flush=True)
        if not passed:
            raise AssertionError(name)

    holder = collision = failing = None
    handles = []
    try:
        holder, handle = launch("0", "holder")
        handles.append(handle)
        first = ready(holder)
        collision, handle = launch("0", "collision")
        handles.append(handle)
        collision.wait(timeout=30)
        record("port_collision_does_not_claim_or_kill_existing_worker", collision.returncode != 0 and holder.poll() is None and not gone(first["pid"]))
        holder.send_signal(signal.SIGTERM)
        holder.wait(timeout=20)
        record("single_gpu_sigterm_cleans_child", holder.returncode == 0 and gone(first["pid"]))
        failing, handle = launch("0,99", "invalid-device")
        handles.append(handle)
        first = ready(failing)
        failing.wait(timeout=30)
        record("invalid_second_device_cleans_first_worker", failing.returncode != 0 and gone(first["pid"]))
    finally:
        for process in [holder, collision, failing]:
            if process is not None and process.poll() is None:
                process.terminate()
                process.wait(timeout=20)
        for handle in handles:
            handle.close()
        (args.out / "checks.json").write_text(json.dumps(results, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
