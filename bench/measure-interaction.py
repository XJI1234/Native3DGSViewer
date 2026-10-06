"""Fresh-process native SDK browsing, without per-frame GPU fence waits."""

import datetime
import hashlib
import json
import math
from pathlib import Path
import re
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "docs/verification/three-engine-2026-10-06"
MODEL_ROOT = Path(r"C:\Users\21544\Desktop\zhishan")
BINARY = ROOT / "out/Release/Native3DGSViewer.InteractionBench.exe"
MODELS = [
    "shengyi_v1.ply", "spz/shengyi_v1.spz",
    "he_v1.ply", "spz/he_v1.spz",
    "jiulonghu_v1.ply", "spz/jiulonghu_v1.spz",
]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def decoded(output):
    return (output or b"").decode("utf-8", errors="replace").replace("\r", "")


def parse_run(text, entry, name, repetition, log):
    loaded = re.search(r"loaded count=(\d+) degree=(\d+) decode_ms=([\d.eE+-]+) first_frame_ms=([\d.eE+-]+)", text)
    interact = re.search(r"interaction duration_ms=([\d.eE+-]+) accepted_presents=(\d+) render_calls=(\d+) fps=([\d.eE+-]+)", text)
    pose = re.search(r"^pose ([\d.eE+ .-]+)$", text, re.M)
    require(loaded and interact and pose, "Missing loaded/interaction/pose records")
    count, degree = int(loaded[1]), int(loaded[2])
    require(count == entry["count"] and degree == 3, "Count/SH mismatch")
    points = [float(value) for value in pose[1].split()]
    require(len(points) == 6, "Invalid initial pose")
    initial = {"position": points[:3], "target": points[3:], "up": [0, 1, 0]}
    samples = [[float(v) for v in line.split(",")] for line in text.splitlines()
               if re.fullmatch(r"[\d.eE+-]+,[\d.eE+-]+,\d+,[\d.eE+-]+", line)]
    require(len(samples) > 30 and float(interact[1]) >= 20000, "Incomplete sample window")
    intervals = [row[1] for row in samples]
    require(all(value > 0 for value in intervals), "Invalid frame interval")
    slow = sorted(intervals, reverse=True)[:max(1, math.ceil(len(intervals) / 100))]
    fps = float(interact[4])
    require(fps > 0, "Invalid FPS")
    return {"model": name, "repetition": repetition, "count": count, "degree": degree,
            "sha256": entry["sha256"], "inputBytes": entry["bytes"],
            "decodeMs": float(loaded[3]), "firstFrameMs": float(loaded[4]),
            "durationMs": float(interact[1]), "acceptedPresents": int(interact[2]),
            "renderCalls": int(interact[3]), "fps": fps, "frameMs": 1000 / fps,
            "onePercentLowFps": 1000 / statistics.mean(slow), "pose": initial,
            "rawFile": log}


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    report = {
        "date": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "complete": False,
        "protocol": "Public native model-loader/render-core SDK host, visible HWND DirectComposition swapchain, accepted DXGI Presents,120Hz paced input/render,5s warm+20s sample,full SH3/stride1/no mitigation,fresh process per repetition",
        "resolution": [1920, 1080], "rows": [], "failures": [],
    }
    poses = {}

    def save():
        (OUT / "windows-results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        (OUT / "reference-poses.json").write_text(json.dumps(poses, indent=2) + "\n", encoding="utf-8")

    # Invalidate old completion/poses even if setup, launch or the first process fails.
    save()
    try:
        report.update({
            "sourceCommit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
            "binarySha256": hashlib.sha256(BINARY.read_bytes()).hexdigest(),
            "sourceSha256": hashlib.sha256((ROOT / "bench/windows-interaction.cpp").read_bytes()).hexdigest(),
            "driverSha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        })
        manifest = {row["name"]: row for row in json.loads((ROOT / "ForWeb/docs/verification/evidence/model-manifest.json").read_text(encoding="utf-8"))["models"]}
        for name in MODELS:
            digest = hashlib.sha256()
            with (MODEL_ROOT / name).open("rb") as file:
                for block in iter(lambda: file.read(8 * 1024 * 1024), b""):
                    digest.update(block)
            require(digest.hexdigest() == manifest[name]["sha256"], f"Model hash mismatch: {name}")
        for repetition in range(3):
            for name in MODELS if repetition % 2 == 0 else list(reversed(MODELS)):
                print("Started", name, repetition, flush=True)
                log = name.replace("/", "_") + f"-native-{repetition}.log"
                try:
                    result = subprocess.run([str(BINARY), str(MODEL_ROOT / name)],
                                            cwd=BINARY.parent, capture_output=True, timeout=660)
                    text = decoded(result.stdout) + decoded(result.stderr)
                    (OUT / log).write_text(text, encoding="utf-8")
                    require(result.returncode == 0, text[-2000:])
                    row = parse_run(text, manifest[name], name, repetition, log)
                    poses[name] = row["pose"]
                    report["rows"].append(row)
                    print(json.dumps(row), flush=True)
                except Exception as error:
                    if isinstance(error, subprocess.TimeoutExpired):
                        (OUT / log).write_text(decoded(error.stdout) + decoded(error.stderr), encoding="utf-8")
                    report["failures"].append({"model": name, "repetition": repetition,
                                               "error": str(error), "rawFile": log})
                    raise
                finally:
                    save()
        report["complete"] = len(report["rows"]) == 18 and not report["failures"]
    except Exception as error:
        if not report["failures"]:
            report["failures"].append({"stage": "setup", "error": str(error)})
        raise
    finally:
        save()


if __name__ == "__main__":
    main()
