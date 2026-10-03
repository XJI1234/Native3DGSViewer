import argparse
import json
import socket
import subprocess
import time
import urllib.error
import urllib.request
from pathlib import Path
from benchmark import launch, stop, linux_path


def main():
    parser = argparse.ArgumentParser(description="Real resident CUDA HTTP and Windows client failure/recovery checks")
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--server", default="/home/qbm/.cache/native3dgs/server/gs-server")
    parser.add_argument("--client", type=Path, default=Path("out/server-windows/Release/gs-client.exe"))
    parser.add_argument("--distro", default="Ubuntu")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--out", type=Path, default=Path("out/server-integration"))
    args = parser.parse_args()
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    worker, pid, load_ms = launch(args, args.model, args.out, 0)
    checks = []

    def record(name, passed, detail):
        checks.append({"name": name, "passed": passed, "detail": detail})
        print(json.dumps(checks[-1]), flush=True)
        if not passed:
            raise AssertionError(name)

    def health():
        with urllib.request.urlopen(f"http://127.0.0.1:{args.port}/health", timeout=15) as response:
            return response.read() == b"ready"

    try:
        for body in [b"bad", b"NGSREQ1 0 100 0 rgba", b"NGSREQ1 640 360 6 rgba",
                     b"NGSREQ1 640 360 0 invalid", b"x" * 1025]:
            try:
                request = urllib.request.Request(f"http://127.0.0.1:{args.port}/frame", data=body)
                urllib.request.urlopen(request, timeout=15)
                rejected = False
            except urllib.error.HTTPError as error:
                rejected = error.code == 400
            record("malformed_request_and_health_recovery", rejected and health(), body[:80].decode())
        with socket.create_connection(("127.0.0.1", args.port), timeout=15) as connection:
            connection.settimeout(15)
            start = time.perf_counter()
            connection.sendall(b"POST /frame HTTP/1.1\r\nHost: localhost\r\n")
            try:
                connection.recv(512)
                expired = True
            except socket.timeout:
                expired = False
            elapsed = time.perf_counter() - start
            record("partial_header_deadline", expired and elapsed < 13, elapsed)
        record("health_after_timeout", health(), "resident model survives read timeout")
        for profile in ["f32", "f16", "q20", "q16", "rgba", "jpeg95", "jpeg85", "jpeg70", "jpeg50"]:
            result = subprocess.run([str(args.client.resolve()), "--url", f"http://127.0.0.1:{args.port}",
                                     "--profile", profile, "--repeat", "2"], capture_output=True, text=True, timeout=120)
            rows = [json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")]
            record("windows_http_profile", result.returncode == 0 and len(rows) == 2 and all(row["server"]["source_count"] > 0 for row in rows), profile)
        for arguments in [["--repeat", "0"], ["--unknown", "1"], ["--url", "http://example.com"]]:
            result = subprocess.run([str(args.client.resolve()), *arguments], capture_output=True, text=True, timeout=15)
            record("client_rejects_invalid_arguments_or_cleartext_remote", result.returncode != 0, arguments)
        for arguments in [["--width", "0"], ["--profile", "invalid"], ["--repeat", "0"], ["--port", "1234"]]:
            start = time.perf_counter()
            result = subprocess.run(["wsl", "-d", args.distro, "--", args.server, "render", "--model", linux_path(args.model), *arguments],
                                    capture_output=True, text=True, timeout=10)
            record("server_validates_cli_before_model_upload", result.returncode != 0 and "Loaded " not in result.stderr,
                   {"arguments": arguments, "elapsed_ms": (time.perf_counter() - start) * 1000})
    finally:
        stop(args, worker, pid)
        (args.out / "checks.json").write_text(json.dumps({"load_ms": load_ms, "checks": checks}, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
