"""Install the hash-pinned NVIDIA CUDA 13.3 development components per user."""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import urllib.request
import zipfile


def install_component(name, manifest, cache, destination):
    component = manifest[name]
    package = component["windows-x86_64"]
    archive = cache / Path(package["relative_path"]).name
    url = "https://developer.download.nvidia.com/compute/cuda/redist/" + package["relative_path"]
    if not archive.exists() or hashlib.sha256(archive.read_bytes()).hexdigest() != package["sha256"]:
        partial = archive.with_suffix(".partial")
        with urllib.request.urlopen(url, timeout=60) as source, partial.open("wb") as target:
            shutil.copyfileobj(source, target)
        if hashlib.sha256(partial.read_bytes()).hexdigest() != package["sha256"]:
            raise RuntimeError(f"SHA-256 mismatch: {name}")
        partial.replace(archive)
    # Official redists contain a single package directory. Merge its contents;
    # preserve package-specific licenses instead of overwriting LICENSE.txt.
    installed = []
    with zipfile.ZipFile(archive) as source:
        for member in source.infolist():
            parts = PurePosixPath(member.filename).parts
            if member.is_dir():
                continue
            if len(parts) < 2 or ".." in parts or PurePosixPath(member.filename).is_absolute():
                raise RuntimeError(f"Invalid archive path: {member.filename}")
            relative = Path(*parts[1:])
            if relative.name.upper().startswith(("LICENSE", "EULA")):
                relative = Path("licenses") / name / relative
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            content = source.read(member)
            if target.exists() and target.read_bytes() != content:
                raise RuntimeError(f"Conflicting installed file: {target}")
            if not target.exists():
                target.write_bytes(content)
            installed.append(str(relative))
    print(f"Installed {name} {component['version']}: {len(installed)} files", flush=True)
    return {"component": name, "version": component["version"], "url": url,
            "sha256": package["sha256"], "files": installed}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(os.environ["USERPROFILE"]) /
                        "AppData/Local/Native3DGSViewer/toolchains/cuda/13.3.0")
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[2]
    evidence = project / "CudaOperators/evidence/2026-10-17-toolchain"
    manifest = json.loads((evidence / "redistrib_13.3.0.json").read_text(encoding="utf-8"))
    if manifest["release_label"] != "13.3.0":
        raise RuntimeError("Unexpected release manifest")
    destination = args.root.resolve()
    destination.mkdir(parents=True, exist_ok=True)
    cache = project / "out/cuda-downloads"
    cache.mkdir(parents=True, exist_ok=True)
    names = ["cuda_nvcc", "cuda_crt", "cuda_cudart", "cccl", "libnvvm", "cuda_nvdisasm",
             "cuda_cuobjdump", "cuda_sanitizer_api"]
    # Downloads are independent. Installation is sequential to detect overlaps.
    installed = [install_component(name, manifest, cache, destination) for name in names]
    receipt = {"release": "13.3.0", "root": str(destination), "components": installed,
               "driver_changed": False, "global_path_changed": False}
    (destination / "install-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    (evidence / "install-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(f"Toolkit root: {destination}", flush=True)


if __name__ == "__main__":
    main()
