"""Collect paired offscreen experiments; never infer display FPS from GPU times."""

import argparse
import csv
import hashlib
import json
import math
import re
from pathlib import Path
import statistics
import shutil
import subprocess
import time

import numpy as np
from PIL import Image


def sha(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(8 * 1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def gpu_snapshot():
    command = ["nvidia-smi", "--query-gpu=name,driver_version,memory.used,temperature.gpu,clocks.sm,power.draw",
               "--format=csv,noheader,nounits"]
    try:
        run = subprocess.run(command, capture_output=True, text=True, timeout=10)
        return {"command": command, "exit_code": run.returncode, "output": run.stdout + run.stderr}
    except (OSError, subprocess.TimeoutExpired) as error:
        return {"command": command, "unavailable": str(error)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model-root", type=Path, default=Path(r"C:\Users\21544\Desktop\zhishan"))
    parser.add_argument("--models", nargs="+", default=["changjin_v1.ply", "shengyi_v1.ply", "tumu_v1.ply", "zhihuizhimen.ply", "jiulonghu_v1.ply"])
    parser.add_argument("--backends", nargs="+", default=["d3d12", "interop-noop", "cuda-sort"])
    parser.add_argument("--image-reference", choices=["d3d12", "d3d12-queued"], default="d3d12")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--binary", type=Path, help="Source executable; copied with its runtime files before collection")
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--formal", action="store_true")
    parser.add_argument("--kernel-timing", action="store_true", help="CUDA event diagnostics; excluded from formal timing")
    parser.add_argument("--camera", choices=["fit", "orbit"], default="fit")
    parser.add_argument("--pan-x", type=float, default=0)
    parser.add_argument("--yaw-degrees", type=float, default=0)
    parser.add_argument("--pitch-degrees", type=float, default=0)
    parser.add_argument("--dolly-steps", type=float, default=0)
    parser.add_argument("--tile-capacity", type=int, help="Explicit fixed reference capacity for tile diagnostics")
    args = parser.parse_args()
    if args.repetitions < 1 or len(set(args.backends)) != len(args.backends) or args.image_reference not in args.backends:
        parser.error("Positive repetitions, distinct backends and the selected D3D12 image reference required")
    if args.formal and args.kernel_timing:
        parser.error("CUDA event diagnostics must run separately from formal timing")
    root = Path(__file__).resolve().parents[2]
    source_binary = (args.binary or root / "out/cmake-cuda/cuda/Release/Native3DGSViewer.CudaBench.exe").resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    temporary = root / "out/cuda-results" / output.name
    temporary.mkdir(parents=True, exist_ok=False)
    runtime = temporary / "runtime"
    shutil.copytree(source_binary.parent, runtime)
    binary = runtime / source_binary.name
    runtime_hashes = {str(p.relative_to(runtime)): sha(p) for p in sorted(runtime.rglob("*")) if p.is_file()}
    models = []
    for name in args.models:
        path = (args.model_root / name).resolve()
        if not path.is_relative_to(args.model_root.resolve()):
            parser.error("Model path escapes model root")
        models.append({"name": name, "sha256": sha(path)})
    result = {"protocol": "offscreen-full-GPU-span; per-backend frame slots; no Present; no display FPS",
              "frame_slots": {backend: 3 if backend.endswith("queued") else 1 for backend in args.backends},
              "image_reference": args.image_reference,
              "cpu_wall_semantics": "submit start to CPU fence resolution; includes queue residence and resolution delay",
              "cpu_submit_semantics": "CPU command recording and submission, excluding slot-recycle fence wait",
              "formal_duration": args.formal, "cuda_event_diagnostics": args.kernel_timing,
              "warmup_seconds": 30 if args.formal else None,
              "sample_seconds": 60 if args.formal else None, "frames": None if args.formal else 120,
              "warmup_frames": None if args.formal else 60, "camera": args.camera, "pan_x": args.pan_x,
              "yaw_degrees": args.yaw_degrees, "pitch_degrees": args.pitch_degrees, "dolly_steps": args.dolly_steps,
              "tile_capacity": args.tile_capacity,
              "head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
              "diff_sha256": hashlib.sha256(subprocess.check_output(["git", "diff", "HEAD", "--binary"], cwd=root)).hexdigest(),
              "binaries": {p.name: sha(p) for p in [binary, binary.parent / "gs-cuda-backend.dll", binary.parent / "model-io-helper.exe"]},
              "shaders": {p.name: sha(p) for p in sorted((binary.parent / "shaders").glob("*.cso"))},
              "runtime_sha256": runtime_hashes, "models": models, "runs": []}
    result["collector_sha256"] = sha(Path(__file__))
    references = {}
    for repetition in range(1, args.repetitions + 1):
        ordered_models = args.models if repetition % 2 else list(reversed(args.models))
        ordered_backends = args.backends if repetition % 2 else list(reversed(args.backends))
        for name in ordered_models:
            for backend in ordered_backends:
                stem = name.replace("/", "_").replace("\\", "_") + f"-{backend}-{repetition}"
                sample = temporary / (stem + ".csv")
                command = [str(binary), "--model", str(args.model_root / name), "--backend", backend,
                           "--output", str(sample), "--camera", args.camera, "--pan-x", str(args.pan_x)]
                for flag, value in [("--yaw-degrees", args.yaw_degrees), ("--pitch-degrees", args.pitch_degrees),
                                    ("--dolly-steps", args.dolly_steps)]:
                    if value:
                        command += [flag, str(value)]
                if args.kernel_timing:
                    command += ["--cuda-events", "on"]
                if args.tile_capacity is not None:
                    command += ["--tile-capacity", str(args.tile_capacity)]
                if args.formal:
                    command += ["--warmup-seconds", "30", "--sample-seconds", "60"]
                else:
                    command += ["--frames", "120", "--warmup-frames", "60"]
                # Outside the benchmark process, so this query never overlaps
                # warmup or the timed sample. These are boundary snapshots only.
                before_gpu = gpu_snapshot()
                start = time.monotonic()
                process = subprocess.run(command, cwd=root, capture_output=True, text=True,
                                         encoding="utf-8", errors="replace", timeout=300)
                (output / (stem + ".log")).write_text(process.stdout + process.stderr, encoding="utf-8")
                record = {"model": name, "backend": backend, "repetition": repetition,
                          "exit_code": process.returncode, "wall_seconds": time.monotonic() - start,
                          "command": command}
                record["gpu_before"] = before_gpu
                record["gpu_after"] = gpu_snapshot()
                visibility = re.search(r"visible=(\d+) total=(\d+)", process.stdout)
                if visibility:
                    record["visible_count"], record["source_count"] = map(int, visibility.groups())
                    if record["visible_count"] > record["source_count"]:
                        raise RuntimeError("Visible count exceeds source count")
                if process.returncode == 0:
                    with sample.open(newline="", encoding="utf-8") as source:
                        rows = list(csv.DictReader(source))
                    if not rows:
                        raise RuntimeError("No measured frames")
                    if [int(row["frame"]) for row in rows] != list(range(len(rows))):
                        raise RuntimeError("Missing, duplicated or reordered measured frames")
                    if any(float(a["time_s"]) > float(b["time_s"]) for a, b in zip(rows, rows[1:])):
                        raise RuntimeError("Camera timestamps are not monotonic")
                    if not args.formal and len(rows) != 120:
                        raise RuntimeError("Unexpected diagnostic sample count")
                    for row in rows:
                        if not all(math.isfinite(float(row[key])) and float(row[key]) >= 0 for key in
                                   ["projection_ms", "sort_and_interop_ms", "draw_ms", "gpu_total_ms", "cpu_wall_ms"]):
                            raise RuntimeError("Invalid timing sample")
                    record["sample_count"] = len(rows)
                    record["medians"] = {key: statistics.median(float(row[key]) for row in rows) for key in
                                         ["projection_ms", "sort_and_interop_ms", "draw_ms", "gpu_total_ms", "cpu_wall_ms"]}
                    for key in ["cpu_submit_ms", "cuda_projection_kernel_ms", "cuda_sort_kernel_ms", "cuda_tile_list_kernel_ms", "cuda_composite_kernel_ms"]:
                        if key in rows[0]:
                            record["medians"][key] = statistics.median(float(row[key]) for row in rows)
                    (output / sample.name).write_bytes(sample.read_bytes())
                    memory_path = Path(str(sample) + ".memory.csv")
                    if memory_path.exists():
                        shutil.copyfile(memory_path, output / memory_path.name)
                        with memory_path.open(newline="", encoding="utf-8") as source:
                            memory = list(csv.DictReader(source))
                        record["memory"] = {"sampled_local_peak_bytes": max(int(r["local_usage"]) for r in memory),
                                            "sampled_nonlocal_peak_bytes": max(int(r["nonlocal_usage"]) for r in memory),
                                            "process_peak_private_bytes": max(int(r["peak_private_bytes"]) for r in memory),
                                            "process_peak_working_set_bytes": max(int(r["peak_working_set"]) for r in memory)}
                    pixels = Path(str(sample) + ".bgra").read_bytes()
                    record["image_sha256"] = hashlib.sha256(pixels).hexdigest()
                    array = np.frombuffer(pixels, dtype=np.uint8).reshape(1080, 1920, 4)
                    record["nonzero_alpha_pixel_fraction"] = float(np.count_nonzero(array[:, :, 3]) / (1080 * 1920))
                    Image.fromarray(array[:, :, [2, 1, 0, 3]]).save(output / (stem + ".png"))
                    if args.camera == "fit":
                        if backend == args.image_reference:
                            references[(name, repetition)] = pixels
                        baseline = references.get((name, repetition))
                        if baseline is not None:
                            difference = np.abs(array.astype(np.int16) - np.frombuffer(baseline, dtype=np.uint8).reshape(array.shape))
                            record["max_channel_error_255"] = int(difference.max())
                result["runs"].append(record)
                (output / "results.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
                print(f"{name} {backend} r{repetition}: exit={process.returncode}, gpu={record.get('medians', {}).get('gpu_total_ms')}", flush=True)
                if process.returncode:
                    raise RuntimeError(f"Experiment failed: {stem}; original log retained")
    # Reverse-order runs may encounter CUDA before their D3D12 reference.
    if args.camera == "fit":
        for run in result["runs"]:
            baseline = next(r for r in result["runs"] if r["model"] == run["model"] and
                            r["repetition"] == run["repetition"] and r["backend"] == args.image_reference)
            run["bit_identical_to_d3d12"] = run["image_sha256"] == baseline["image_sha256"]
    after = {str(p.relative_to(runtime)): sha(p) for p in sorted(runtime.rglob("*")) if p.is_file()}
    result["runtime_unchanged"] = after == runtime_hashes
    if not result["runtime_unchanged"]:
        raise RuntimeError("Runtime changed during collection; results invalid")
    result["complete"] = True
    (output / "results.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
