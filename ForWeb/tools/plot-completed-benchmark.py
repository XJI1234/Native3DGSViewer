"""Plot completed submission wall times, retaining protocol and scope caveats."""
import json
import statistics
import sys
from pathlib import Path
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

root = Path(sys.argv[1])
protocol = json.loads((root / "results.json").read_text(encoding="utf-8"))
if not protocol["complete"] or protocol["failures"]:
    raise ValueError("Incomplete benchmark")
models = protocol["requested"]["models"]
rows = {(row["model"], row["mode"]): row for row in protocol["rows"]}
fig, ax = plt.subplots(figsize=(8, 4.8), layout="constrained")
x = np.arange(len(models))
for mode, offset, color, label in [("native", -.18, "#315f8c", "WebGPU SDK"),
                                    ("spark", .18, "#b56d33", "SparkJS 2.3.1")]:
    values = [[run["metrics"]["completedMs"]["p50"] for run in rows[(model, mode)]["perRun"]]
              for model in models]
    medians = [statistics.median(series) for series in values]
    errors = [[median - min(series) for median, series in zip(medians, values)],
              [max(series) - median for median, series in zip(medians, values)]]
    bars = ax.bar(x + offset, medians, width=.34, color=color, label=label, yerr=errors, capsize=4)
    ax.bar_label(bars, labels=[f"{value:.1f}" for value in medians], padding=4)
ax.set_xticks(x, [Path(model).stem.replace("_v1", "") for model in models])
ax.set_ylabel("Completed submission wall time (ms)")
ax.spines[["top", "right"]].set_visible(False)
ax.set_axisbelow(True)
ax.grid(axis="y", alpha=.2)
ax.set_ylim(0, ax.get_ylim()[1] * 1.15)
ax.legend(frameon=False)
degrees = sorted({row['loaded']['degree'] for row in protocol['rows']})
ax.set_title(f"{protocol['resolution'][0]} x {protocol['resolution'][1]} | full SH {degrees} | "
             f"{protocol['warmup']} warmup frames + {protocol['runs']} x {protocol['frames']} samples\n"
             "Median and range across repetitions", fontsize=11)
fig.supxlabel("Includes worker update, timer observation and completion waits.\n"
              "GPU timer scopes differ; physical display FPS is unverified.", fontsize=9)
for extension in ("png", "svg"):
    fig.savefig(root / f"completed-wall-comparison.{extension}", dpi=180)
plt.close(fig)
