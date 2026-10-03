import argparse
import csv
import json
import re
import numpy as np
from pathlib import Path
from benchmark import quality


def main():
    parser = argparse.ArgumentParser(description="Recompute full RGB, foreground RGB and cropped local-window SSIM")
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    summaries = json.loads((args.directory / "summary.json").read_text(encoding="utf-8"))
    if not summaries:
        print("No successful results to reanalyze")
        return
    for row in summaries:
        model = Path(row["model"])
        directory = args.directory / (model.stem + "-" + model.suffix[1:])
        base = f"{row['resolution']}-v{row['view']}-raw{row['raw']}"
        row.update(quality(directory / f"{base}-reference.ppm", directory / f"{base}-{row['profile']}.ppm"))
        iterations = [json.loads(line) for line in (directory / f"{base}-{row['profile']}.jsonl").read_text(encoding="utf-8").splitlines() if line.startswith("{")]
        row["client_init_ms"] = iterations[0]["init_ms"]
        row["source_splats"] = iterations[0]["server"]["source_count"]
        worker_log = (directory / f"worker-raw{row['raw']}.stderr").read_text(encoding="utf-8", errors="replace")
        loaded = re.search(r"Loaded (\d+) splats, SH(\d+), load/upload ([0-9.eE+-]+) ms", worker_log)
        if loaded:
            row["source_sh_degree"] = int(loaded[2])
            row["load_upload_ms"] = float(loaded[3])
        row["cold_client_init_plus_ready_ms"] = iterations[0]["init_ms"] + iterations[0]["ready_with_capture_ms"]
        measured = iterations[row["warmup_discarded"]:]
        for quantile, percentile in [("p50", 50), ("p95", 95)]:
            row[f"client_upload_plus_draw_ms_{quantile}"] = float(np.percentile([item["gpu_upload_ms"] + item["gpu_draw_ms"] for item in measured], percentile))
    (args.directory / "summary.json").write_text(json.dumps(summaries, indent=2), encoding="utf-8")
    with (args.directory / "summary.csv").open("w", newline="", encoding="utf-8") as target:
        writer = csv.DictWriter(target, fieldnames=list(summaries[0]))
        writer.writeheader()
        writer.writerows(summaries)


if __name__ == "__main__":
    main()
