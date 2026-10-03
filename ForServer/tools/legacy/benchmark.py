import argparse
import csv
import hashlib
import json
import shlex
import subprocess
import time
import urllib.request
from pathlib import Path

import numpy as np
from PIL import Image
from skimage.metrics import structural_similarity


def linux_path(path):
    path = Path(path).resolve()
    return f"/mnt/{path.drive[0].lower()}/{path.as_posix()[3:]}"


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(4 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def quality(reference_path, image_path):
    reference = np.asarray(Image.open(reference_path).convert("RGB"))
    image = np.asarray(Image.open(image_path).convert("RGB"))
    error = np.abs(reference.astype(np.float64) - image.astype(np.float64))
    mse = float(np.mean(error ** 2))
    psnr = 10 * np.log10(255 ** 2 / mse) if mse else None
    ssim = float(structural_similarity(reference, image, channel_axis=2, data_range=255))
    Image.fromarray(image).save(image_path.with_suffix(".png"))
    heat = np.minimum(error.max(axis=2) * 8, 255).astype(np.uint8)
    Image.fromarray(heat).save(image_path.with_name(image_path.stem + "-error.png"))
    mask = reference.max(axis=2) > 8
    foreground_fraction = float(mask.mean())
    foreground_mse = float(np.mean(error[mask] ** 2)) if mask.any() else 0
    foreground_psnr = float(10 * np.log10(255 ** 2 / foreground_mse)) if foreground_mse else None
    crop_ssim = ssim
    if mask.any():
        rows, columns = np.nonzero(mask)
        top, bottom = max(int(rows.min()) - 4, 0), min(int(rows.max()) + 5, reference.shape[0])
        left, right = max(int(columns.min()) - 4, 0), min(int(columns.max()) + 5, reference.shape[1])
        if min(bottom - top, right - left) >= 7:
            crop_ssim = float(structural_similarity(reference[top:bottom, left:right], image[top:bottom, left:right], channel_axis=2, data_range=255))
    return {"rgb_psnr_db": psnr, "rgb_ssim": ssim, "max_channel_error": int(error.max()),
            "mean_absolute_error": float(error.mean()), "identical": mse == 0,
            "foreground_fraction": foreground_fraction, "foreground_psnr_db": foreground_psnr, "crop_ssim": crop_ssim}


def launch(args, model, directory, raw):
    stdout_path = directory / f"worker-raw{raw}.jsonl"
    stderr_path = directory / f"worker-raw{raw}.stderr"
    command = [args.server, "serve", "--model", linux_path(model), "--port", str(args.port), "--raw", str(raw)]
    script = f"echo $$; exec {shlex.join(command)} > {shlex.quote(linux_path(stdout_path))} 2> {shlex.quote(linux_path(stderr_path))}"
    worker = subprocess.Popen(["wsl", "-d", args.distro, "--", "bash", "-lc", script],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    pid = int(worker.stdout.readline().strip())
    start = time.perf_counter()
    try:
        while time.perf_counter() - start < 180:
            if worker.poll() is not None:
                raise RuntimeError(stderr_path.read_text(encoding="utf-8", errors="replace"))
            try:
                with urllib.request.urlopen(f"http://127.0.0.1:{args.port}/health", timeout=1) as response:
                    if response.read() == b"ready":
                        if response.headers.get("X-GS-Worker-Pid") != str(pid):
                            raise RuntimeError("Port belongs to another worker")
                        return worker, pid, (time.perf_counter() - start) * 1000
            except OSError:
                time.sleep(0.1)
        raise TimeoutError("Resident model readiness timeout")
    except BaseException:
        stop(args, worker, pid)
        raise


def stop(args, worker, pid):
    subprocess.run(["wsl", "-d", args.distro, "--", "kill", "-TERM", str(pid)], capture_output=True)
    try:
        worker.wait(timeout=20)
    except subprocess.TimeoutExpired:
        subprocess.run(["wsl", "-d", args.distro, "--", "kill", "-KILL", str(pid)], capture_output=True)
        worker.wait(timeout=10)


def capture(args, profile, width, height, view, output, repeats):
    command = [str(args.client), "--url", args.url or f"http://127.0.0.1:{args.port}",
               "--profile", profile, "--width", str(width), "--height", str(height), "--view", str(view),
               "--repeat", str(repeats), "--out", str(output), "--packet", str(output.with_suffix(".ngsf"))]
    result = subprocess.run(command, capture_output=True, text=True, timeout=1800)
    output.with_suffix(".jsonl").write_text(result.stdout, encoding="utf-8")
    output.with_suffix(".stderr").write_text(result.stderr, encoding="utf-8")
    if result.returncode:
        raise RuntimeError(result.stderr.strip())
    return [json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")]


def main():
    parser = argparse.ArgumentParser(description="WSL CUDA -> WinHTTP -> Windows D3D12 fixed-frame benchmark")
    parser.add_argument("--models", nargs="+", type=Path, required=True)
    parser.add_argument("--server", default="/home/qbm/.cache/native3dgs/server/gs-server")
    parser.add_argument("--client", type=Path, default=Path("out/server-windows/Release/gs-client.exe"))
    parser.add_argument("--distro", default="Ubuntu")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--url", help="Optional loopback paced proxy URL; origin still launched on --port")
    parser.add_argument("--resolutions", nargs="+", default=["1280x720", "1920x1080"])
    parser.add_argument("--views", nargs="+", type=int, default=[0])
    parser.add_argument("--profiles", nargs="+", default=["f32", "f16", "q20", "q16", "rgba", "jpeg95", "jpeg85", "jpeg70", "jpeg50"])
    parser.add_argument("--repeat", type=int, default=30)
    parser.add_argument("--warmup", type=int, default=3)
    parser.add_argument("--include-raw", action="store_true")
    parser.add_argument("--out", type=Path, default=Path("out/server-benchmark"))
    args = parser.parse_args()
    if args.repeat < 1 or args.warmup < 0:
        parser.error("Invalid repeat/warmup")
    output_names = [model.stem + "-" + model.suffix[1:] for model in args.models]
    if len(output_names) != len(set(output_names)):
        parser.error("Duplicate model output names; run same-named models in separate output batches")
    args.out = args.out.resolve()
    args.client = args.client.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    metadata = {"created_local": time.strftime("%Y-%m-%dT%H:%M:%S%z"), "arguments": vars(args).copy(),
                "transport": "WinHTTP new session/connection every request; resident warm model; no response cache",
                "timing": "offscreen readiness including GPU screenshot; not physical display latency",
                "same_gpu": "WSL CUDA server and Windows D3D12 client share RTX 3080; not a low-end client"}
    metadata["arguments"] = {key: str(value) if isinstance(value, Path) else [str(item) for item in value] if isinstance(value, list) else value
                             for key, value in metadata["arguments"].items()}
    metadata["gpu"] = subprocess.run(["wsl", "-d", args.distro, "--", "nvidia-smi"], capture_output=True, text=True).stdout
    (args.out / "environment.json").write_text(json.dumps(metadata, indent=2, ensure_ascii=False), encoding="utf-8")
    summaries, failures = [], []
    for model in args.models:
        model = model.resolve()
        directory = args.out / (model.stem + "-" + model.suffix[1:])
        directory.mkdir(exist_ok=True)
        identity = {"model": str(model), "sha256": sha256(model), "model_bytes": model.stat().st_size}
        (directory / "sample.json").write_text(json.dumps(identity, indent=2), encoding="utf-8")
        for raw in ([0, 1] if args.include_raw else [0]):
            worker = None
            try:
                worker, pid, load_ms = launch(args, model, directory, raw)
                for resolution in args.resolutions:
                    width, height = map(int, resolution.split("x"))
                    for view in args.views:
                        reference = directory / f"{resolution}-v{view}-raw{raw}-reference.ppm"
                        capture(args, "rgba", width, height, view, reference, 1)
                        for profile in args.profiles:
                            if raw and profile.startswith("jpeg"):
                                continue
                            output = directory / f"{resolution}-v{view}-raw{raw}-{profile}.ppm"
                            try:
                                rows = capture(args, profile, width, height, view, output, args.repeat + args.warmup)
                                rows = rows[args.warmup:]
                                last = rows[-1]
                                summary = {**identity, "resolution": resolution, "view": view, "profile": profile, "raw": raw,
                                           "measured_repeats": len(rows), "warmup_discarded": args.warmup, "model_ready_ms": load_ms,
                                           "client_init_ms": last["init_ms"],
                                           "packet_bytes": last["packet_bytes"], "visible_splats": last["server"]["visible_count"],
                                           "source_splats": last["server"]["source_count"],
                                           "client_committed_resource_bytes": last["client_committed_resource_bytes"],
                                           "peak_working_set_bytes": max(row["peak_working_set_bytes"] for row in rows),
                                           "server_peak_gpu_bytes": last["server"]["peak_gpu_bytes"], **quality(reference, output)}
                                packet = output.with_suffix(".ngsf").read_bytes()
                                summary["unpacked_bytes"] = int.from_bytes(packet[32:40], "little")
                                summary["compression_flag"] = int.from_bytes(packet[28:32], "little")
                                for name in ["network_ms", "decode_ms", "gpu_upload_ms", "gpu_draw_ms", "capture_ms", "render_wall_ms", "ready_with_capture_ms"]:
                                    for quantile, percentile in [("p50", 50), ("p95", 95)]:
                                        summary[f"{name}_{quantile}"] = float(np.percentile([row[name] for row in rows], percentile))
                                for name in ["project_ms", "sort_ms", "gather_ms", "draw_ms", "readback_ms", "encode_ms", "server_ms"]:
                                    summary[f"server_{name}_p50"] = float(np.median([row["server"][name] for row in rows]))
                                for mbps in [5, 10, 20, 100]:
                                    summary[f"theoretical_{mbps}mbps_ms"] = last["packet_bytes"] * 8 / (mbps * 1000)
                                summaries.append(summary)
                                print(json.dumps(summary), flush=True)
                            except (RuntimeError, subprocess.TimeoutExpired) as error:
                                failure = {**identity, "resolution": resolution, "view": view, "profile": profile, "raw": raw, "error": str(error)}
                                failures.append(failure)
                                print(json.dumps(failure), flush=True)
                            (args.out / "summary.json").write_text(json.dumps(summaries, indent=2), encoding="utf-8")
                            (args.out / "failures.json").write_text(json.dumps(failures, indent=2), encoding="utf-8")
            except (RuntimeError, TimeoutError, subprocess.TimeoutExpired) as error:
                failures.append({**identity, "raw": raw, "error": str(error)})
            finally:
                if worker is not None:
                    stop(args, worker, pid)
        (args.out / "failures.json").write_text(json.dumps(failures, indent=2), encoding="utf-8")
    if summaries:
        with (args.out / "summary.csv").open("w", newline="", encoding="utf-8") as target:
            writer = csv.DictWriter(target, fieldnames=list(summaries[0]))
            writer.writeheader()
            writer.writerows(summaries)
    if failures:
        print(f"Recorded {len(failures)} explicit failures; see failures.json")


if __name__ == "__main__":
    main()
