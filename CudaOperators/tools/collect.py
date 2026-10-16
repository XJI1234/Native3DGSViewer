"""Collect paired offscreen experiments; never infer display FPS from GPU times."""

import argparse
import csv
import hashlib
import json
from pathlib import Path
import statistics
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model-root", type=Path, default=Path(r"C:\Users\21544\Desktop\zhishan"))
    parser.add_argument("--models", nargs="+", default=["changjin_v1.ply", "shengyi_v1.ply", "tumu_v1.ply", "zhihuizhimen.ply", "jiulonghu_v1.ply"])
    parser.add_argument("--backends", nargs="+", default=["d3d12", "interop-noop", "cuda-sort"])
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--formal", action="store_true")
    parser.add_argument("--camera", choices=["fit", "orbit"], default="fit")
    args = parser.parse_args()
    if args.repetitions < 1 or len(set(args.backends)) != len(args.backends):
        parser.error("Positive repetitions and distinct backends required")
    root = Path(__file__).resolve().parents[2]
    binary = root / "out/cmake-cuda/cuda/Release/Native3DGSViewer.CudaBench.exe"
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    temporary = root / "out/cuda-results" / output.name
    temporary.mkdir(parents=True, exist_ok=False)
    models = []
    for name in args.models:
        path = (args.model_root / name).resolve()
        if not path.is_relative_to(args.model_root.resolve()):
            parser.error("Model path escapes model root")
        models.append({"name": name, "sha256": sha(path)})
    result = {"protocol": "offscreen-serialized-full-GPU-span; no Present; no display FPS",
              "formal_duration": args.formal, "warmup_seconds": 30 if args.formal else None,
              "sample_seconds": 60 if args.formal else None, "frames": None if args.formal else 120,
              "warmup_frames": None if args.formal else 60, "camera": args.camera,
              "head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
              "diff_sha256": hashlib.sha256(subprocess.check_output(["git", "diff", "HEAD", "--binary"], cwd=root)).hexdigest(),
              "binaries": {p.name: sha(p) for p in [binary, binary.parent / "gs-cuda-backend.dll", binary.parent / "model-io-helper.exe"]},
              "shaders": {p.name: sha(p) for p in sorted((binary.parent / "shaders").glob("*.cso"))},
              "models": models, "runs": []}
    references = {}
    for repetition in range(1, args.repetitions + 1):
        ordered_models = args.models if repetition % 2 else list(reversed(args.models))
        ordered_backends = args.backends if repetition % 2 else list(reversed(args.backends))
        for name in ordered_models:
            for backend in ordered_backends:
                stem = name.replace("/", "_").replace("\\", "_") + f"-{backend}-{repetition}"
                sample = temporary / (stem + ".csv")
                command = [str(binary), "--model", str(args.model_root / name), "--backend", backend,
                           "--output", str(sample), "--camera", args.camera]
                if args.formal:
                    command += ["--warmup-seconds", "30", "--sample-seconds", "60"]
                else:
                    command += ["--frames", "120", "--warmup-frames", "60"]
                start = time.monotonic()
                process = subprocess.run(command, cwd=root, capture_output=True, text=True,
                                         encoding="utf-8", errors="replace", timeout=300)
                (output / (stem + ".log")).write_text(process.stdout + process.stderr, encoding="utf-8")
                record = {"model": name, "backend": backend, "repetition": repetition,
                          "exit_code": process.returncode, "wall_seconds": time.monotonic() - start,
                          "command": command}
                if process.returncode == 0:
                    with sample.open(newline="", encoding="utf-8") as source:
                        rows = list(csv.DictReader(source))
                    if not rows:
                        raise RuntimeError("No measured frames")
                    record["sample_count"] = len(rows)
                    record["medians"] = {key: statistics.median(float(row[key]) for row in rows) for key in
                                         ["projection_ms", "sort_and_interop_ms", "draw_ms", "gpu_total_ms", "cpu_wall_ms"]}
                    (output / sample.name).write_bytes(sample.read_bytes())
                    pixels = Path(str(sample) + ".bgra").read_bytes()
                    record["image_sha256"] = hashlib.sha256(pixels).hexdigest()
                    array = np.frombuffer(pixels, dtype=np.uint8).reshape(1080, 1920, 4)
                    Image.fromarray(array[:, :, [2, 1, 0, 3]]).save(output / (stem + ".png"))
                    if args.camera == "fit":
                        if backend == "d3d12":
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
                            r["repetition"] == run["repetition"] and r["backend"] == "d3d12")
            run["bit_identical_to_d3d12"] = run["image_sha256"] == baseline["image_sha256"]
    result["complete"] = True
    (output / "results.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
