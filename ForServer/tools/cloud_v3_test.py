import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import time
import urllib.error
import urllib.request


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="http://127.0.0.1:8891")
    parser.add_argument("--gateway")
    parser.add_argument("--server")
    parser.add_argument("--models")
    parser.add_argument("--token-file", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    token = Path(args.token_file).read_text().strip()
    checks = []
    with tempfile.TemporaryDirectory(prefix="gs-v3-acceptance-") as state:
        process = None
        log = open(Path(state) / "gateway.log", "wb")
        try:
            if args.gateway:
                port = args.url.rsplit(":", 1)[1]
                process = subprocess.Popen([args.gateway, "--server", args.server, "--models", args.models, "--state", state, "--token-file", args.token_file, "--bind", "127.0.0.1", "--port", port, "--worker-port", "21000", "--devices", "0", "--instance-idle", "1s", "--cache-ttl", "10s", "--lease-ttl", "2s", "--sweep-interval", "100ms"], stdout=log, stderr=log)

            def call(route, body=None, session=None, prefetch=False, identity=None):
                headers = {"Authorization": "Bearer " + token}
                if session:
                    headers["X-GS-Session"] = session
                if prefetch:
                    headers["X-GS-Prefetch"] = "1"
                request = urllib.request.Request(args.url + route, data=body.encode() if body is not None else None, headers=headers)
                with urllib.request.urlopen(request, timeout=180) as response:
                    packet = response.read()
                    if identity is not None:
                        assert response.headers["X-GS-Request-Id"] == str(identity)
                        assert packet[:8] == b"NGSFRM02"
                    return packet, dict(response.headers)

            deadline = time.monotonic() + 25
            while True:
                try:
                    health = json.loads(call("/health")[0])
                    assert health["api_version"] == 3 and health["available"] == 1
                    break
                except (OSError, ValueError):
                    if time.monotonic() > deadline:
                        raise
                    time.sleep(0.05)
            models = json.loads(call("/models")[0])
            model = min(models, key=lambda entry: entry["bytes"])["id"]
            sessions = [json.loads(call("/sessions", model)[0])["session_id"] for unused in range(8)]
            def frame(identity, yaw=22, flip=0, session=None, prefetch=False, profile="rgba"):
                body = f"NGSREQ3 {identity} {model} 640 360 {yaw} 14 0.4 {profile} {flip}"
                return call("/frame", body, session, prefetch, identity)

            before = json.loads(call("/status")[0])
            start = time.perf_counter()
            with ThreadPoolExecutor(max_workers=8) as executor:
                frames = list(executor.map(lambda index: frame(100 + index, session=sessions[index]), range(8)))
            elapsed = (time.perf_counter() - start) * 1000
            after = json.loads(call("/status")[0])
            assert after["loads"] - before["loads"] == 1
            assert after["renders"] - before["renders"] == 1
            assert len(after["instances"]) == 1 and after["instances"][0]["gpu"] == 0
            assert len({hashlib.sha256(packet).hexdigest() for packet, headers in frames}) == 1
            checks.append({"case": "8-users-one-instance-one-render", "passed": True, "elapsed_ms": elapsed, "cache_states": [headers["X-Gs-Cache"] for packet, headers in frames], "pid": after["instances"][0]["pid"]})
            session = sessions[0]
            for stale in sessions[1:]:
                call("/sessions/close", stale)
            for unused in range(6):
                call("/sessions/heartbeat", session)
                time.sleep(0.3)
            pinned = json.loads(call("/status")[0])
            assert pinned["instances"][0]["pid"] == after["instances"][0]["pid"]
            packet, headers = frame(200, session=session)
            assert headers["X-Gs-Cache"] == "hit"
            flipped, flip_headers = frame(201, flip=1, session=session)
            assert flip_headers["X-Gs-View-Code"] == headers["X-Gs-View-Code"]
            assert hashlib.sha256(flipped).digest() != hashlib.sha256(packet).digest()
            checks.append({"case": "lease-pins-and-y-flip-keeps-world-code", "passed": True, "view_code": headers["X-Gs-View-Code"]})
            for direction in (-1, 1):
                for step in range(1, 5):
                    call("/sessions/heartbeat", session)
                    frame(300 + step + (10 if direction > 0 else 0), yaw=22 + direction * 2 * step, session=session, prefetch=True)
            predicted, predicted_headers = frame(400, yaw=24, session=session)
            assert predicted_headers["X-Gs-Cache"] == "hit"
            checks.append({"case": "predicted-packet-transferred-and-next-move-hit", "passed": True, "packet_bytes": len(predicted)})
            for profile in ("jpeg95", "jpeg90", "jpeg85"):
                call("/sessions/heartbeat", session)
                frame(500, session=session, profile=profile)
            call("/sessions/close", session)
            if args.gateway:
                time.sleep(1.5)
                unloaded = json.loads(call("/status")[0])
                assert unloaded["instances"] == []
                cached, cached_headers = frame(600)
                assert cached_headers["X-Gs-Cache"] == "hit"
                assert json.loads(call("/status")[0])["instances"] == []
                checks.append({"case": "idle-unloads-and-disk-hit-needs-no-GPU-instance", "passed": True})
                resumed = json.loads(call("/sessions", model)[0])["session_id"]
                frame(650, yaw=90, session=resumed, prefetch=True)
                assert json.loads(call("/status")[0])["loads"] == unloaded["loads"] + 1
                call("/sessions/close", resumed)
                checks.append({"case": "live-cached-view-starts-missing-neighbor-instance", "passed": True})
                time.sleep(10.5)
                empty = json.loads(call("/status")[0])
                assert int(empty["cache_entries"]) == 0
                frame(700)
                reloaded = json.loads(call("/status")[0])
                assert reloaded["loads"] == unloaded["loads"] + 2
                checks.append({"case": "cache-no-hit-TTL-deletion-and-instance-reload", "passed": True})
            result = {"checks": checks, "status": json.loads(call("/status")[0])}
            Path(args.output).write_text(json.dumps(result, indent=2))
            print(json.dumps(result))
        finally:
            if process:
                process.terminate()
                try:
                    process.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            log.close()


if __name__ == "__main__":
    main()
