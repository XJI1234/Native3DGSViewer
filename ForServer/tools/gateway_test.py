import asyncio
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import AsyncMock, patch

import gateway


class Process:
    pid = 123
    dead = False

    def poll(self):
        return 1 if self.dead else None


class Contracts(unittest.TestCase):
    def test_catalog_stable_and_escape_rejected(self):
        with tempfile.TemporaryDirectory() as folder, tempfile.TemporaryDirectory() as outside:
            root = Path(folder)
            (root / "模型.ply").write_bytes(b"ply")
            first = gateway.catalog_models(root)
            self.assertEqual(first, gateway.catalog_models(root))
            if os.name == "posix":
                other = Path(outside) / "outside.ply"
                other.write_bytes(b"ply")
                (root / "escape.ply").symlink_to(other)
                with self.assertRaises(ValueError):
                    gateway.catalog_models(root)

    def test_request_validation(self):
        models = {"m-1": {}}
        valid = b"NGSREQ2 1 m-1 640 360 -12 25 0.5 rgba"
        self.assertEqual(gateway.parse_frame(valid, models), ("1", "m-1"))
        self.assertEqual(gateway.parse_frame(valid.replace(b"1 m-1", b"0001 m-1"), models), ("1", "m-1"))
        for body in (valid + b" extra", valid.replace(b"rgba", b"f32"), valid.replace(b"-12", b"nan"), valid.replace(b"25", b"90"), valid.replace(b"640", b"4097"), valid.replace(b"m-1", b"../x"), valid.replace(b"0.5", b"0"), valid.replace(b"1 m-1", b"-1 m-1")):
            with self.assertRaises(ValueError):
                gateway.parse_frame(body, models)

    def test_empty_catalog_and_token(self):
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaises(ValueError):
                gateway.catalog_models(folder)
            path = Path(folder) / "token"
            token = gateway.read_token(path)
            self.assertEqual(token, gateway.read_token(path))
            self.assertEqual(len(token), 64)
            if os.name == "posix":
                path.chmod(0o644)
                with self.assertRaises(ValueError):
                    gateway.read_token(path)


class AsyncContracts(unittest.IsolatedAsyncioTestCase):
    async def test_affinity_concurrency_and_failed_worker(self):
        pool = gateway.Pool(max_queue=2, queue_timeout=0.05)
        pool.workers = [gateway.Worker(0, 1, Process(), model="m-0"), gateway.Worker(1, 2, Process(), model="m-1")]
        first = await pool.acquire("m-1")
        self.assertEqual(first.device, 1)
        second = await pool.acquire("m-1")
        self.assertEqual(second.device, 0)
        with self.assertRaises(asyncio.TimeoutError):
            await pool.acquire("m-0")
        self.assertEqual(pool.waiting, 0)
        await pool.release(first)
        self.assertIs(await pool.acquire("m-1"), first)
        await pool.release(first)
        await pool.release(second)
        for worker in pool.workers:
            worker.failed = True
        with self.assertRaises(gateway.BusyError):
            await pool.acquire("m-0")

    async def test_queue_limit_and_cancel_cleanup(self):
        pool = gateway.Pool(max_queue=1)
        pool.workers = [gateway.Worker(0, 1, Process(), busy=True)]
        pending = asyncio.create_task(pool.acquire("m-0"))
        await asyncio.sleep(0.01)
        with self.assertRaises(gateway.BusyError):
            await pool.acquire("m-0")
        pending.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await pending
        self.assertEqual(pool.waiting, 0)

    async def test_wire_rejects_duplicate_and_oversized_body(self):
        for header in (b"POST /frame HTTP/1.1\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\n", b"POST /frame HTTP/1.1\r\nContent-Length: 1025\r\n\r\n", b"POST /frame HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n"):
            reader = asyncio.StreamReader()
            reader.feed_data(header)
            with self.assertRaises(ValueError):
                await gateway.read_http(reader)

    async def test_real_socket_health_auth_catalog_frame_and_identity(self):
        pool = gateway.Pool()
        pool.workers = [gateway.Worker(0, 1, Process())]
        models = {"m-1": {"id": "m-1", "name": "模型.ply", "bytes": 3, "path": "/secret/model.ply"}}
        service = gateway.Gateway(pool, models, "a" * 64)
        server = await asyncio.start_server(service.handle, "127.0.0.1", 0, limit=8193)
        port = server.sockets[0].getsockname()[1]

        async def request(path, body=None, auth=False):
            reader, writer = await asyncio.open_connection("127.0.0.1", port)
            header = ("POST" if body else "GET") + " " + path + " HTTP/1.1\r\nHost: localhost\r\n"
            if auth:
                header += "Authorization: Bearer " + "a" * 64 + "\r\n"
            if body:
                header += "Content-Length: " + str(len(body)) + "\r\n"
            writer.write(header.encode() + b"\r\n" + (body or b""))
            await writer.drain()
            fields, headers, size = await gateway.read_http(reader, True)
            result = await reader.readexactly(size)
            writer.close()
            await writer.wait_closed()
            return int(fields[1]), headers, result

        try:
            self.assertEqual((await request("/health"))[0], 200)
            self.assertEqual((await request("/models"))[0], 401)
            catalog = await request("/models", auth=True)
            self.assertNotIn(b"secret", catalog[2])
            self.assertEqual(json.loads(catalog[2])[0]["name"], "模型.ply")
            body = b"NGSREQ2 42 m-1 640 360 0 0 1 rgba"
            response = ({"x-gs-request-id": "42", "x-gs-model-id": "m-1", "x-gs-stats": "{}"}, b"NGSFRM02test")
            with patch.object(gateway, "worker_http", AsyncMock(return_value=response)):
                result = await request("/frame", body, True)
                self.assertEqual(result[0], 200)
                self.assertEqual(result[1]["x-gs-request-id"], "42")
            response[0]["x-gs-request-id"] = "41"
            with patch.object(gateway, "worker_http", AsyncMock(return_value=response)):
                self.assertEqual((await request("/frame", body, True))[0], 502)
            self.assertFalse(pool.workers[0].busy)
            pool.workers[0].process.dead = True
            self.assertEqual((await request("/health"))[0], 503)
        finally:
            server.close()
            await server.wait_closed()


if __name__ == "__main__":
    unittest.main()
