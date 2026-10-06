"""Fault tests for benchmark evidence validity; no model/GPU measurements."""

import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("native_measure", Path(__file__).with_name("measure-interaction.py"))
MEASURE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MEASURE)
BROWSER_SPEC = importlib.util.spec_from_file_location("browser_measure", Path(__file__).with_name("measure-browser-interaction.py"))
BROWSER = importlib.util.module_from_spec(BROWSER_SPEC)
BROWSER_SPEC.loader.exec_module(BROWSER)


class EvidenceGuards(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.out = self.root / "evidence"
        self.out.mkdir()
        (self.out / "windows-results.json").write_text('{"complete":true}', encoding="utf-8")
        (self.out / "reference-poses.json").write_text('{"stale":true}', encoding="utf-8")
        self.binary = self.root / "missing.exe"

    def report(self):
        return json.loads((self.out / "windows-results.json").read_text(encoding="utf-8"))

    def test_setup_failure_invalidates_previous_complete_evidence(self):
        with patch.object(MEASURE, "OUT", self.out), patch.object(MEASURE, "BINARY", self.binary):
            with self.assertRaises(FileNotFoundError):
                MEASURE.main()
        self.assertFalse(self.report()["complete"])
        self.assertEqual(self.report()["failures"][0]["stage"], "setup")
        self.assertEqual(json.loads((self.out / "reference-poses.json").read_text(encoding="utf-8")), {})

    def test_timeout_retains_partial_output_and_failure(self):
        self.binary.write_bytes(b"fake executable")
        (self.root / "bench").mkdir()
        (self.root / "bench/windows-interaction.cpp").write_text("fake source", encoding="utf-8")
        model_dir = self.root / "models"
        model_dir.mkdir()
        (model_dir / "fake.ply").write_bytes(b"fake model")
        manifest_dir = self.root / "ForWeb/docs/verification/evidence"
        manifest_dir.mkdir(parents=True)
        entry = {"name": "fake.ply", "count": 1, "bytes": 10,
                 "sha256": hashlib.sha256(b"fake model").hexdigest()}
        (manifest_dir / "model-manifest.json").write_text(json.dumps({"models": [entry]}), encoding="utf-8")
        error = subprocess.TimeoutExpired("fake", 660, output=b"partial\r\n", stderr=b"failure\r\n")
        with patch.object(MEASURE, "ROOT", self.root), patch.object(MEASURE, "OUT", self.out), \
             patch.object(MEASURE, "BINARY", self.binary), patch.object(MEASURE, "MODEL_ROOT", model_dir), \
             patch.object(MEASURE, "MODELS", ["fake.ply"]), \
             patch.object(MEASURE.subprocess, "check_output", return_value="test-commit"), \
             patch.object(MEASURE.subprocess, "run", side_effect=error):
            with self.assertRaises(subprocess.TimeoutExpired):
                MEASURE.main()
        self.assertFalse(self.report()["complete"])
        self.assertEqual(self.report()["failures"][0]["model"], "fake.ply")
        self.assertEqual((self.out / "fake.ply-native-0.log").read_text(encoding="utf-8"), "partial\nfailure\n")

    def test_wrong_count_and_short_window_are_rejected(self):
        text = ("loaded count=1 degree=3 decode_ms=1 first_frame_ms=2\n"
                "pose 0 0 1 0 0 0\n"
                "interaction duration_ms=20000 accepted_presents=40 render_calls=40 fps=2\n"
                + "500,500,1,1\n" * 40)
        entry = {"count": 1, "bytes": 1, "sha256": "test"}
        with self.assertRaisesRegex(ValueError, "Count/SH"):
            MEASURE.parse_run(text, {**entry, "count": 2}, "fake", 0, "fake.log")
        with self.assertRaisesRegex(ValueError, "Incomplete sample"):
            MEASURE.parse_run(text.replace("duration_ms=20000", "duration_ms=10"), entry, "fake", 0, "fake.log")

    def test_browser_launch_failure_invalidates_previous_complete_evidence(self):
        (self.out / "windows-results.json").write_text('{"complete":true}', encoding="utf-8")
        directory = self.out / "web-repeat-0"
        directory.mkdir()
        (directory / "results.json").write_text('{"complete":true}', encoding="utf-8")
        with patch.object(BROWSER, "OUT", self.out), \
             patch.object(BROWSER.subprocess, "Popen", side_effect=FileNotFoundError("node unavailable")):
            with self.assertRaises(FileNotFoundError):
                BROWSER.main()
        report = json.loads((directory / "results.json").read_text(encoding="utf-8"))
        self.assertFalse(report["complete"])
        self.assertEqual(report["status"], "launching")


if __name__ == "__main__":
    unittest.main()
