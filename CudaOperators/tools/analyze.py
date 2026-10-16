"""Summarize paired rounds; frames within one round are not independent trials."""

import argparse
import csv
import json
from pathlib import Path
import statistics

import numpy as np
from PIL import Image
from skimage.metrics import structural_similarity


def quality(reference, candidate):
    a = np.asarray(Image.open(reference)).astype(np.float64)
    b = np.asarray(Image.open(candidate)).astype(np.float64)
    metrics = {}
    for label, channels in [("rgb", slice(0, 3)), ("alpha", 3)]:
        first, second = a[:, :, channels], b[:, :, channels]
        error = np.abs(first - second)
        # Exact equality has SSIM=1; avoid expensive filtering of identical
        # frames. Keep errors in integer channel units to avoid rounding a
        # threshold value (e.g. 2/255) above its exact acceptance boundary.
        ssim = 1.0 if np.array_equal(first, second) else structural_similarity(
            first, second, data_range=255, channel_axis=2 if label == "rgb" else None)
        metrics[label] = {"ssim": float(ssim), "rmse_255": float(np.sqrt(np.mean(error ** 2))),
                          "p99_error_255": float(np.quantile(error, .99)),
                          "max_error_255": float(error.max())}
    metrics["passed"] = all(m["ssim"] >= .995 and m["rmse_255"] <= 1 and
                            m["p99_error_255"] <= 2 and m["max_error_255"] <= 8
                            for key, m in metrics.items() if key != "passed")
    return metrics


def stem(run):
    return run["model"].replace("/", "_").replace("\\", "_") + f'-{run["backend"]}-{run["repetition"]}'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--reference", help="Paired timing control; defaults to collection image control")
    args = parser.parse_args()
    path = args.directory
    result = json.loads((path / "results.json").read_text(encoding="utf-8"))
    image_reference = result.get("image_reference", "d3d12")
    args.reference = args.reference or image_reference
    if not result.get("complete") or not result.get("runtime_unchanged"):
        raise RuntimeError("Incomplete collection or missing runtime integrity check")
    random = np.random.default_rng(42)
    runs = result["runs"]
    for run in runs:
        with (path / (stem(run) + ".csv")).open(newline="", encoding="utf-8") as source:
            frames = np.array([float(row["gpu_total_ms"]) for row in csv.DictReader(source)])
        slow = np.sort(frames)[-max(1, int(np.ceil(len(frames) * .01))):]
        run["tails"] = {"p95_ms": float(np.quantile(frames, .95)), "p99_ms": float(np.quantile(frames, .99)),
                        "one_percent_low_gpu_fps": float(1000 / slow.mean())}
        if result["camera"] == "fit":
            baseline = next(r for r in runs if r["model"] == run["model"] and
                            r["repetition"] == run["repetition"] and r["backend"] == image_reference)
            run["quality"] = quality(path / (stem(baseline) + ".png"), path / (stem(run) + ".png"))
    rows = []
    for model in dict.fromkeys(r["model"] for r in runs):
        for backend in dict.fromkeys(r["backend"] for r in runs):
            if backend == args.reference:
                continue
            paired = []
            low = []
            baseline_ms, candidate_ms = [], []
            selected = [r for r in runs if r["model"] == model and r["backend"] == backend]
            for run in selected:
                base = next(r for r in runs if r["model"] == model and r["backend"] == args.reference and
                            r["repetition"] == run["repetition"])
                a, b = base["medians"]["gpu_total_ms"], run["medians"]["gpu_total_ms"]
                paired.append((1 - b / a) * 100)
                low.append((run["tails"]["one_percent_low_gpu_fps"] / base["tails"]["one_percent_low_gpu_fps"] - 1) * 100)
                baseline_ms.append(a)
                candidate_ms.append(b)
            gains = np.asarray(paired)
            # Resample whole paired rounds; n=3 remains weak evidence and is reported.
            interval = None
            if len(gains) >= 3:
                estimates = random.choice(gains, size=(20000, len(gains)), replace=True).mean(axis=1)
                interval = np.quantile(estimates, [.025, .975]).tolist()
            rows.append({"model": model, "backend": backend, "rounds": len(gains),
                         "reference_backend": args.reference,
                         "reference_median_ms": statistics.median(baseline_ms),
                         "candidate_median_ms": statistics.median(candidate_ms),
                         "paired_frame_reduction_percent": paired, "mean_reduction_percent": float(gains.mean()),
                         "paired_bootstrap_95ci_percent": interval,
                         "one_percent_low_gpu_fps_change_percent": low,
                         "all_quality_passed": all(r.get("quality", {}).get("passed", False) for r in selected),
                         "all_bit_identical": all(r.get("bit_identical_to_d3d12", False) for r in selected)})
    summary = {"protocol": result["protocol"], "formal_duration": result["formal_duration"],
               "frame_slots": result.get("frame_slots", {}),
               "note": "Offscreen GPU timing; frame slots follow collection metadata. No Present or input-latency conclusion. "
                       "Bootstrap resamples whole paired rounds; small round counts may understate uncertainty. "
                       "Lifecycle/resource gates remain separate.",
               "comparisons": rows, "runs": runs}
    suffix = "" if args.reference == image_reference else "-vs-" + args.reference
    (path / ("analysis" + suffix + ".json")).write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    title = "# Paired GPU timing (formal duration)" if result["formal_duration"] else "# Paired GPU diagnostic"
    lines = [title, "", summary["note"], "",
             f"Timing reference: `{args.reference}`. Image reference: `{image_reference}`.", "",
             "| Model | Backend | Reference ms | Candidate ms | Reduction % | Paired 95% CI % | Image gate |",
             "|---|---|---:|---:|---:|---|---|"]
    for row in rows:
        interval = row["paired_bootstrap_95ci_percent"]
        interval_text = f'[{interval[0]:.2f}, {interval[1]:.2f}]' if interval else 'unavailable (<3 rounds)'
        lines.append(f'| {row["model"]} | {row["backend"]} | {row["reference_median_ms"]:.4f} | '
                     f'{row["candidate_median_ms"]:.4f} | {row["mean_reduction_percent"]:.2f} | '
                     f'{interval_text} | {row["all_quality_passed"]} |')
    (path / ("SUMMARY" + suffix + ".md")).write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
