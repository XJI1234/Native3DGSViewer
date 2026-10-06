"""Run the existing real-browser interaction harness after native measurement."""

import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "docs/verification/three-engine-2026-10-06"
MODELS = [
    "shengyi_v1.ply", "spz/shengyi_v1.spz",
    "he_v1.ply", "spz/he_v1.spz",
    "jiulonghu_v1.ply", "spz/jiulonghu_v1.spz",
]


def main():
    native = json.loads((OUT / "windows-results.json").read_text(encoding="utf-8"))
    if not native["complete"]:
        raise RuntimeError("Finish native GPU measurements before browser runs")
    settings = {
        "GS_TEST_URL": "http://127.0.0.1:5187",
        "GS_BENCH_PERSISTENT": "1",
        "GS_BENCH_FRESH_CASE": "1",
        "GS_BENCH_DECODER": "auto",
        "GS_BENCH_THREADS": "4",
        "GS_BENCH_SORTING": "adaptive",
        "GS_BENCH_FRAME_DEPTH": "2",
        "GS_BENCH_WARMUP_MS": "5000",
        "GS_BENCH_SAMPLE_MS": "20000",
        "GS_BENCH_RUNS": "1",
        "GS_BENCH_ENGINES": "native,spark",
        "GS_BENCH_POSES": str(OUT / "reference-poses.json"),
    }
    for repetition in range(3):
        directory = OUT / f"web-repeat-{repetition}"
        directory.mkdir(exist_ok=True)
        # Node setup may fail before its first save; never leave stale completion.
        (directory / "results.json").write_text(json.dumps({
            "complete": False, "rows": [], "failures": [],
            "status": "launching", "repetition": repetition,
        }) + "\n", encoding="utf-8")
        models = MODELS if repetition % 2 == 0 else list(reversed(MODELS))
        env = {**os.environ, **settings, "GS_BENCH_MODELS": ",".join(models),
               "GS_BENCH_OUTPUT": str(directory)}
        with (directory / "runner.log").open("w", encoding="utf-8") as log:
            with subprocess.Popen(["node", "tools/interactive-benchmark.mjs"],
                                  cwd=ROOT / "ForWeb", env=env,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  text=True, encoding="utf-8", errors="replace") as process:
                for line in process.stdout:
                    log.write(line)
                    log.flush()
                    print(f"repeat {repetition}: {line}", end="", flush=True)
                status = process.wait()
        report = json.loads((directory / "results.json").read_text(encoding="utf-8"))
        if status != 0 or not report["complete"]:
            raise RuntimeError(f"Incomplete browser repeat {repetition}; retain raw failures")


if __name__ == "__main__":
    main()
