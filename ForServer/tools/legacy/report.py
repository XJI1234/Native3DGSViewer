import argparse
import csv
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont


def write_csv(path, rows):
    if not rows:
        return
    with path.open("w", encoding="utf-8", newline="") as target:
        writer = csv.DictWriter(target, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def contact_sheet(directory, rows):
    selected = [row for row in rows if row["resolution"] == "1920x1080" and row["view"] == 2 and row["raw"] == 0]
    for model in sorted({row["model"] for row in selected}):
        model_path = Path(model)
        model_directory = directory / (model_path.stem + "-" + model_path.suffix[1:])
        reference_path = model_directory / "1920x1080-v2-raw0-reference.ppm"
        reference = Image.open(reference_path).convert("RGB")
        mask = np.asarray(reference).max(axis=2) > 8
        pixel_rows, columns = np.nonzero(mask)
        if not len(columns):
            continue
        box = (max(int(columns.min()) - 8, 0), max(int(pixel_rows.min()) - 8, 0),
               min(int(columns.max()) + 9, reference.width), min(int(pixel_rows.max()) + 9, reference.height))
        profiles = ["reference", "f32", "f16", "q20", "q16", "jpeg95", "jpeg85", "jpeg70", "jpeg50"]
        canvas = Image.new("RGB", (1260, 1080), (22, 22, 22))
        draw = ImageDraw.Draw(canvas)
        try:
            font = ImageFont.truetype("C:/Windows/Fonts/arial.ttf", 17)
        except OSError:
            font = ImageFont.load_default()
        for index, profile in enumerate(profiles):
            x_position, y_position = (index % 3) * 420, (index // 3) * 360
            image_path = reference_path if profile == "reference" else model_directory / f"1920x1080-v2-raw0-{profile}.ppm"
            if not image_path.is_file():
                continue
            image = Image.open(image_path).convert("RGB").crop(box)
            image.thumbnail((408, 280), Image.Resampling.LANCZOS)
            canvas.paste(image, (x_position + (420 - image.width) // 2, y_position + 60 + (280 - image.height) // 2))
            draw.text((x_position + 8, y_position + 8), profile.upper(), fill="white", font=font)
            if profile != "reference":
                row = next(row for row in selected if row["model"] == model and row["profile"] == profile)
                psnr = row["foreground_psnr_db"]
                quality_label = "lossless" if psnr is None else f"{psnr:.2f} dB"
                label = f"{row['packet_bytes'] / 1048576:.3f} MiB / FG {quality_label} / crop {row['crop_ssim']:.4f}"
                draw.text((x_position + 8, y_position + 32), label, fill=(190, 220, 210), font=font)
        canvas.save(model_directory / "quality-comparison.png")


def main():
    parser = argparse.ArgumentParser(description="Archive small numeric evidence; keep model images/raw logs under ignored out")
    parser.add_argument("--directories", nargs="+", type=Path, required=True)
    parser.add_argument("--out", type=Path, default=Path("ForServer/docs/evidence"))
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    batches = {}
    batch_directories = {}
    for directory in args.directories:
        rows = json.loads((directory / "summary.json").read_text(encoding="utf-8"))
        write_csv(args.out / f"{directory.name}.csv", rows)
        (args.out / f"{directory.name}-environment.json").write_text((directory / "environment.json").read_text(encoding="utf-8"), encoding="utf-8")
        batches[directory.name] = rows
        batch_directories[directory.name] = directory
        contact_sheet(directory, rows)
    if "server-benchmark-small" in batches and "server-benchmark-optimized" in batches:
        comparisons = []
        for row in batches["server-benchmark-optimized"]:
            before = next((item for item in batches["server-benchmark-small"] if all(item[key] == row[key] for key in ["model", "resolution", "view", "raw", "profile"])), None)
            if before is None:
                continue
            filename = f"{row['resolution']}-v{row['view']}-raw{row['raw']}-{row['profile']}.ngsf"
            model = Path(row["model"])
            subdirectory = model.stem + "-" + model.suffix[1:]
            baseline_path = batch_directories["server-benchmark-small"] / subdirectory / filename
            optimized_path = batch_directories["server-benchmark-optimized"] / subdirectory / filename
            identical = baseline_path.read_bytes() == optimized_path.read_bytes()
            comparisons.append({"profile": row["profile"], "raw": row["raw"], "resolution": row["resolution"], "view": row["view"],
                                "packet_identical": identical, "before_encode_p50_ms": before["server_encode_ms_p50"],
                                "after_encode_p50_ms": row["server_encode_ms_p50"],
                                "encode_reduction_fraction": 1 - row["server_encode_ms_p50"] / before["server_encode_ms_p50"],
                                "before_ready_p50_ms": before["ready_with_capture_ms_p50"], "after_ready_p50_ms": row["ready_with_capture_ms_p50"]})
        write_csv(args.out / "optimization.csv", comparisons)
    source_paths = ["ForServer/CMakeLists.txt", "ForServer/src/cuda_renderer.cu", "ForServer/src/windows_renderer.cpp", "ForServer/src/frame.cpp"]
    manifest = {path: hashlib.sha256(Path(path).read_bytes()).hexdigest() for path in source_paths}
    shader = Path("ForServer/src/windows_renderer.cpp").read_text(encoding="utf-8").split('R"(', 1)[1].split(')";', 1)[0]
    manifest["embedded_thin_hlsl"] = hashlib.sha256(shader.encode("utf-8")).hexdigest()
    (args.out / "source-sha256.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
