import argparse
import json
from pathlib import Path
import statistics
import numpy as np
from PIL import Image
from skimage.metrics import structural_similarity


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    folder = Path(args.input)
    reference = np.asarray(Image.open(folder / "rgba.ppm"), dtype=np.float64)
    mask = np.max(reference, axis=2) > 8
    locations = np.argwhere(mask)
    if not len(locations):
        raise ValueError("Reference is black; invalid quality experiment")
    lower = np.maximum(locations.min(axis=0) - 4, 0)
    upper = np.minimum(locations.max(axis=0) + 5, reference.shape[:2])
    crop = (slice(lower[0], upper[0]), slice(lower[1], upper[1]))
    results = []
    for profile in ("rgba", "jpeg95", "jpeg90", "jpeg85"):
        pixels = np.asarray(Image.open(folder / (profile + ".ppm")), dtype=np.float64)
        records = [json.loads(line) for line in (folder / (profile + ".jsonl")).read_text(encoding="utf-8-sig").splitlines()]
        if len(records) < 2:
            raise ValueError("Quality timing requires --repeat >=2 so one warm-up record can be discarded")
        samples = records[1:]
        error = np.mean((pixels[mask] - reference[mask]) ** 2)
        result = {"profile": profile, "samples": len(samples), "packet_bytes": samples[0]["packet_bytes"], "foreground_fraction": float(mask.mean()), "foreground_psnr_db": None if error == 0 else float(10 * np.log10(255 ** 2 / error)), "crop_ssim": float(structural_similarity(reference[crop], pixels[crop], channel_axis=2, data_range=255)), "exact_rgba": bool(np.array_equal(pixels, reference))}
        for key in ("decode_ms", "network_ms", "gpu_upload_ms", "gpu_draw_ms", "ready_with_capture_ms", "client_committed_resource_bytes", "peak_working_set_bytes"):
            result[key + "_p50"] = statistics.median(entry[key] for entry in samples)
        result["transfer_only_5mbps_ms"] = result["packet_bytes"] * 8 / 5000
        results.append(result)
    Path(args.output).write_text(json.dumps(results, indent=2))
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
