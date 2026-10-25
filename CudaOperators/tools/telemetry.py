"""Diagnostic global GPU telemetry around one child process; timing is instrumented."""

import argparse
import json
from pathlib import Path
import subprocess
import time

import psutil


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=300)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command or args.timeout <= 0:
        parser.error("Command and positive timeout required")
    args.output.mkdir(parents=True, exist_ok=False)
    start = time.monotonic()
    result = {"command": command, "global_gpu_memory": True, "diagnostic_interference": True,
              "samples": [], "timed_out": False}
    gpu_query = ["nvidia-smi", "--query-gpu=timestamp,index,memory.total,memory.used,temperature.gpu,clocks.sm,power.draw",
                 "--format=csv,noheader,nounits"]
    with (args.output / "process.log").open("wb") as log:
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
        try:
            while process.poll() is None:
                sample = {"seconds": time.monotonic() - start}
                gpu = subprocess.run(gpu_query, capture_output=True, text=True, timeout=10)
                sample["gpu_query_exit"] = gpu.returncode
                sample["gpu_csv"] = gpu.stdout.strip()
                try:
                    parent = psutil.Process(process.pid)
                    memory = parent.memory_info()
                    sample["process_rss"] = memory.rss
                    sample["process_private"] = getattr(memory, "private", None)
                    sample["children_rss"] = sum(p.memory_info().rss for p in parent.children(recursive=True) if p.is_running())
                except (psutil.NoSuchProcess, psutil.AccessDenied):
                    pass
                result["samples"].append(sample)
                if time.monotonic() - start >= args.timeout:
                    result["timed_out"] = True
                    process.kill()
                    break
                time.sleep(.5)
            result["exit_code"] = process.wait(timeout=15)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=15)
            (args.output / "telemetry.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f'exit={result["exit_code"]}, samples={len(result["samples"])}, timeout={result["timed_out"]}')
    if result["exit_code"] != 0 or result["timed_out"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
