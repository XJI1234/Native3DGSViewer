"""Validate collected evidence and export tables/charts, without running GPU work."""
import csv
import json
import math
from pathlib import Path
import statistics as st

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from PIL import Image, ImageDraw

BASE = Path(__file__).resolve().parent


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


def write_csv(path, rows):
    with path.open("w", newline="", encoding="utf-8-sig") as f:
        writer = csv.DictWriter(f, list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def summarize(directory):
    result = read(directory / "results.json")
    analysis = read(directory / "analysis.json")
    assert result["complete"] and result["runtime_unchanged"]
    output = []
    for comparison in analysis["comparisons"]:
        name = comparison["model"]
        runs = [r for r in analysis["runs"] if r["model"] == name]
        baseline = [r for r in runs if r["backend"] == "d3d12-queued"]
        cuda = [r for r in runs if r["backend"] == comparison["backend"]]
        assert len(baseline) == len(cuda) == 3
        assert all(r["exit_code"] == 0 and r["source_count"] == runs[0]["source_count"] for r in runs)
        assert all(r["visible_count"] > 0 for r in runs), name
        med = lambda selected, key: st.median(r["medians"][key] for r in selected)
        tail = lambda selected, key: st.median(r["tails"][key] for r in selected)
        row = {"model": name, "source_count": runs[0]["source_count"],
               "d3d12_gpu_ms": med(baseline, "gpu_total_ms"), "cuda_gpu_ms": med(cuda, "gpu_total_ms"),
               "paired_reduction_percent": comparison["mean_reduction_percent"],
               "ci_low_percent": comparison["paired_bootstrap_95ci_percent"][0],
               "ci_high_percent": comparison["paired_bootstrap_95ci_percent"][1],
               "d3d12_projection_ms": med(baseline, "projection_ms"), "cuda_projection_ms": med(cuda, "projection_ms"),
               "d3d12_sort_ms": med(baseline, "sort_and_interop_ms"), "cuda_sort_and_interop_ms": med(cuda, "sort_and_interop_ms"),
               "d3d12_draw_ms": med(baseline, "draw_ms"), "cuda_draw_ms": med(cuda, "draw_ms"),
               "d3d12_cpu_submit_ms": med(baseline, "cpu_submit_ms"), "cuda_cpu_submit_ms": med(cuda, "cpu_submit_ms"),
               "d3d12_p95_ms": tail(baseline, "p95_ms"), "cuda_p95_ms": tail(cuda, "p95_ms"),
               "d3d12_p99_ms": tail(baseline, "p99_ms"), "cuda_p99_ms": tail(cuda, "p99_ms"),
               "d3d12_slowest_1pct_mean_gpu_ms": st.median(1000/r["tails"]["one_percent_low_gpu_fps"] for r in baseline),
               "cuda_slowest_1pct_mean_gpu_ms": st.median(1000/r["tails"]["one_percent_low_gpu_fps"] for r in cuda),
               "d3d12_local_peak_GiB": max(r["memory"]["sampled_local_peak_bytes"] for r in baseline)/2**30,
               "cuda_local_peak_GiB": max(r["memory"]["sampled_local_peak_bytes"] for r in cuda)/2**30,
               "d3d12_process_peak_working_set_GiB": max(r["memory"]["process_peak_working_set_bytes"] for r in baseline)/2**30,
               "cuda_process_peak_working_set_GiB": max(r["memory"]["process_peak_working_set_bytes"] for r in cuda)/2**30,
               "static_quality_passed": comparison["all_quality_passed"] if result["camera"] == "fit" else "not-compared-moving-final-frame",
               "static_bit_identical": comparison["all_bit_identical"] if result["camera"] == "fit" else "not-compared-moving-final-frame",
               "min_nonzero_alpha_fraction": min(r["nonzero_alpha_pixel_fraction"] for r in runs),
               "sample_frames": sum(r["sample_count"] for r in runs)}
        output.append(row)
    return result, output


def main():
    inputs = read(BASE / "inputs.json")
    loading = read(BASE / "loading.json")
    assert len(inputs) == 42 and len(loading) == 126
    assert all(r["exit_code"] == 0 for r in loading)
    static, rows = summarize(BASE / "all-models-static")
    assert len(rows) == 42 and len(static["runs"]) == 252
    assert all(r["static_bit_identical"] and r["static_quality_passed"] for r in rows)
    assert sum(r["sample_frames"] for r in rows) == 30240
    by_name = {r["model"]: r for r in rows}
    load_rows = []
    for model in inputs:
        selected = [r for r in loading if r["model"] == model["model"]]
        assert len(selected) == 3 and len({r["splats"] for r in selected}) == 1
        name = model["model"]
        assert selected[0]["splats"] == by_name[name]["source_count"]
        by_name[name]["file_MiB"] = model["bytes"]/2**20
        by_name[name]["SH_degree"] = selected[0]["sh_degree"]
        by_name[name]["decode_median_ms"] = st.median(r["load_ms"] for r in selected)
        by_name[name]["sha256"] = model["sha256"]
        load_rows.append({"model": name, "file_MiB": model["bytes"]/2**20,
                          "splats": selected[0]["splats"], "SH_degree": selected[0]["sh_degree"],
                          "decode_median_ms": st.median(r["load_ms"] for r in selected),
                          "decode_min_ms": min(r["load_ms"] for r in selected),
                          "decode_max_ms": max(r["load_ms"] for r in selected), "sha256": model["sha256"]})
    write_csv(BASE / "model-summary.csv", sorted(rows, key=lambda r: (r["source_count"], r["model"])))
    write_csv(BASE / "loading-summary.csv", load_rows)
    formal = read(BASE / "representative-orbit-formal/results.json")
    # User explicitly stopped extended tests; do not promote partial runs.
    assert not formal.get("complete", False)
    pairs = []
    for spz in (r for r in load_rows if r["model"].startswith("spz/")):
        stem = Path(spz["model"]).stem.replace("_spzv4", "_v1")
        ply_name = stem + ".ply"
        if stem == "zhihuizhimen_v1":
            ply_name = "zhihuizhimen.ply"
        if ply_name not in by_name:
            continue
        ply = next(r for r in load_rows if r["model"] == ply_name)
        pairs.append({"ply": ply_name, "spz": spz["model"], "ply_splats": ply["splats"], "spz_splats": spz["splats"], "equal_point_count": ply["splats"] == spz["splats"],
                      "compression_ratio_ply_over_spz": ply["file_MiB"]/spz["file_MiB"],
                      "spz_decode_time_over_ply": spz["decode_median_ms"]/ply["decode_median_ms"],
                      "ply_decode_ms": ply["decode_median_ms"], "spz_decode_ms": spz["decode_median_ms"],
                      "d3d12_spz_gpu_over_ply": by_name[spz["model"]]["d3d12_gpu_ms"]/by_name[ply_name]["d3d12_gpu_ms"],
                      "cuda_spz_gpu_over_ply": by_name[spz["model"]]["cuda_gpu_ms"]/by_name[ply_name]["cuda_gpu_ms"]})
    write_csv(BASE / "format-pairs.csv", pairs)
    verification = {"inputs": len(inputs), "PLY_files": sum(r["model"].endswith(".ply") for r in inputs),
                    "SPZ_files": sum(r["model"].endswith(".spz") for r in inputs),
                    "load_runs": len(loading), "static_runs": len(static["runs"]),
                    "static_sample_frames": sum(r["sample_frames"] for r in rows),
                    "static_all_bit_identical": all(r["static_bit_identical"] for r in rows),
                    "formal_runs": len(formal["runs"]), "formal_sample_frames": sum(r["sample_count"] for r in formal["runs"]),
                    "static_improved_file_count": sum(r["paired_reduction_percent"] > 0 for r in rows),
                    "static_regressed_file_count": sum(r["paired_reduction_percent"] < 0 for r in rows),
                    "static_runtime_unchanged": static["runtime_unchanged"], "extended_collection_stopped_at_user_request": True, "formal_collection_complete": False}
    (BASE / "verification.json").write_text(json.dumps(verification, indent=2)+"\n", encoding="utf-8")
    # Figures are standalone exports; GPU timing is never labelled display FPS.
    plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 10, "axes.spines.top": False,
                         "axes.spines.right": False, "figure.facecolor": "white"})
    reps = ["changjin_v1.ply", "shengyi_v1.ply", "spz/shengyi_v1.spz", "tumu_v1.ply", "spz/tumu_v1.spz",
            "zhihuizhimen.ply", "spz/zhihuizhimen_spzv4.spz", "jiulonghu_v1.ply", "spz/jiulonghu_v1.spz"]
    fig, axes = plt.subplots(2, 2, figsize=(15, 11), constrained_layout=True)
    y = np.arange(len(reps))
    labels = [Path(n).name for n in reps]
    ax = axes[0, 0]
    ax.barh(y-.18, [by_name[n]["d3d12_gpu_ms"] for n in reps], height=.35, label="DX12", color="#476c9b")
    ax.barh(y+.18, [by_name[n]["cuda_gpu_ms"] for n in reps], height=.35, label="CUDA sort + DX12", color="#b46c37")
    ax.set_yticks(y, labels); ax.invert_yaxis(); ax.set_xscale("log")
    ax.set_xlabel("Full GPU frame (ms), log scale"); ax.set_title("A. Fixed camera; full recompute, 3 x 120 frames")
    ax.legend()
    ax = axes[0, 1]
    gains = [by_name[n]["paired_reduction_percent"] for n in reps]
    ax.barh(y, gains, color=["#568365" if g > 0 else "#b45a50" for g in gains])
    ax.set_yticks(y, labels); ax.invert_yaxis(); ax.axvline(0, color="#333333", linewidth=.8)
    ax.set_xlabel("GPU time reduction (%); negative = slower"); ax.set_title("B. Paired reduction vs DX12")
    for i,g in enumerate(gains): ax.text(g, i, f" {g:+.1f}% ", va="center", ha="left" if g>=0 or g < -10 else "right", color="white" if g < -10 else "black", fontsize=9)
    ax = axes[1, 0]
    selected = [next(p for p in pairs if p["ply"] == name) for name in ["shengyi_v1.ply", "tumu_v1.ply", "zhihuizhimen.ply", "jiulonghu_v1.ply"]]
    x=np.arange(len(selected))
    ax.bar(x-.18, [p["ply_decode_ms"]/1000 for p in selected], width=.35, label="PLY", color="#476c9b")
    ax.bar(x+.18, [p["spz_decode_ms"]/1000 for p in selected], width=.35, label="SPZ", color="#b46c37")
    ax.set_xticks(x, [p["ply"].replace("_v1.ply", "").replace(".ply", "") for p in selected], rotation=15)
    ax.set_ylabel("Shared CPU decode (s)"); ax.set_title("C. Loading: 3-run medians, OS cache uncontrolled"); ax.legend()
    ax = axes[1, 1]
    memory_names = ["shengyi_v1.ply", "tumu_v1.ply", "zhihuizhimen.ply", "jiulonghu_v1.ply"]
    x=np.arange(len(memory_names))
    ax.bar(x-.18, [by_name[n]["d3d12_local_peak_GiB"] for n in memory_names], width=.35, label="DX12", color="#476c9b")
    ax.bar(x+.18, [by_name[n]["cuda_local_peak_GiB"] for n in memory_names], width=.35, label="CUDA sort + DX12", color="#b46c37")
    ax.set_xticks(x, [Path(n).stem for n in memory_names], rotation=18)
    ax.set_ylabel("Sampled process local VRAM peak (GiB)"); ax.set_title("D. GPU memory cost"); ax.legend()
    fig.suptitle("RTX 3080 10 GB | 1920 x 1080 | SH3, full points | Offscreen; no display FPS claim", fontsize=14)
    fig.savefig(BASE / "performance-overview.png", dpi=150); plt.close(fig)
    # Contact sheet exposes empty/edge-only framing and compares visible content.
    thumb_w, thumb_h = 480, 270
    sheet = Image.new("RGB", (thumb_w*3, (thumb_h+30)*math.ceil(len(rows)/3)), "#eeeeee")
    draw = ImageDraw.Draw(sheet)
    for i,row in enumerate(sorted(rows, key=lambda r: (r["source_count"], r["model"]))):
        stem = row["model"].replace("/", "_")+"-d3d12-queued-1.png"
        image = Image.open(BASE / "all-models-static" / stem).convert("RGB")
        image.thumbnail((thumb_w,thumb_h))
        xx, yy = i%3*thumb_w, i//3*(thumb_h+30)
        sheet.paste(image, (xx,yy+30)); draw.text((xx+5,yy+6), row["model"], fill="#222222")
    sheet.save(BASE / "all-models-contact-sheet.jpg", quality=90)
    print(json.dumps(verification, indent=2))


if __name__ == "__main__":
    main()
