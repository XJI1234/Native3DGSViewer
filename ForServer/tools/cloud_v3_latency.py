import argparse
import json
from pathlib import Path
import statistics
import sqlite3
import time
import urllib.request


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", required=True)
    parser.add_argument("--token-file", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--yaw-start", type=int, default=180)
    parser.add_argument("--expire-database", help="Admin test only: expire this model's 1080p RGBA cached samples")
    args = parser.parse_args()
    headers = {"Authorization": "Bearer " + Path(args.token_file).read_text().strip()}
    with urllib.request.urlopen(urllib.request.Request(args.url + "/models", headers=headers)) as response:
        model = max(json.load(response), key=lambda entry: entry["bytes"])["id"]
    if args.expire_database:
        with sqlite3.connect(args.expire_database) as database:
            database.execute("UPDATE cache SET last_hit=0 WHERE filename LIKE ? AND filename LIKE ?", (model + "/%", "%-1920x1080-rgba.ngsf"))
    samples = {"miss": [], "hit": []}
    def render(identity, yaw):
        body = f"NGSREQ3 {identity} {model} 1920 1080 {yaw} 14 1 rgba 0".encode()
        start = time.perf_counter()
        with urllib.request.urlopen(urllib.request.Request(args.url + "/frame", data=body, headers=headers), timeout=180) as response:
            packet = response.read()
            native = json.loads(response.headers["X-GS-Stats"])
            return {"wall_ms": (time.perf_counter() - start) * 1000, "cache": response.headers["X-GS-Cache"], "native_ms": native.get("server_ms"), "packet_bytes": len(packet)}
    render(1, args.yaw_start)
    for index in range(1, 21):
        yaw = args.yaw_start + index * 2
        result = render(index + 1, yaw)
        if result["cache"] != "miss":
            raise ValueError("Cold sample was not miss; use fresh views/state or explicitly expire only benchmark cache entries")
        samples["miss"].append(result)
        cached = render(index + 101, yaw)
        if cached["cache"] != "hit":
            raise ValueError("Warm sample was not hit; cache changed during the experiment")
        samples["hit"].append(cached)
    summary = {kind: {"samples": len(values), "wall_p50_ms": statistics.median(entry["wall_ms"] for entry in values), "wall_p95_ms": sorted(entry["wall_ms"] for entry in values)[18], "states": sorted({entry["cache"] for entry in values})} for kind, values in samples.items()}
    result = {"model": model, "summary": summary, "samples": samples}
    Path(args.output).write_text(json.dumps(result, indent=2))
    print(json.dumps(summary))


if __name__ == "__main__":
    main()
