import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / "deploy/service.py"
SPEC = importlib.util.spec_from_file_location("cloud_supervisor", SCRIPT)
service = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(service)


class DeploymentContracts(unittest.TestCase):
    def test_failed_upgrade_stops_new_supervisor_and_restores_transaction(self):
        deploy = (SCRIPT.parent / "deploy.sh").read_text()
        rollback = deploy.split("restore_upgrade() {", 1)[1].split("trap restore_upgrade EXIT", 1)[0]
        with tempfile.TemporaryDirectory() as folder:
            prefix = Path(folder)
            backup = prefix / ".upgrade.test"
            backup.mkdir()
            for name in ("config.json", "service.py"):
                (backup / name).write_text("old " + name)
                (prefix / name).write_text("new " + name)
            (prefix / "current").symlink_to("new-release")
            (prefix / "previous").symlink_to("old-release")
            fake_python = prefix / "python"
            fake_python.write_text('#!/bin/bash\n[[ "$2" = stop ]] || exit 1\nprintf "%s" "$(cat "$4/config.json")" > "$4/stopped"\n')
            fake_python.chmod(0o755)
            environment = dict(os.environ, prefix=str(prefix), backup=str(backup), python=str(fake_python), old_current="old-release", old_previous="older-release", startup_attempted="1")
            result = subprocess.run(["bash", "-c", "restore_upgrade() {" + rollback + "\ntrap restore_upgrade EXIT\nexit 7\n"], env=environment, capture_output=True)
            self.assertEqual(result.returncode, 7, result.stderr.decode())
            self.assertEqual((prefix / "stopped").read_text(), "new config.json")
            self.assertEqual((prefix / "config.json").read_text(), "old config.json")
            self.assertEqual((prefix / "service.py").read_text(), "old service.py")
            self.assertEqual(os.readlink(prefix / "current"), "old-release")
            self.assertEqual(os.readlink(prefix / "previous"), "older-release")
            self.assertFalse(backup.exists())

    def test_health_uses_configured_bind_and_ipv6(self):
        with tempfile.TemporaryDirectory() as folder:
            prefix = Path(folder)
            for host, expected in (("0.0.0.0", "127.0.0.1"), ("192.168.0.2", "192.168.0.2"), ("::", "[::1]"), ("::1", "[::1]")):
                (prefix / "config.json").write_text(json.dumps({"bind": host, "port": 8888}))
                with patch.object(service.urllib.request, "urlopen") as request:
                    request.return_value.__enter__.return_value.read.return_value = b'{"status":"ready"}'
                    self.assertEqual(service.health(prefix), {"status": "ready"})
                    self.assertEqual(request.call_args.args[0], "http://" + expected + ":8888/health")

    def test_running_uses_ownership_even_without_health(self):
        with tempfile.TemporaryDirectory() as folder:
            prefix = Path(folder)
            (prefix / "state").mkdir()
            (prefix / "service.py").write_text("")
            (prefix / "state/supervisor.pid").write_text("123")
            with patch.object(service, "owned", return_value=True) as owned:
                self.assertEqual(service.pid_for(prefix), 123)
                owned.assert_called_once_with(123, prefix / "service.py", prefix)
            with patch.object(service, "owned", return_value=False):
                self.assertIsNone(service.pid_for(prefix))

    def test_configure_rejects_worker_port_range_and_root(self):
        with tempfile.TemporaryDirectory() as folder:
            for port in (1, 65534):
                result = subprocess.run([sys.executable, str(SCRIPT), "configure", "--prefix", folder + "/install", "--models", folder, "--worker-port", str(port), "--private-http"], capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((Path(folder) / "install/config.json").exists())
        result = subprocess.run([sys.executable, str(SCRIPT), "running", "--prefix", "/opt/.."], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"Dedicated prefix", result.stderr)


if __name__ == "__main__":
    unittest.main()
