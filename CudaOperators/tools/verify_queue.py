"""Exercise frame-slot reuse and phase draining under D3D12 validation."""

import argparse
import csv
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    root = Path(__file__).resolve().parents[2]
    temporary = root / "out/cuda-results" / args.output.name
    temporary.mkdir(parents=True, exist_ok=False)
    binary = root / "out/cmake-cuda/cuda/Release/Native3DGSViewer.CudaBench.exe"
    model = Path(r"C:\Users\21544\Desktop\zhishan\changjin_v1.ply")
    records = []
    for frames, warm in [(1, 0), (2, 1), (4, 2), (7, 4)]:
        reference = None
        for backend in ["d3d12", "d3d12-queued", "cuda-sort", "cuda-sort-graph",
                        "cuda-sort-queued", "cuda-sort-graph-queued"]:
            sample = temporary / f"{backend}-f{frames}-w{warm}.csv"
            command = [str(binary), "--model", str(model), "--backend", backend,
                       "--frames", str(frames), "--warmup-frames", str(warm),
                       "--diagnostics", "on", "--output", str(sample)]
            run = subprocess.run(command, capture_output=True, text=True, encoding="utf-8",
                                 errors="replace", timeout=60, cwd=root)
            (args.output / sample.with_suffix(".log").name).write_text(run.stdout + run.stderr, encoding="utf-8")
            record = {"command": command, "exit_code": run.returncode}
            records.append(record)
            (args.output / "results.json").write_text(json.dumps(records, indent=2), encoding="utf-8")
            if run.returncode:
                raise RuntimeError(f"Queue validation failed: {backend}")
            with sample.open(newline="", encoding="utf-8") as source:
                rows = list(csv.DictReader(source))
            assert [int(row["frame"]) for row in rows] == list(range(frames))
            assert all(float(rows[i]["time_s"]) <= float(rows[i + 1]["time_s"]) for i in range(frames - 1))
            for row in rows:
                assert all(float(row[key]) >= 0 for key in
                           ["projection_ms", "sort_and_interop_ms", "draw_ms", "cpu_submit_ms"])
                assert float(row["cpu_wall_ms"]) >= float(row["cpu_submit_ms"])
            pixels = Path(str(sample) + ".bgra").read_bytes()
            assert len(pixels) == 1920 * 1080 * 4
            if reference is None:
                reference = pixels
            assert pixels == reference, f"Queue image changed: {backend}"
            record.update(frames=len(rows), image_sha256=hashlib.sha256(pixels).hexdigest(), passed=True)
            shutil.copyfile(sample, args.output / sample.name)
            (args.output / "results.json").write_text(json.dumps(records, indent=2), encoding="utf-8")
            print(f"{backend} frames={frames} warmup={warm}: passed", flush=True)


if __name__ == "__main__":
    main()
