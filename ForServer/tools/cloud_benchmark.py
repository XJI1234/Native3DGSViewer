import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import statistics
import time
import urllib.request


def percentile(values, ratio):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int((len(ordered) - 1) * ratio))]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="http://127.0.0.1:8888")
    parser.add_argument("--token-file", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--model", default="")
    parser.add_argument("--frames", type=int, default=120)
    parser.add_argument("--concurrency", type=int, default=4)
    parser.add_argument("--profile", default="rgba")
    parser.add_argument("--width", type=int, default=1920)
    parser.add_argument("--height", type=int, default=1080)
    args = parser.parse_args()
    if args.frames < 1 or not 1 <= args.concurrency <= 64:
        parser.error("frames must be positive and concurrency must be 1..64")
    token = Path(args.token_file).read_text().strip()
    headers = {"Authorization": "Bearer " + token}
    request = urllib.request.Request(args.url + "/models", headers=headers)
    with urllib.request.urlopen(request, timeout=10) as response:
        models = json.load(response)
    model = next((entry for entry in models if entry["id"] == args.model or entry["name"] == args.model), models[-1] if not args.model else None)
    if model is None:
        raise ValueError("Unknown benchmark model")

    def render(identity):
        body = f"NGSREQ2 {identity} {model['id']} {args.width} {args.height} 0 14.0362434679 1 {args.profile}".encode()
        start = time.perf_counter()
        with urllib.request.urlopen(urllib.request.Request(args.url + "/frame", data=body, headers=headers), timeout=200) as response:
            payload = response.read()
            if response.headers["X-GS-Request-Id"] != str(identity) or response.headers["X-GS-Model-Id"] != model["id"] or payload[:8] != b"NGSFRM02":
                raise ValueError("Frame identity mismatch")
            stats = json.loads(response.headers["X-GS-Stats"])
            stats["cache_state"] = response.headers.get("X-GS-Cache", "uncached-v2")
        stats.update(request_id=identity, wall_ms=(time.perf_counter() - start) * 1000, packet_bytes=len(payload), packet_sha256=hashlib.sha256(payload).hexdigest())
        return stats, payload if identity == 1001 else None

    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(max_workers=args.concurrency) as executor:
        warm = [entry[0] for entry in executor.map(render, range(1, args.concurrency * 4 + 1))]
        start = time.perf_counter()
        samples = []
        captured = None
        for stats, payload in executor.map(render, range(1001, 1001 + args.frames)):
            samples.append(stats)
            if payload is not None:
                captured = payload
        elapsed = time.perf_counter() - start
    (output / "frame.ngsf").write_bytes(captured)
    (output / "samples.json").write_text(json.dumps(samples, indent=2))
    summary = {"model": {key: value for key, value in model.items() if key != "path"}, "profile": args.profile, "width": args.width, "height": args.height, "frames": args.frames, "concurrency": args.concurrency, "elapsed_seconds": elapsed, "frames_per_second": args.frames / elapsed,
               "latency_p50_ms": statistics.median(entry["wall_ms"] for entry in samples), "latency_p95_ms": percentile([entry["wall_ms"] for entry in samples], 0.95), "packet_bytes": samples[0]["packet_bytes"],
               "devices": sorted({entry["worker_device"] for entry in samples if "worker_device" in entry}), "identical_fixed_camera_packets": len({entry["packet_sha256"] for entry in samples}) == 1,
               "cache_states": {state: sum(entry["cache_state"] == state for entry in samples) for state in sorted({entry["cache_state"] for entry in samples})},
               "measurement": "fixed-view HTTP throughput including cache; not native render FPS"}
    for key in ("server_ms", "queue_ms", "resident_bytes", "peak_gpu_bytes"):
        values = [entry[key] for entry in samples if key in entry]
        summary[key + "_samples"] = len(values)
        summary[key + "_median"] = statistics.median(values) if values else None
    summary["warm_load_ms"] = [entry["load_ms"] for entry in warm if "load_ms" in entry]
    (output / "summary.json").write_text(json.dumps(summary, indent=2))
    print(json.dumps(summary), flush=True)


if __name__ == "__main__":
    main()
