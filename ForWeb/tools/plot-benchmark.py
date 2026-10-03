"""Plot three-run rendering-loop evidence; bars show medians, whiskers ranges."""

import json
import statistics
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

root = Path(sys.argv[1])
rows = json.loads((root / "summary.json").read_text())
models = list(dict.fromkeys(row["model"] for row in rows))
lookup = {(row["model"], row["engine"]): row for row in rows}
fig, axes = plt.subplots(1, 2, figsize=(10, 4.4), layout="constrained")
x = np.arange(len(models))
labels = [Path(model).stem.replace("_v1", "") for model in models]

for engine, offset, color, label in [
    ("native", -0.18, "#315f8c", "WebGPU engine"),
    ("spark", 0.18, "#b56d33", "SparkJS 2.3.1"),
]:
    for axis, metric in zip(axes, ["p50", "onePercentLowFps"]):
        series = [[run[metric] for run in lookup[(model, engine)]["runs"]]
                  for model in models]
        medians = np.array([statistics.median(values) for values in series])
        limits = np.array([[median - min(values) for median, values in zip(medians, series)],
                           [max(values) - median for median, values in zip(medians, series)]])
        bars = axis.bar(x + offset, medians, width=0.34, color=color, label=label,
                        yerr=limits, capsize=3)
        axis.bar_label(bars, labels=[f"{value:.2f}" for value in medians],
                       padding=4, fontsize=8)

for axis in axes:
    axis.set_xticks(x, labels)
    axis.spines[["top", "right"]].set_visible(False)
    axis.set_axisbelow(True)
    axis.grid(axis="y", alpha=0.2)
    axis.set_ylim(0, axis.get_ylim()[1] * 1.18)
axes[0].set_title("Median rAF interval (lower is faster)")
axes[0].set_ylabel("Milliseconds")
axes[1].set_title("1% low rendering-loop throughput")
axes[1].set_ylabel("Callbacks / second")
axes[0].legend(frameon=False, fontsize=8)
fig.suptitle("1920 × 1080 · full SH3 · 30 s warmup + 3 × 60 s\n"
             "Median and range across repetitions; physical presentation unverified",
             fontsize=11)
for extension in ["png", "svg"]:
    output = root / f"rendering-loop-comparison.{extension}"
    fig.savefig(output, dpi=180)
    if extension == "svg":
        output.write_text("\n".join(line.rstrip() for line in output.read_text().splitlines()) + "\n")
plt.close(fig)
print(f"Saved rendering-loop comparison to {root}")
