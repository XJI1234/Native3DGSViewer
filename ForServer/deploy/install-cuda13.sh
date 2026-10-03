#!/usr/bin/env bash
set -euo pipefail
umask 022
prefix="${1:-/opt/native3dgs/toolchains}"
[[ "$prefix" = /* && "$prefix" != / ]] || { echo "Use an absolute, dedicated prefix" >&2; exit 2; }
prefix=$(realpath -m "$prefix")
[[ "$prefix" != / ]] || { echo "Use an absolute, dedicated prefix" >&2; exit 2; }
command -v apt-get >/dev/null
command -v dpkg-deb >/dev/null
packages=(cuda-nvcc-13-0 cuda-crt-13-0 libnvvm-13-0 libnvptxcompiler-13-0 cuda-cudart-dev-13-0 cuda-cudart-13-0 cuda-cccl-13-0 cuda-culibos-dev-13-0 cuda-driver-dev-13-0)
mkdir -p "$prefix/downloads"
cd "$prefix/downloads"
verified=()
for package in "${packages[@]}"; do
    version=$(apt-cache policy "$package" | awk '/Candidate:/ {print $2}')
    [[ -n "$version" && "$version" != '(none)' ]] || { echo "Missing NVIDIA Ubuntu repository package: $package. Configure the official CUDA repository first." >&2; exit 1; }
    filename=$(apt-cache show "$package=$version" | awk '/^Filename:/ {print $2; exit}')
    checksum=$(apt-cache show "$package=$version" | awk '/^SHA256:/ {print $2; exit}')
    filename="${filename##*/}"
    if [[ ! -f "$filename" ]]; then
        apt-get download "$package=$version"
    fi
    printf '%s  %s\n' "$checksum" "$filename" | sha256sum --check --status
    verified+=("$filename")
    printf '%s=%s\n' "$package" "$version" >> "$prefix/packages.lock"
done
for package in "${verified[@]}"; do
    dpkg-deb --extract "$package" "$prefix"
done
sha256sum "${verified[@]}" > "$prefix/downloads.sha256"
"$prefix/usr/local/cuda-13.0/bin/nvcc" --version
printf 'Isolated CUDA path: %s\nNo driver or alternatives were installed/changed.\n' "$prefix/usr/local/cuda-13.0"
