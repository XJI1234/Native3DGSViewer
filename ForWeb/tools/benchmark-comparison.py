"""Compare completed benchmark runs without equating GPU timing scopes."""

import json
import statistics
import sys
from pathlib import Path

root = Path(sys.argv[1])
protocol = json.loads((root / "results.json").read_text())
summaries = json.loads((root / "summary.json").read_text())
quality = json.loads((root / "image-quality.json").read_text())
by_key = {(row["model"], row["engine"]): row for row in summaries}
comparisons = []
model_names = list(dict.fromkeys(row["model"] for row in protocol["rows"]))
expected_views = {model.replace("/", "_").replace(".", "_") + suffix
                  for model in model_names for suffix in ["", "-left", "-right"]}
if {row["model"] for row in quality} != expected_views:
    raise ValueError("Incomplete three-view image comparison")
quality_pass = all(row["ssim"] >= 0.95 and row["foregroundSsim"] >= 0.95
                   for row in quality)

for model in dict.fromkeys(row["model"] for row in protocol["rows"]):
    native = by_key[(model, "native")]
    spark = by_key[(model, "spark")]
    source_rows = [row for row in protocol["rows"] if row["model"] == model]
    if len(source_rows) != 2 or any(
        row["errors"] or row["disposalErrors"] for row in source_rows
    ):
        raise ValueError(f"Incomplete or failed dual-engine run: {model}")
    if any(len(row["runs"]) != protocol["runs"] for row in (native, spark)):
        raise ValueError(f"Missing repetitions: {model}")
    n50 = statistics.median(run["p50"] for run in native["runs"])
    s50 = statistics.median(run["p50"] for run in spark["runs"])
    ratio = n50 / s50
    low_ratio = native["medianOnePercentLowFps"] / spark["medianOnePercentLowFps"]
    comparisons.append({
        "model": model,
        "nativeIntervalP50Ms": n50,
        "sparkIntervalP50Ms": s50,
        "nativeOverSparkInterval": ratio,
        "nativeIntervalReductionPercent": 100 * (1 - ratio),
        "nativeOnePercentLowFps": native["medianOnePercentLowFps"],
        "sparkOnePercentLowFps": spark["medianOnePercentLowFps"],
        "nativeMeanLoopFps": statistics.median(run["meanFps"] for run in native["runs"]),
        "sparkMeanLoopFps": statistics.median(run["meanFps"] for run in spark["runs"]),
        "nativeOverSparkOnePercentLow": low_ratio,
        "loopMeets20PercentAndTailCriterion": ratio <= 0.8 and low_ratio >= 1,
        "nativeP50RangeMs": [min(run["p50"] for run in native["runs"]),
                                max(run["p50"] for run in native["runs"])],
        "sparkP50RangeMs": [min(run["p50"] for run in spark["runs"]),
                               max(run["p50"] for run in spark["runs"])],
    })

result = {
    "metric": "requestAnimationFrame interval during sustained rendering",
    "uncappedRequested": protocol.get("uncappedRequested", False),
    "physicalPresentationVerified": False,
    "sparkSortFreshnessVerified": False,
    "gpuScopesEquivalent": False,
    "staticImageQualityCriterionMet": quality_pass,
    "quality": quality,
    "comparisons": comparisons,
}
(root / "comparison.json").write_text(json.dumps(result, indent=2))
print(json.dumps({key: value for key, value in result.items() if key != "quality"}, indent=2))
