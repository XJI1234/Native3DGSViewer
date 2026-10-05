"""Export measured phase medians; keep independent quantiles separate."""
import json
import pathlib
import statistics
import matplotlib.pyplot as plt

root = pathlib.Path(__file__).resolve().parents[1] / "docs/verification/evidence/parallel-2026-10-05"

def read(name):
    data = json.loads((root / name / "results.json").read_text(encoding="utf-8"))
    if not data["complete"] or data["failures"]:
        raise ValueError(f"Incomplete evidence: {name}")
    return data["rows"]

before = read("stages-before-persistent") + read("stages-before-persistent-large")
after = read("stages-no-clear")
fig, axes = plt.subplots(1, 2, figsize=(10, 4), layout="constrained")
for ax, model, title in zip(axes, ["zhihuizhimen.ply", "spz/jiulonghu_v1.spz"], ["3.91 million points", "22.48 million points"]):
    for offset, rows, label, color in [(-.18, before, "Before", "#8298a8"), (.18, after, "Remove redundant clears", "#197c86")]:
        row = next(row for row in rows if row["model"] == model)
        values = [statistics.median(run["metrics"][metric]["p50"] for run in row["perRun"]) for metric in ["projectionMs", "sortMs", "drawMs", "gpuMs"]]
        bars = ax.bar([i + offset for i in range(4)], values, width=.35, label=label, color=color)
        ax.bar_label(bars, fmt="%.2f", fontsize=9, padding=3)
    ax.set_xticks(range(4), ["Projection", "Sort", "Draw", "Total GPU"])
    ax.set_title(title, fontsize=13, loc="left")
    ax.set_ylabel("Completed GPU time (ms)")
    ax.set_ylim(0, ax.get_ylim()[1] * 1.15)
    ax.spines[["top", "right"]].set_visible(False)
    ax.set_axisbelow(True)
    ax.grid(axis="y", alpha=.15)
axes[0].legend(frameon=False, fontsize=9, loc="upper left")
fig.suptitle("WebGPU phase measurements | RTX 3080 / Edge 154", fontsize=15)
fig.savefig(root / "gpu-phases.png", dpi=180)
fig.savefig(root / "gpu-phases.svg")
plt.close(fig)
