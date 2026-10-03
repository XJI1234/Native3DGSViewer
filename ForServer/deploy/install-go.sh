#!/usr/bin/env bash
set -euo pipefail
destination="${1:-/opt/native3dgs/go-toolchain}"
[[ "$destination" = /* && "$destination" != / ]] || { echo "Dedicated absolute toolchain directory required" >&2; exit 2; }
destination=$(realpath -m "$destination")
[[ "$destination" != / ]] || { echo "Dedicated absolute toolchain directory required" >&2; exit 2; }
archive=go1.27.1.linux-amd64.tar.gz
checksum=63d339f0da5ab53635a56f2490a7984dfe12dfcff22ad749f63edaf590168445
[[ $(uname -m) = x86_64 ]] || { echo "This pinned bootstrap supports x86_64 only" >&2; exit 1; }
mkdir -p "$destination/downloads"
if [[ ! -f "$destination/downloads/$archive" ]]; then
    curl --fail --location --proto '=https' --tlsv1.2 "https://go.dev/dl/$archive" --output "$destination/downloads/$archive.partial"
    mv "$destination/downloads/$archive.partial" "$destination/downloads/$archive"
fi
printf '%s  %s\n' "$checksum" "$destination/downloads/$archive" | sha256sum -c -
if [[ -x "$destination/go/bin/go" ]]; then
    "$destination/go/bin/go" version | grep -q 'go1.27.1 ' || { echo "Existing toolkit differs; choose another destination" >&2; exit 1; }
else
    tar -xzf "$destination/downloads/$archive" -C "$destination"
fi
"$destination/go/bin/go" version
