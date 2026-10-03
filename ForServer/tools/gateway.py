import argparse
import asyncio
import dataclasses
import hashlib
import hmac
import json
import math
import os
from pathlib import Path
import re
import secrets
import signal
import ssl
import subprocess
import time

MAX_PACKET = (64 << 20) + 64


def catalog_models(root):
    root = Path(root).resolve(strict=True)
    models = {}
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.suffix.lower() not in {".ply", ".spz"}:
            continue
        resolved = path.resolve(strict=True)
        if root not in resolved.parents:
            raise ValueError("Model symlink escapes configured root")
        name = path.relative_to(root).as_posix()
        if any(character in str(resolved) for character in "\t\r\n"):
            raise ValueError("Model path contains unsupported control characters")
        model_id = "m-" + hashlib.sha256(name.encode()).hexdigest()[:16]
        if model_id in models or len(models) >= 10000:
            raise ValueError("Catalog duplicate or resource limit")
        models[model_id] = {"id": model_id, "name": name, "bytes": resolved.stat().st_size, "path": str(resolved)}
    if not models:
        raise ValueError("No PLY/SPZ models in configured root")
    return models


def parse_frame(body, models):
    fields = body.decode("ascii").split()
    if len(body) > 1024 or len(fields) != 9 or fields[0] != "NGSREQ2":
        raise ValueError("Expected v2 image request")
    _, identity, model, width, height, yaw, pitch, zoom, profile = fields
    if not all(re.fullmatch(r"[0-9]+", value) for value in (identity, width, height)):
        raise ValueError("Invalid integer")
    if not 0 < int(identity) <= (1 << 64) - 1 or model not in models:
        raise ValueError("Invalid request/model id")
    if not all(1 <= int(value) <= 4096 for value in (width, height)):
        raise ValueError("Invalid dimensions")
    values = tuple(float(value) for value in (yaw, pitch, zoom))
    if not all(math.isfinite(value) for value in values) or abs(values[0]) > 36000 or abs(values[1]) > 89 or not 0.1 <= values[2] <= 10:
        raise ValueError("Invalid centered camera")
    if profile not in {"rgba", *("jpeg" + str(value) for value in range(85, 96))}:
        raise ValueError("Unsupported image quality")
    return str(int(identity)), model


def read_token(path):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    except FileExistsError:
        pass
    else:
        with os.fdopen(descriptor, "w") as output:
            output.write(secrets.token_hex(32) + "\n")
    if os.name == "posix" and path.stat().st_mode & 0o077:
        raise ValueError("Token file must have mode 0600")
    token = path.read_text().strip()
    if not re.fullmatch(r"[A-Za-z0-9_-]{32,256}", token):
        raise ValueError("Invalid token file")
    return token


async def read_http(reader, response=False, timeout=10):
    header = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), timeout)
    if len(header) > 8192:
        raise ValueError("Header budget")
    lines = header.decode("ascii").split("\r\n")
    fields = lines[0].split(" ")
    headers = {}
    for line in lines[1:-2]:
        name, separator, value = line.partition(":")
        name = name.lower()
        if not separator or not re.fullmatch(r"[a-z0-9-]+", name) or name in headers:
            raise ValueError("Invalid/duplicate header")
        headers[name] = value.strip()
    if "transfer-encoding" in headers:
        raise ValueError("Chunked requests/responses are not supported")
    length = headers.get("content-length", "0")
    if not re.fullmatch(r"[0-9]+", length) or int(length) > (MAX_PACKET if response else 1024):
        raise ValueError("Body budget")
    if not response and (len(fields) != 3 or fields[2] not in {"HTTP/1.0", "HTTP/1.1"}):
        raise ValueError("Invalid request line")
    return fields, headers, int(length)


async def worker_http(port, body=None):
    reader, writer = await asyncio.wait_for(asyncio.open_connection("127.0.0.1", port, limit=8193), 5)
    try:
        request = b"GET /health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n" if body is None else b"POST /frame HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\nContent-Length: " + str(len(body)).encode() + b"\r\n\r\n" + body
        writer.write(request)
        await asyncio.wait_for(writer.drain(), 10)
        fields, headers, length = await read_http(reader, response=True, timeout=180 if body else 5)
        if len(fields) < 2 or fields[1] != "200":
            raise RuntimeError("Native worker rejected request")
        payload = await asyncio.wait_for(reader.readexactly(length), 180)
        return headers, payload
    finally:
        writer.close()
        await writer.wait_closed()


@dataclasses.dataclass
class Worker:
    device: int
    port: int
    process: object
    log: object = None
    model: str = ""
    busy: bool = False
    failed: bool = False

    def alive(self):
        return not self.failed and self.process.poll() is None


class BusyError(Exception):
    pass


class Pool:
    def __init__(self, max_queue=16, queue_timeout=30):
        self.workers = []
        self.condition = asyncio.Condition()
        self.waiting = 0
        self.max_queue = max_queue
        self.queue_timeout = queue_timeout

    async def start(self, executable, catalog, state, devices, first_port):
        try:
            for index, device in enumerate(devices):
                port = first_port + index
                log = open(Path(state) / ("worker-" + str(device) + ".log"), "ab", buffering=0)
                process = subprocess.Popen([str(executable), "serve", "--catalog", str(catalog), "--device", str(device), "--port", str(port)], stdout=log, stderr=log)
                worker = Worker(device, port, process, log)
                self.workers.append(worker)
                deadline = time.monotonic() + 30
                while time.monotonic() < deadline:
                    if process.poll() is not None:
                        raise RuntimeError("Worker exited during startup; see worker log")
                    try:
                        headers, body = await worker_http(port)
                        if headers.get("x-gs-worker-pid") != str(process.pid):
                            raise RuntimeError("Worker port is owned by another process")
                        if body != b"ready":
                            raise RuntimeError("Invalid worker health")
                        break
                    except (ConnectionError, OSError, asyncio.TimeoutError):
                        await asyncio.sleep(0.1)
                else:
                    raise RuntimeError("Worker startup health deadline")
        except BaseException:
            await self.close()
            raise

    async def acquire(self, model):
        async with self.condition:
            if self.waiting >= self.max_queue:
                raise BusyError("Queue full")
            self.waiting += 1
            deadline = time.monotonic() + self.queue_timeout
            try:
                while True:
                    healthy = [worker for worker in self.workers if worker.alive()]
                    if not healthy:
                        raise BusyError("No healthy GPU workers")
                    available = [worker for worker in healthy if not worker.busy]
                    if available:
                        worker = min(available, key=lambda item: item.model != model)
                        worker.busy = True
                        return worker
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        raise asyncio.TimeoutError("Queue deadline")
                    await asyncio.wait_for(self.condition.wait(), remaining)
            finally:
                self.waiting -= 1

    async def release(self, worker):
        async with self.condition:
            worker.busy = False
            self.condition.notify_all()

    def status(self):
        return [{"device": worker.device, "pid": worker.process.pid, "model": worker.model,
                 "state": "failed" if not worker.alive() else "busy" if worker.busy else "ready"}
                for worker in self.workers]

    async def close(self):
        for worker in self.workers:
            if worker.process.poll() is None:
                worker.process.terminate()
        for worker in self.workers:
            try:
                await asyncio.to_thread(worker.process.wait, timeout=10)
            except subprocess.TimeoutExpired:
                worker.process.kill()
                await asyncio.to_thread(worker.process.wait)
            if worker.log:
                worker.log.close()
        self.workers.clear()


class Gateway:
    def __init__(self, pool, models, token, max_connections=64):
        self.pool, self.models, self.token = pool, models, token
        self.active = 0
        self.max_connections = max_connections

    async def send(self, writer, status, payload, headers=None):
        reasons = {200: "OK", 400: "Bad Request", 401: "Unauthorized", 404: "Not Found", 502: "Bad Gateway", 503: "Service Unavailable", 504: "Gateway Timeout"}
        values = {"Content-Length": str(len(payload)), "Connection": "close", "Cache-Control": "no-store", **(headers or {})}
        head = "HTTP/1.1 " + str(status) + " " + reasons[status] + "\r\n" + "".join(name + ": " + value + "\r\n" for name, value in values.items()) + "\r\n"
        writer.response_started = True
        writer.write(head.encode("ascii"))
        for offset in range(0, len(payload), 65536):
            writer.write(payload[offset:offset + 65536])
            await writer.drain()

    async def fail(self, writer, status, payload):
        if not getattr(writer, "response_started", False) and not writer.is_closing():
            await asyncio.wait_for(self.send(writer, status, payload), 5)

    async def handle(self, reader, writer):
        if self.active >= self.max_connections:
            try:
                await asyncio.wait_for(self.send(writer, 503, b"Connection budget"), 5)
            finally:
                writer.close()
            return
        self.active += 1
        worker = None
        start = time.perf_counter()
        read_deadline = time.monotonic() + 10
        try:
            fields, headers, length = await read_http(reader)
            method, path, _ = fields
            if method == "GET" and path == "/health":
                alive = sum(worker.alive() for worker in self.pool.workers)
                status = "ready" if alive == len(self.pool.workers) and alive else "degraded" if alive else "unavailable"
                payload = json.dumps({"protocol": 2, "status": status, "workers": len(self.pool.workers), "available": alive}).encode()
                await asyncio.wait_for(self.send(writer, 200 if alive else 503, payload, {"Content-Type": "application/json"}), 10)
                return
            if not hmac.compare_digest(headers.get("authorization", ""), "Bearer " + self.token):
                await asyncio.wait_for(self.send(writer, 401, b"Authentication required"), 10)
                return
            if method == "GET" and path in {"/models", "/status"}:
                data = [{key: value for key, value in model.items() if key != "path"} for model in self.models.values()] if path == "/models" else {"workers": self.pool.status(), "waiting": self.pool.waiting, "max_queue": self.pool.max_queue}
                await asyncio.wait_for(self.send(writer, 200, json.dumps(data, ensure_ascii=False).encode(), {"Content-Type": "application/json; charset=utf-8"}), 10)
                return
            if method != "POST" or path != "/frame":
                await asyncio.wait_for(self.send(writer, 404, b"Unknown route"), 10)
                return
            body = await asyncio.wait_for(reader.readexactly(length), max(0.001, read_deadline - time.monotonic()))
            identity, model = parse_frame(body, self.models)
            queue_start = time.perf_counter()
            worker = await self.pool.acquire(model)
            queue_ms = (time.perf_counter() - queue_start) * 1000
            try:
                response, payload = await worker_http(worker.port, body)
                if response.get("x-gs-request-id") != identity or response.get("x-gs-model-id") != model or payload[:8] != b"NGSFRM02":
                    raise RuntimeError("Worker response identity mismatch")
                worker.model = model
                stats = json.loads(response["x-gs-stats"])
                stats.update(queue_ms=queue_ms, worker_device=worker.device, gateway_ms=(time.perf_counter() - start) * 1000)
            except (ConnectionError, OSError, asyncio.TimeoutError):
                worker.failed = True
                raise
            finally:
                await self.pool.release(worker)
                worker = None
            await asyncio.wait_for(self.send(writer, 200, payload, {"Content-Type": "application/x-native3dgs-frame", "X-GS-Request-Id": identity, "X-GS-Model-Id": model, "X-GS-Stats": json.dumps(stats, separators=(",", ":"))}), 30)
            print(json.dumps({"request_id": identity, "model_id": model, **stats}), flush=True)
        except BusyError:
            await self.fail(writer, 503, b"GPU capacity or queue unavailable")
        except asyncio.TimeoutError:
            await self.fail(writer, 504, b"Request deadline")
        except (ValueError, UnicodeError, asyncio.IncompleteReadError, asyncio.LimitOverrunError):
            await self.fail(writer, 400, b"Invalid request")
        except (RuntimeError, ConnectionError, OSError, KeyError):
            await self.fail(writer, 502, b"Worker failure; inspect server logs")
        finally:
            if worker:
                await self.pool.release(worker)
            self.active -= 1
            writer.close()
            try:
                await asyncio.wait_for(writer.wait_closed(), 5)
            except (OSError, asyncio.TimeoutError):
                pass


async def run(args):
    state = Path(args.state).resolve()
    state.mkdir(parents=True, exist_ok=True)
    import fcntl
    lock = open(state / "gateway.lock", "a")
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    token = read_token(args.token_file)
    models = catalog_models(args.models)
    manifest = state / "catalog.tsv"
    manifest.write_text("".join(model["id"] + "\t" + model["path"] + "\n" for model in models.values()))
    context = None
    if args.cert or args.key:
        if not args.cert or not args.key:
            raise ValueError("Both TLS cert and key are required")
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(args.cert, args.key)
    elif args.bind not in {"127.0.0.1", "::1", "localhost"} and not args.private_http:
        raise ValueError("Non-loopback requires TLS or explicit --private-http")
    if args.private_http and context is None:
        print("WARNING: private HTTP exposes images and bearer token to network observers", flush=True)
    devices = [int(value) for value in args.devices.split(",")]
    if not devices or len(set(devices)) != len(devices) or any(value < 0 for value in devices) or args.worker_port + len(devices) > 65536:
        raise ValueError("Invalid devices/worker ports")
    pool = Pool(args.max_queue)
    stop = asyncio.Event()
    loop = asyncio.get_running_loop()
    for event in (signal.SIGTERM, signal.SIGINT):
        loop.add_signal_handler(event, stop.set)
    try:
        await pool.start(args.server, manifest, state, devices, args.worker_port)
        gateway = Gateway(pool, models, token)
        server = await asyncio.start_server(gateway.handle, args.bind, args.port, ssl=context, limit=8193)
        (state / "gateway.pid").write_text(str(os.getpid()))
        print(json.dumps({"event": "ready", "port": args.port, "devices": devices, "models": len(models), "protocol": 2}), flush=True)
        async with server:
            await stop.wait()
        for _ in range(100):
            if not gateway.active:
                break
            await asyncio.sleep(0.1)
    finally:
        await pool.close()
        lock.close()


def main():
    parser = argparse.ArgumentParser(description="Single-port bounded ImageFrame GPU gateway")
    parser.add_argument("--server", required=True)
    parser.add_argument("--models", required=True)
    parser.add_argument("--state", required=True)
    parser.add_argument("--token-file", required=True)
    parser.add_argument("--devices", default="0")
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8888)
    parser.add_argument("--worker-port", type=int, default=19000)
    parser.add_argument("--max-queue", type=int, default=16)
    parser.add_argument("--private-http", action="store_true")
    parser.add_argument("--cert")
    parser.add_argument("--key")
    args = parser.parse_args()
    if not 1 <= args.port <= 65535 or not 1 <= args.worker_port <= 65535 or not 1 <= args.max_queue <= 64:
        parser.error("Invalid port or queue limit")
    asyncio.run(run(args))


if __name__ == "__main__":
    main()
