"""Match Ninja's MSVC dependency prefix to actual compiler output bytes."""

import argparse
from pathlib import Path
import re
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    args = parser.parse_args()
    directory = args.build.resolve() / "msvc-prefix-probe"
    directory.mkdir(exist_ok=True)
    header = directory / "cuda-showincludes-probe.h"
    source = directory / "probe.cpp"
    header.write_text("#pragma once\n", encoding="ascii")
    source.write_text('#include "cuda-showincludes-probe.h"\nint probe() { return 0; }\n', encoding="ascii")
    compiler = shutil.which("cl.exe")
    if not compiler:
        raise RuntimeError("MSVC developer environment required")
    run = subprocess.run([compiler, "/nologo", "/showIncludes", "/c", str(source),
                          "/Fo" + str(directory / "probe.obj")], capture_output=True)
    if run.returncode:
        raise RuntimeError("MSVC prefix probe compilation failed")
    lines = [line for line in run.stdout.splitlines() if header.name.encode("ascii") in line]
    if len(lines) != 1:
        raise RuntimeError("Cannot identify the actual MSVC include prefix")
    path = re.search(rb"[A-Za-z]:[\\/]", lines[0])
    if not path:
        raise RuntimeError("Expected an absolute include path")
    prefix = lines[0][:path.start()].rstrip(b" \t")
    rules = args.build.resolve() / "CMakeFiles" / "rules.ninja"
    original = rules.read_bytes()
    pattern = rb"(?m)^msvc_deps_prefix = [^\r\n]*"
    if len(re.findall(pattern, original)) != 1:
        raise RuntimeError("Expected one MSVC prefix in Ninja rules")
    updated = re.sub(pattern, lambda _: b"msvc_deps_prefix = " + prefix, original)
    marker = directory / "verified-prefix.bin"
    clean = not marker.exists() or marker.read_bytes() != prefix
    if updated != original:
        rules.write_bytes(updated)
        print("Matched Ninja /showIncludes prefix to compiler bytes (no locale installation required)")
    marker.write_bytes(prefix)
    return 2 if clean else 0


if __name__ == "__main__":
    raise SystemExit(main())
