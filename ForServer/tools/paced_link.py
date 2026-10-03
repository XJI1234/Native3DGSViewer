import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import signal
import threading
import time
import urllib.request


def main():
    parser = argparse.ArgumentParser(description="Loopback application response pacing; not real WAN/netem")
    parser.add_argument("--origin", default="http://127.0.0.1:8888")
    parser.add_argument("--port", type=int, default=8899)
    parser.add_argument("--mbps", type=float, default=5)
    parser.add_argument("--delay-ms", type=float, default=40)
    args = parser.parse_args()
    if args.mbps <= 0 or args.delay_ms < 0:
        parser.error("Invalid pacing parameters")

    class Handler(BaseHTTPRequestHandler):
        def do_POST(self):
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if self.path != "/frame" or not 0 < length <= 1024:
                    raise ValueError("Invalid request")
                body = self.rfile.read(length)
                time.sleep(args.delay_ms / 1000)
                request = urllib.request.Request(args.origin.rstrip("/") + "/frame", data=body, headers={"Authorization": self.headers.get("Authorization", "")})
                with urllib.request.urlopen(request, timeout=200) as response:
                    packet = response.read((64 << 20) + 65)
                    headers = {name: response.headers[name] for name in ("X-GS-Stats", "X-GS-Request-Id", "X-GS-Model-Id", "X-GS-View-Code", "X-GS-Cache") if name in response.headers}
                if len(packet) > (64 << 20) + 64 or packet[:8] != b"NGSFRM02":
                    raise ValueError("Invalid frame budget/version")
                self.send_response(200)
                self.send_header("Content-Length", str(len(packet)))
                for name, value in headers.items():
                    self.send_header(name, value)
                self.send_header("Connection", "close")
                self.end_headers()
                start = time.perf_counter()
                for offset in range(0, len(packet), 4096):
                    end = min(offset + 4096, len(packet))
                    remaining = start + end * 8 / (args.mbps * 1e6) - time.perf_counter()
                    if remaining > 0:
                        time.sleep(remaining)
                    self.wfile.write(packet[offset:end])
                    self.wfile.flush()
            except (OSError, ValueError):
                self.close_connection = True

        def log_message(self, *arguments):
            pass

    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    server.daemon_threads = True
    def stop(*arguments):
        threading.Thread(target=server.shutdown, daemon=True).start()
    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)
    print("Paced loopback port=" + str(args.port), flush=True)
    try:
        server.serve_forever(poll_interval=0.1)
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
