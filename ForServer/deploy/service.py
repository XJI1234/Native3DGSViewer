import argparse
import fcntl
import json
import os
from pathlib import Path
import signal
import ssl
import subprocess
import sys
import threading
import time
import urllib.request


def owned(pid, script, prefix):
    try:
        arguments = Path("/proc/" + str(pid) + "/cmdline").read_bytes().split(b"\0")
        return str(script).encode() in arguments and str(prefix).encode() in arguments
    except (OSError, ValueError):
        return False


def pid_for(prefix):
    try:
        pid = int((prefix / "state/supervisor.pid").read_text())
    except (OSError, ValueError):
        return None
    script = prefix / "service.py"
    if not script.exists():
        script = Path(__file__).resolve()
    return pid if owned(pid, script, prefix) else None


def health(prefix):
    config = json.loads((prefix / "config.json").read_text())
    scheme = "https" if config.get("cert") else "http"
    context = ssl.create_default_context(cafile=config["cert"]) if config.get("cert") else None
    host = config.get("bind", "127.0.0.1")
    if host in ("0.0.0.0", ""):
        host = "127.0.0.1"
    elif host == "::":
        host = "::1"
    if ":" in host and not host.startswith("["):
        host = "[" + host + "]"
    with urllib.request.urlopen(scheme + "://" + host + ":" + str(config["port"]) + "/health", timeout=2, context=context) as response:
        return json.load(response)


def run(prefix):
    state = prefix / "state"
    state.mkdir(parents=True, exist_ok=True)
    lock = open(state / "supervisor.lock", "a")
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    (state / "supervisor.pid").write_text(str(os.getpid()))
    config = json.loads((prefix / "config.json").read_text())
    stop = threading.Event()
    for event in (signal.SIGTERM, signal.SIGINT):
        signal.signal(event, lambda *_: stop.set())
    gateway = prefix / "current/bin/gs-gateway"
    command = ([str(gateway)] if gateway.exists() else [sys.executable, str(prefix / "current/tools/gateway.py")]) + ["--server", str(prefix / "current/bin/gs-server"), "--models", config["models"], "--state", str(state), "--token-file", str(prefix / "token"), "--devices", config["devices"], "--bind", config["bind"], "--port", str(config["port"]), "--worker-port", str(config["worker_port"])]
    if gateway.exists():
        command += ["--instance-idle", str(config.get("instance_idle", 300)) + "s", "--cache-ttl", str(config.get("cache_ttl", 86400)) + "s", "--cache-bytes", str(config.get("cache_bytes", 10 << 30)), "--max-instances", str(config.get("max_instances", 4))]
        if config.get("enable_multi_gpu", False):
            command.append("--enable-multi-gpu")
    if config.get("private_http"):
        command.append("--private-http")
    if config.get("cert"):
        command += ["--cert", config["cert"], "--key", config["key"]]
    failures = 0
    with open(prefix / "logs/gateway.log", "ab", buffering=0) as log:
        while not stop.is_set():
            child = subprocess.Popen(command, stdout=log, stderr=log, start_new_session=True)
            while child.poll() is None and not stop.wait(0.2):
                pass
            try:
                os.killpg(child.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                child.wait(timeout=15)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
                child.wait()
            try:
                os.killpg(child.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            if stop.is_set():
                break
            failures += 1
            print(json.dumps({"event": "gateway_restart", "attempt": failures, "exit": child.returncode}), flush=True)
            if failures >= 5:
                raise RuntimeError("Gateway restart budget exhausted; inspect logs")
            stop.wait(min(30, 2 ** failures))
    lock.close()


def stop_service(prefix):
    pid = pid_for(prefix)
    if pid is None:
        print("Already stopped")
        return
    os.kill(pid, signal.SIGTERM)
    for _ in range(150):
        if pid_for(prefix) is None:
            print("Stopped")
            return
        time.sleep(0.2)
    raise RuntimeError("Owned supervisor did not stop; inspect logs before forcing termination")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("configure", "start", "stop", "status", "run", "logs", "running"))
    parser.add_argument("--prefix", required=True)
    parser.add_argument("--models", default="")
    parser.add_argument("--devices", default="0")
    parser.add_argument("--port", type=int, default=8888)
    parser.add_argument("--worker-port", type=int, default=19000)
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--private-http", action="store_true")
    parser.add_argument("--cert", default="")
    parser.add_argument("--key", default="")
    parser.add_argument("--reconfigure", action="store_true")
    parser.add_argument("--enable-multi-gpu", action="store_true")
    parser.add_argument("--instance-idle", type=int, default=300)
    parser.add_argument("--cache-ttl", type=int, default=86400)
    parser.add_argument("--cache-bytes", type=int, default=10 << 30)
    parser.add_argument("--max-instances", type=int, default=4)
    args = parser.parse_args()
    prefix = Path(args.prefix).resolve()
    if prefix == Path("/"):
        parser.error("Dedicated prefix required")
    if args.command == "running":
        sys.exit(0 if pid_for(prefix) is not None else 1)
    (prefix / "state").mkdir(parents=True, exist_ok=True)
    (prefix / "logs").mkdir(parents=True, exist_ok=True)
    if args.command == "run":
        run(prefix)
        return
    lock = open(prefix / "state/control.lock", "a")
    fcntl.flock(lock, fcntl.LOCK_EX)
    if args.command == "configure":
        path = prefix / "config.json"
        if path.exists() and not args.reconfigure:
            print("Existing configuration preserved; use --reconfigure to change it")
            return
        if pid_for(prefix):
            raise RuntimeError("Stop before reconfiguring")
        devices = args.devices.split(",")
        if not devices or any(not value.isdecimal() or int(value) > 255 for value in devices) or len(devices) != len(set(devices)):
            raise ValueError("Invalid devices")
        if len(devices) > 1 and not args.enable_multi_gpu:
            raise ValueError("Multi GPU is retained but disabled; explicitly --enable-multi-gpu to enable")
        if args.instance_idle <= 0 or args.cache_ttl <= 0 or args.cache_bytes < 1024 or not 1 <= args.max_instances <= 16:
            raise ValueError("Invalid instance/cache limits")
        models = Path(args.models).resolve(strict=True)
        if not models.is_dir() or not 1 <= args.port <= 65535 or not 1024 <= args.worker_port or args.worker_port + args.max_instances > 65535:
            raise ValueError("Invalid model directory/ports")
        if not args.private_http and not args.cert and args.bind not in {"127.0.0.1", "::1", "localhost"}:
            raise ValueError("TLS or explicit private HTTP is required")
        config = {name: getattr(args, name) for name in ("devices", "port", "worker_port", "bind", "private_http", "cert", "key", "enable_multi_gpu", "instance_idle", "cache_ttl", "cache_bytes", "max_instances")}
        config["models"] = str(models)
        temporary = prefix / "config.json.new"
        temporary.write_text(json.dumps(config, indent=2) + "\n")
        temporary.replace(path)
        print("Configuration saved; access token will be generated at first startup")
    elif args.command == "start":
        if pid_for(prefix):
            print(json.dumps(health(prefix)))
            return
        with open(prefix / "logs/supervisor.log", "ab", buffering=0) as log:
            subprocess.Popen([sys.executable, str(Path(__file__).resolve()), "run", "--prefix", str(prefix)], stdout=log, stderr=log, start_new_session=True)
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            try:
                status = health(prefix)
                if status["status"] == "ready":
                    print(json.dumps(status))
                    return
            except (OSError, ValueError):
                pass
            time.sleep(0.2)
        stop_service(prefix)
        raise RuntimeError("Startup health deadline; inspect logs")
    elif args.command == "stop":
        stop_service(prefix)
    elif args.command == "status":
        if pid_for(prefix) is None:
            print("Stopped")
            sys.exit(1)
        print(json.dumps(health(prefix)))
    elif args.command == "logs":
        for path in (prefix / "logs").glob("*.log"):
            print("=== " + path.name + " ===")
            print(path.read_text(errors="replace")[-12000:])


if __name__ == "__main__":
    main()
