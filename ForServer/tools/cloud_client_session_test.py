import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import threading
import urllib.error
import urllib.request


def main():
    parser = argparse.ArgumentParser(description="Loopback-only test proxy for real cloud frames and session fault injection")
    parser.add_argument("--upstream", required=True)
    parser.add_argument("--port", type=int, default=18892)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    mutex = threading.Lock()
    stats = {"heartbeat_calls": 0, "transient_failures": 0, "leases_revoked": 0}

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            self.forward()

        def do_POST(self):
            self.forward()

        def forward(self):
            if self.path not in ("/health", "/models", "/status", "/sessions", "/sessions/heartbeat", "/sessions/close", "/frame"):
                self.send_error(404)
                return
            length = int(self.headers.get("Content-Length", "0"))
            if length < 0 or length > 1024:
                self.send_error(400)
                return
            body = self.rfile.read(length)
            headers = {name: self.headers[name] for name in ("Authorization", "X-GS-Session", "X-GS-Prefetch") if name in self.headers}
            if self.path == "/sessions/heartbeat":
                with mutex:
                    stats["heartbeat_calls"] += 1
                    current = stats["heartbeat_calls"]
                    if current == 1:
                        stats["transient_failures"] += 1
                    elif current == 3:
                        stats["leases_revoked"] += 1
                    Path(args.output).write_text(json.dumps(stats, indent=2))
                if current == 1:
                    self.send_error(503, "Injected transient control failure")
                    return
                if current == 3:
                    request = urllib.request.Request(args.upstream + "/sessions/close", data=body, headers=headers)
                    with urllib.request.urlopen(request, timeout=15) as response:
                        response.read()
            request = urllib.request.Request(args.upstream + self.path, data=body if self.command == "POST" else None, headers=headers)
            try:
                response = urllib.request.urlopen(request, timeout=200)
            except urllib.error.HTTPError as failure:
                response = failure
            with response:
                packet = response.read(64 * 1024 * 1024 + 65)
                self.send_response(response.status)
                for name, value in response.headers.items():
                    if name.lower() in ("content-type", "cache-control") or name.lower().startswith("x-gs-"):
                        self.send_header(name, value)
                self.send_header("Content-Length", str(len(packet)))
                self.end_headers()
                self.wfile.write(packet)

    print("Session test proxy bound to loopback", flush=True)
    ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
