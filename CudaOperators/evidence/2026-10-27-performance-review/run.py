"""Read-only corpus measurement using the repository's unmodified benchmark hosts.

Run from the repository root: python CudaOperators/evidence/2026-10-27-performance-review/run.py
Output subdirectories must not already exist. GPU work is strictly sequential.
"""
import csv
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent
MODELS = Path(r"C:\Users\21544\Desktop\zhishan")
SCENE = ROOT / "out/Release/Native3DGSViewer.SceneBench.exe"
CUDA = ROOT / "out/cmake-cuda/cuda/Release/Native3DGSViewer.CudaBench.exe"


def sha(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(8 << 20), b""):
            h.update(block)
    return h.hexdigest()


def save(name, value):
    (OUT / name).write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def capture(command):
    p = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=30)
    return {"command": command, "exit_code": p.returncode, "stdout": p.stdout, "stderr": p.stderr}


def collect(name, models, formal=False, camera="fit"):
    command = [sys.executable, str(ROOT / "CudaOperators/tools/collect.py"), "--models", *models,
               "--backends", "d3d12-queued", "cuda-sort-queued", "--image-reference", "d3d12-queued",
               "--repetitions", "3", "--camera", camera, "--yaw-degrees", "30", "--pitch-degrees", "-45",
               "--dolly-steps", "2", "--binary", str(CUDA), "--output", str(OUT / name)]
    if formal:
        command.append("--formal")
    save(name + "-command.json", command)
    subprocess.run(command, cwd=ROOT, check=True)
    subprocess.run([sys.executable, str(ROOT / "CudaOperators/tools/analyze.py"), str(OUT / name)], cwd=ROOT, check=True)


def main():
    environment = {"started_local": time.strftime("%Y-%m-%d %H:%M:%S %z"), "platform": platform.platform(),
                   "head": capture(["git", "rev-parse", "HEAD"]),
                   "gpu": capture(["nvidia-smi", "-q"]),
                   "system": capture(["powershell", "-NoProfile", "-Command",
                       "Get-CimInstance Win32_OperatingSystem | Select Caption,Version,BuildNumber,TotalVisibleMemorySize,FreePhysicalMemory | ConvertTo-Json"]),
                   "cpu": capture(["powershell", "-NoProfile", "-Command",
                       "Get-CimInstance Win32_Processor | Select Name,NumberOfCores,NumberOfLogicalProcessors | ConvertTo-Json"]),
                   "power": capture(["powercfg", "/getactivescheme"]),
                   "scene_binary_sha256": sha(SCENE), "scene_helper_sha256": sha(SCENE.parent / "model-io-helper.exe"),
                   "cuda_binary_sha256": sha(CUDA), "runner_sha256": sha(Path(__file__)),
                   "load_cache_policy": "OS cache not flushed; all inputs hashed first; warm/cache-uncontrolled decode times, not cold disk latency"}
    save("environment.json", environment)
    for name in ["cuda-build-20261027.log", "cuda-tests-20261027.log", "cuda-scenebench-build-20261027.log"]:
        shutil.copyfile(ROOT / "out" / name, OUT / name)
    paths = sorted(p for p in MODELS.rglob("*") if p.suffix.lower() in (".ply", ".spz"))
    inventory = [{"model": p.relative_to(MODELS).as_posix(), "bytes": p.stat().st_size, "sha256": sha(p)} for p in paths]
    save("inputs.json", inventory)
    print(f"Hashed {len(paths)} model inputs", flush=True)
    rows = []
    for repetition in range(1, 4):
        ordered = inventory if repetition % 2 else list(reversed(inventory))
        for model in ordered:
            name = model["model"]
            command = [str(SCENE), str(MODELS / name), "load", "1"]
            start = time.monotonic()
            try:
                p = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180)
                row = {"model": name, "repetition": repetition, "exit_code": p.returncode,
                       "process_wall_s": time.monotonic() - start, "command": command}
                match = re.search(r"splats=(\d+) sh_degree=(\d+) scene_bytes=(\d+) load_ms=([\d.eE+-]+)", p.stdout)
                if match:
                    row.update(dict(zip(["splats", "sh_degree", "scene_bytes"], map(int, match.groups()[:3]))))
                    row["load_ms"] = float(match[4])
                row["output"] = p.stdout + p.stderr
            except subprocess.TimeoutExpired:
                row = {"model": name, "repetition": repetition, "exit_code": "timeout", "process_wall_s": time.monotonic() - start}
            rows.append(row)
            save("loading.json", rows)
            print(f"LOAD r{repetition} {name}: exit={row['exit_code']} ms={row.get('load_ms')}", flush=True)
    fields = ["model", "repetition", "exit_code", "process_wall_s", "splats", "sh_degree", "scene_bytes", "load_ms"]
    with (OUT / "loading.csv").open("w", newline="", encoding="utf-8-sig") as f:
        writer = csv.DictWriter(f, fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)
    supported = [m["model"] for m in inventory if all(r["exit_code"] == 0 and "load_ms" in r for r in rows if r["model"] == m["model"])]
    save("load_failures.json", [r for r in rows if r["exit_code"] != 0 or "load_ms" not in r])
    collect("all-models-static", supported)
    # Long samples cover a smaller model where fixed interop costs matter and
    # the largest model where sorting dominates. Both formats are measured.
    selected = ["shengyi_v1.ply", "spz/shengyi_v1.spz", "jiulonghu_v1.ply", "spz/jiulonghu_v1.spz"]
    if not set(selected).issubset(supported):
        raise RuntimeError("A required representative model failed loading; inspect load_failures.json")
    collect("representative-orbit-formal", selected, formal=True, camera="orbit")
    environment["finished_local"] = time.strftime("%Y-%m-%d %H:%M:%S %z")
    environment["scene_binary_unchanged"] = sha(SCENE) == environment["scene_binary_sha256"]
    environment["scene_helper_unchanged"] = sha(SCENE.parent / "model-io-helper.exe") == environment["scene_helper_sha256"]
    save("environment.json", environment)
    print("ALL COLLECTIONS COMPLETE", flush=True)


if __name__ == "__main__":
    main()
