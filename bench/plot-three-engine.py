"""Validate the complete measured matrix and generate one two-panel bar figure."""

import csv
import hashlib
import json
import math
from pathlib import Path
import re
import statistics

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "docs/verification/three-engine-2026-10-06"
MODELS = [
    "shengyi_v1.ply", "spz/shengyi_v1.spz",
    "he_v1.ply", "spz/he_v1.spz",
    "jiulonghu_v1.ply", "spz/jiulonghu_v1.spz",
]
ENGINES = ["Windows", "Web SDK", "SparkJS"]
COLORS = ["#3266af", "#008779", "#d5762d"]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def main():
    manifest = {row["name"]: row for row in json.loads(
        (ROOT / "ForWeb/docs/verification/evidence/model-manifest.json").read_text(
            encoding="utf-8"))["models"]}
    native = json.loads((OUT / "windows-results.json").read_text(encoding="utf-8"))
    require(native["complete"] and not native["failures"], "Native results incomplete")
    require(native["resolution"] == [1920, 1080], "Native resolution differs")
    pose_bytes = (OUT / "reference-poses.json").read_bytes()
    reference_poses = json.loads(pose_bytes)
    pose_hash = hashlib.sha256(pose_bytes).hexdigest()
    runs = []
    for row in native["rows"]:
        require(row["pose"] == reference_poses[row["model"]], "Native initial pose differs")
        log = (OUT / row["rawFile"]).read_text(encoding="utf-8")
        intervals = [float(line.split(",")[1]) for line in log.splitlines()
                     if re.fullmatch(r"[\d.eE+-]+,[\d.eE+-]+,\d+,[\d.eE+-]+", line)]
        require(len(intervals) > 30, "Native raw frame intervals missing")
        slow = sorted(intervals, reverse=True)[:max(1, math.ceil(len(intervals) / 100))]
        runs.append({"model": row["model"], "engine": "Windows",
                     "repetition": row["repetition"], "count": row["count"],
                     "degree": row["degree"], "sha256": row["sha256"],
                     "loadMs": row["firstFrameMs"], "fps": row["fps"],
                     "onePercentLowFps": 1000 / statistics.mean(slow),
                     "source": row["rawFile"]})
    browsers = []
    browser_source_hashes = None
    for repetition in range(3):
        path = OUT / f"web-repeat-{repetition}/results.json"
        browser = json.loads(path.read_text(encoding="utf-8"))
        require(browser["complete"] and not browser["failures"], str(path))
        require(browser["freshCase"] and browser["persistent"] and
                not browser["headless"] and browser["sorting"] == "adaptive" and
                browser["decoder"] == "auto" and browser["threads"] == 4 and
                browser["resolution"] == [1920, 1080] and browser["warmupMs"] == 5000 and
                browser["sampleMs"] == 20000 and browser["runs"] == 1 and
                browser["frameDepth"] == 2, "Browser settings mismatch")
        require(browser["provenance"]["poseOverride"]["sha256"] == pose_hash,
                "Browser pose file provenance differs")
        source_hashes = {entry["path"]: entry["sha256"] for entry in browser["provenance"]["files"]}
        if browser_source_hashes is None:
            browser_source_hashes = source_hashes
        require(source_hashes == browser_source_hashes, "Browser source changed across repetitions")
        browsers.append(browser["browser"])
        for row in browser["rows"]:
            require(not row["errors"] and len(row["perRun"]) == 1, "Browser case failed")
            sample = row["perRun"][0]
            require(sample["durationMs"] >= 20000, "Truncated sampling window")
            loaded = row["loaded"]
            require(loaded["pose"] == reference_poses[row["model"]], "Browser initial pose differs")
            require(loaded["count"] == row["count"], "Loaded count differs from manifest")
            runs.append({"model": row["model"],
                         "engine": "Web SDK" if row["mode"] == "native" else "SparkJS",
                         "repetition": repetition, "count": loaded["count"],
                         "degree": loaded["degree"], "sha256": row["sha256"],
                         "loadMs": loaded["firstFrameMs"],
                         "fps": sample["submissionFps"],
                         "onePercentLowFps": sample["onePercentLowFps"],
                         "source": f"web-repeat-{repetition}/{row['rawFile']}"})
    require(len(runs) == 54, "Need 6 models x 3 engines x 3 repetitions")
    for run in runs:
        entry = manifest[run["model"]]
        require(run["count"] == entry["count"] and run["degree"] == 3 and
                run["sha256"] == entry["sha256"], "Model/count/SH/source mismatch")
        for metric in ["loadMs", "fps", "onePercentLowFps"]:
            require(math.isfinite(run[metric]) and run[metric] > 0, f"Invalid {metric}")
        run["frameMs"] = 1000 / run["fps"]
    with (OUT / "runs.csv").open("w", newline="", encoding="utf-8-sig") as file:
        writer = csv.DictWriter(file, fieldnames=list(runs[0]))
        writer.writeheader()
        writer.writerows(runs)
    summaries = []
    for model in MODELS:
        for engine in ENGINES:
            subset = [run for run in runs if run["model"] == model and run["engine"] == engine]
            require(len(subset) == 3 and {r["repetition"] for r in subset} == {0, 1, 2},
                    f"Duplicate/missing repetitions: {model} {engine}")
            summary = {"model": model, "engine": engine,
                       "count": manifest[model]["count"], "inputBytes": manifest[model]["bytes"]}
            for metric in ["loadMs", "frameMs", "fps", "onePercentLowFps"]:
                values = [run[metric] for run in subset]
                summary.update({f"{metric}Median": statistics.median(values),
                                f"{metric}Min": min(values), f"{metric}Max": max(values)})
            summaries.append(summary)
    with (OUT / "summary.csv").open("w", newline="", encoding="utf-8-sig") as file:
        writer = csv.DictWriter(file, fieldnames=list(summaries[0]))
        writer.writeheader()
        writer.writerows(summaries)
    (OUT / "summary.json").write_text(json.dumps({"complete": True,
        "browsers": browsers, "aggregation": "median, range of 3 independent fresh runs",
        "rows": summaries}, indent=2) + "\n", encoding="utf-8")

    plt.rcParams.update({"font.family": "Microsoft YaHei", "font.size": 11,
                         "axes.unicode_minus": False, "svg.fonttype": "none",
                         "pdf.fonttype": 42})
    fig, axes = plt.subplots(2, 1, figsize=(16, 10.6), sharex=True)
    fig.patch.set_facecolor("#fafbfc")
    x = np.arange(len(MODELS))
    width = 0.245
    lookup = {(s["model"], s["engine"]): s for s in summaries}
    for ax, metric, divisor, title, unit in [
        (axes[0], "loadMs", 1000, "完整加载  ·  文件读取 → 解码 → 上传 → 首个完整 GPU 帧", "秒"),
        (axes[1], "frameMs", 1, "连续浏览帧时间  ·  1000 / 实测浏览 FPS", "毫秒 / 帧"),
    ]:
        maxima = []
        for index, (engine, color) in enumerate(zip(ENGINES, COLORS)):
            rows = [lookup[(model, engine)] for model in MODELS]
            values = np.array([r[f"{metric}Median"] / divisor for r in rows])
            low = np.array([r[f"{metric}Min"] / divisor for r in rows])
            high = np.array([r[f"{metric}Max"] / divisor for r in rows])
            bars = ax.bar(x + (index - 1) * width, values, width=width, label=engine,
                          color=color, zorder=3,
                          yerr=np.stack([values - low, high - values]),
                          error_kw={"ecolor": "#293344", "capsize": 3, "linewidth": 1})
            for bar, value, upper in zip(bars, values, high):
                ax.annotate(f"{value:.2f}", (bar.get_x() + bar.get_width()/2, upper),
                            xytext=(0, 7), textcoords="offset points", ha="center",
                            va="bottom", fontsize=10, color="#243247")
            maxima.extend(high)
        ax.set_ylim(0, max(maxima) * 1.23)
        ax.set_title(title + "  （越低越好）", loc="left", fontsize=14, pad=16)
        ax.set_ylabel(unit)
        ax.set_axisbelow(True)
        ax.grid(axis="y", color="#dde3ea", linewidth=.8)
        for spine in ["top", "right"]:
            ax.spines[spine].set_visible(False)
        ax.spines["left"].set_color("#c1cad6")
        ax.spines["bottom"].set_color("#c1cad6")
        ax.tick_params(axis="x", length=0, pad=12)
        for boundary in [.5, 2.5, 4.5]:
            if boundary > .5:
                ax.axvline(boundary, color="#d3dbe5", linewidth=.8, zorder=0)
    labels = ["小 · PLY\nshengyi / 0.805M", "小 · SPZ\nshengyi / 0.805M",
              "中 · PLY\nhe / 2.973M", "中 · SPZ\nhe / 2.973M",
              "大 · PLY\njiulonghu / 22.480M", "大 · SPZ\njiulonghu / 22.480M"]
    axes[1].set_xticks(x, labels)
    axes[0].legend(loc="upper left", frameon=False, ncol=3)
    fig.suptitle("Windows 原生 / Web SDK / SparkJS：同机连续交互实测", x=.07,
                 y=.97, ha="left", fontsize=21, fontweight="bold", color="#1e2e43")
    fig.text(.07, .922, "RTX 3080 10GB · Ryzen 7 5700G · 1920×1080 · SH3 / 全量点 · 3 次独立重复",
             fontsize=12, color="#526277")
    fig.text(.07, .057, "柱高为中位数，误差线为最小–最大值；每次预热 5 秒、连续旋转 / 缩放 / 平移采样 20 秒。",
             fontsize=10, color="#526277")
    fig.text(.07, .028, "原生 SDK 宿主按 120 Hz 调度；浏览器保留生产调度。FPS 为 Present / 渲染提交节奏，非物理扫描输出；不对齐画质。",
             fontsize=10, color="#526277")
    fig.subplots_adjust(left=.07, right=.985, top=.85, bottom=.145, hspace=.28)
    for extension in ["png", "svg", "pdf"]:
        fig.savefig(OUT / f"three-engine-comparison.{extension}", dpi=180,
                    facecolor=fig.get_facecolor())
    plt.close(fig)
    print(json.dumps(summaries, indent=2))


if __name__ == "__main__":
    main()
