"""Join additional whole paired rounds only when inputs and frozen runtimes match."""

import argparse
import copy
import hashlib
import json
from pathlib import Path
import shutil


def run_stem(run):
    model = run["model"].replace("/", "_").replace("\\", "_")
    return f'{model}-{run["backend"]}-{run["repetition"]}'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directories", type=Path, nargs="+")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if len(args.directories) < 2:
        parser.error("At least two completed collections required")
    sources = []
    seen_directories, seen_commands = set(), set()
    for directory in args.directories:
        if directory.resolve() in seen_directories:
            raise RuntimeError("Duplicate collection directory")
        seen_directories.add(directory.resolve())
        raw = (directory / "results.json").read_bytes()
        data = json.loads(raw)
        if not data.get("complete") or not data.get("runtime_unchanged"):
            raise RuntimeError(f"Incomplete or mutable runtime: {directory}")
        sources.append((directory.resolve(), data, hashlib.sha256(raw).hexdigest()))
    reference = sources[0][1]
    same = ["protocol", "formal_duration", "cuda_event_diagnostics", "warmup_seconds", "sample_seconds",
            "frames", "warmup_frames", "camera", "pan_x", "yaw_degrees", "pitch_degrees", "dolly_steps",
            "image_reference", "frame_slots", "tile_capacity", "models", "binaries", "shaders",
            "runtime_sha256", "collector_sha256"]
    expected = {(model["name"], backend) for model in reference["models"] for backend in reference["frame_slots"]}
    for directory, data, _ in sources:
        for key in same:
            if data.get(key) != reference.get(key):
                raise RuntimeError(f"Collection differs in {key}: {directory}")
        for repetition in set(run["repetition"] for run in data["runs"]):
            runs = [run for run in data["runs"] if run["repetition"] == repetition]
            if len(runs) != len(expected) or {(run["model"], run["backend"]) for run in runs} != expected:
                raise RuntimeError(f"Unpaired or duplicate round: {directory}, {repetition}")
            if any(run["exit_code"] or not run.get("sample_count") for run in runs):
                raise RuntimeError(f"Failed or empty round: {directory}, {repetition}")
            for run in runs:
                command = tuple(run["command"])
                if command in seen_commands:
                    raise RuntimeError("Repeated original run cannot count as an independent round")
                seen_commands.add(command)
    args.output.mkdir(parents=True, exist_ok=False)
    result = copy.deepcopy(reference)
    result["runs"] = []
    result["source_collections"] = []
    offset = 0
    for directory, data, digest in sources:
        result["source_collections"].append({"directory": str(directory), "results_sha256": digest,
            "head": data["head"], "diff_sha256": data["diff_sha256"], "collector_sha256": data["collector_sha256"]})
        repetitions = sorted(set(run["repetition"] for run in data["runs"]))
        mapped = {repetition: offset + index + 1 for index, repetition in enumerate(repetitions)}
        for original in data["runs"]:
            run = copy.deepcopy(original)
            run["source_collection"] = str(directory)
            run["source_repetition"] = original["repetition"]
            run["repetition"] = mapped[original["repetition"]]
            before, after = run_stem(original), run_stem(run)
            for suffix in [".csv", ".png", ".log", ".csv.memory.csv"]:
                source = directory / (before + suffix)
                if source.exists():
                    shutil.copyfile(source, args.output / (after + suffix))
                elif suffix in [".csv", ".png", ".log"]:
                    raise RuntimeError(f"Missing evidence: {source}")
            result["runs"].append(run)
        offset += len(repetitions)
    result["paired_rounds"] = offset
    (args.output / "results.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Merged {offset} complete paired rounds; all input/runtime fingerprints match")


if __name__ == "__main__":
    main()
