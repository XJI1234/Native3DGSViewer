import argparse
import asyncio
import json
from pathlib import Path
import tempfile

from gateway import Pool


class RecordingPool(Pool):
    async def close(self):
        self.children = [worker.process for worker in self.workers]
        await super().close()


async def run(args):
    checks = []
    with tempfile.TemporaryDirectory(prefix="gs-lifecycle-") as folder:
        state = Path(folder)
        catalog = state / "catalog.tsv"
        catalog.write_text("m-test\t" + str(Path(args.model).resolve()) + "\n")
        pool = RecordingPool()
        try:
            await pool.start(args.server, catalog, state, [0, 99], args.worker_port)
            raise AssertionError("Invalid GPU unexpectedly accepted")
        except RuntimeError:
            assert not pool.workers and all(child.poll() is not None for child in pool.children)
            checks.append({"case": "invalid-device-startup-cleans-earlier-worker", "passed": True})

        async def conflicting(reader, writer):
            await reader.readuntil(b"\r\n\r\n")
            writer.write(b"HTTP/1.1 200 OK\r\nX-GS-Worker-Pid: 999999\r\nContent-Length: 5\r\n\r\nready")
            await writer.drain()
            writer.close()

        server = await asyncio.start_server(conflicting, "127.0.0.1", args.worker_port)
        try:
            pool = RecordingPool()
            try:
                await pool.start(args.server, catalog, state, [0], args.worker_port)
                raise AssertionError("Conflicting service incorrectly accepted")
            except RuntimeError:
                assert not pool.workers and all(child.poll() is not None for child in pool.children)
                assert server.is_serving()
                checks.append({"case": "port-owner-mismatch-cleanup-without-stopping-owner", "passed": True})
        finally:
            server.close()
            await server.wait_closed()
    Path(args.output).write_text(json.dumps(checks, indent=2))
    print(json.dumps(checks))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", required=True)
    parser.add_argument("--model", required=True)
    parser.add_argument("--worker-port", type=int, default=19100)
    parser.add_argument("--output", required=True)
    asyncio.run(run(parser.parse_args()))


if __name__ == "__main__":
    main()
