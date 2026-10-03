#!/usr/bin/env bash
set -euo pipefail
umask 022
command="${1:-doctor}"
[[ $# -gt 0 ]] && shift
source_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
prefix=/opt/native3dgs
models=""
devices=0
port=8888
worker_port=19000
bind=0.0.0.0
cuda=/usr/local/cuda-13.0
go_tool=""
enable_multi_gpu=0
instance_idle=300
cache_ttl=86400
cache_bytes=10737418240
max_instances=4
install_deps=0
private_http=0
reconfigure=0
cert=""
key=""
no_start=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefix|--models|--devices|--port|--worker-port|--bind|--cuda|--cert|--key|--go|--instance-idle|--cache-ttl|--cache-bytes|--max-instances)
            [[ $# -ge 2 ]] || { echo "Missing option value: $1" >&2; exit 2; }
            case "$1" in
                --prefix) prefix="$2" ;; --models) models="$2" ;; --devices) devices="$2" ;;
                --port) port="$2" ;; --worker-port) worker_port="$2" ;; --bind) bind="$2" ;;
                --cuda) cuda="$2" ;; --cert) cert="$2" ;; --key) key="$2" ;;
                --go) go_tool="$2" ;; --instance-idle) instance_idle="$2" ;; --cache-ttl) cache_ttl="$2" ;;
                --cache-bytes) cache_bytes="$2" ;; --max-instances) max_instances="$2" ;;
            esac
            shift 2 ;;
        --install-deps) install_deps=1; shift ;;
        --private-http) private_http=1; shift ;;
        --reconfigure) reconfigure=1; shift ;;
        --no-start) no_start=1; shift ;;
        --enable-multi-gpu) enable_multi_gpu=1; shift ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done
[[ "$prefix" = /* && "$prefix" != / ]] || { echo "Dedicated absolute prefix required" >&2; exit 2; }
prefix=$(realpath -m "$prefix")
[[ "$prefix" != / ]] || { echo "Dedicated absolute prefix required" >&2; exit 2; }
python=/usr/bin/python3
[[ -x "$python" ]] || python=$(command -v python3)
control=("$python" "$source_root/ForServer/deploy/service.py")
if [[ -f "$prefix/service.py" ]]; then
    control=("$python" "$prefix/service.py")
fi
if [[ "$command" = start || "$command" = stop || "$command" = status || "$command" = logs || "$command" = run ]]; then
    exec "${control[@]}" "$command" --prefix "$prefix"
fi
[[ "$command" = install || "$command" = doctor ]] || { echo "Commands: doctor install start stop status logs run" >&2; exit 2; }
if [[ "$install_deps" = 1 ]]; then
    [[ $(id -u) = 0 ]] || { echo "--install-deps needs root" >&2; exit 1; }
    missing=()
    for package in build-essential g++-12 cmake ninja-build libboost-system-dev libjpeg-dev zlib1g-dev libsqlite3-dev python3 curl; do
        dpkg-query -W -f='${Status}' "$package" 2>/dev/null | grep -q 'install ok installed' || missing+=("$package")
    done
    if [[ ${#missing[@]} -gt 0 ]]; then
        apt-get update
        apt-get install -y --no-install-recommends "${missing[@]}"
    fi
fi
if [[ -z "$go_tool" ]]; then
    if command -v go >/dev/null 2>&1; then go_tool=$(command -v go)
    elif [[ -x "$prefix/go-toolchain/go/bin/go" ]]; then go_tool="$prefix/go-toolchain/go/bin/go"
    elif [[ "$install_deps" = 1 ]]; then
        bash "$source_root/ForServer/deploy/install-go.sh" "$prefix/go-toolchain"
        go_tool="$prefix/go-toolchain/go/bin/go"
    fi
fi
[[ -x "$go_tool" ]] || { echo "Go >=1.23 missing: supply --go or run deploy/install-go.sh" >&2; exit 1; }
"$go_tool" version
go_version=$(GOTOOLCHAIN=local "$go_tool" version | awk '{print $3}' | sed 's/^go//')
[[ $(printf '%s\n' 1.23 "$go_version" | sort -V | head -1) = 1.23 ]] || { echo "Go >=1.23 required" >&2; exit 1; }
dpkg-query -W -f='${Status}' libsqlite3-dev | grep -q 'install ok installed' || { echo "Install libsqlite3-dev (or --install-deps)" >&2; exit 1; }
cat /etc/os-release
nvidia-smi --query-gpu=index,name,driver_version,memory.total,compute_cap --format=csv
[[ -x "$cuda/bin/nvcc" ]] || { echo "CUDA toolkit missing: $cuda. Use deploy/install-cuda13.sh for isolated CUDA13 (supports pre-downloaded .deb files)." >&2; exit 1; }
"$cuda/bin/nvcc" --version
command -v cmake
cmake --version
command -v g++-12
g++-12 --version | head -1
"$python" --version
printf 'PID1: '; ps -p 1 -o comm=
printf 'Public port: %s; prefix: %s\n' "$port" "$prefix"
if [[ "$command" = doctor ]]; then
    exit 0
fi
[[ -d "$models" ]] || { echo "--models must be an existing model directory" >&2; exit 1; }
[[ "$private_http" = 1 || ( -f "$cert" && -f "$key" ) || "$bind" = 127.0.0.1 ]] || { echo "Supply TLS --cert/--key, or explicitly --private-http on trusted networks" >&2; exit 1; }
mkdir -p "$prefix/releases" "$prefix/state" "$prefix/logs"
exec 9>"$prefix/install.lock"
flock -n 9 || { echo "Another installation is active" >&2; exit 1; }
if "$python" "$source_root/ForServer/deploy/service.py" running --prefix "$prefix" >/dev/null 2>&1; then
    echo "Stop the running installation before switching releases" >&2
    exit 1
fi
if [[ -f "$prefix/config.json" && "$reconfigure" = 0 ]]; then
    devices=$("$python" -c 'import json,sys; print(json.load(open(sys.argv[1]))["devices"])' "$prefix/config.json")
    enable_multi_gpu=$("$python" -c 'import json,sys; print(int(json.load(open(sys.argv[1])).get("enable_multi_gpu", False)))' "$prefix/config.json")
fi
[[ "$devices" != *,* || "$enable_multi_gpu" = 1 ]] || { echo "Multiple GPUs disabled; explicitly --enable-multi-gpu" >&2; exit 2; }
capabilities=$(nvidia-smi -i "$devices" --query-gpu=compute_cap --format=csv,noheader | tr -d '.' | sort -u)
for capability in $capabilities; do
    [[ "$capability" -ge 75 ]] || { echo "Selected GPU requires compute capability >=7.5 for CUDA13" >&2; exit 2; }
done
architectures=$(printf '%s\n' "$capabilities" | awk '{printf "%s-real;%s-virtual;", $1, $1}' | sed 's/;$//')
[[ -n "$architectures" ]] || { echo "No CUDA architectures detected" >&2; exit 1; }
source_hash=$(cd "$source_root" && find ForServer include src/model-io third_party/spz third_party/zstd third_party/googletest -type f ! -path '*/.git/*' ! -path '*/__pycache__/*' ! -name '.git' -print0 | sort -z | xargs -0 sha256sum | sha256sum | cut -c1-16)
release="$prefix/releases/$source_hash"
mkdir -p "$release"
cmake -S "$source_root/ForServer" -B "$release/build" -G Ninja -DCMAKE_MAKE_PROGRAM=/usr/bin/ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/g++-12 -DCMAKE_C_COMPILER=/usr/bin/gcc-12 -DCMAKE_CUDA_COMPILER="$cuda/bin/nvcc" -DCMAKE_CUDA_HOST_COMPILER=/usr/bin/g++-12 -DCUDAToolkit_ROOT="$cuda" -DGS_SERVER_CUDA_ARCHITECTURES="$architectures" 2>&1 | tee "$prefix/logs/configure.log"
cmake --build "$release/build" --parallel 8 2>&1 | tee "$prefix/logs/build.log"
ctest --test-dir "$release/build" --output-on-failure 2>&1 | tee "$prefix/logs/ctest.log"
mkdir -p "$release/bin" "$release/licenses"
export CGO_ENABLED=1 GOTOOLCHAIN=local
(cd "$source_root/ForServer/gateway" && "$go_tool" test -race ./... && "$go_tool" vet ./... && "$go_tool" build -trimpath -o "$release/bin/gs-gateway" .) 2>&1 | tee "$prefix/logs/go-tests.log"
go_root=$("$go_tool" env GOROOT)
cp "$go_root/LICENSE" "$release/licenses/Go-LICENSE"
"$python" -m unittest discover -s "$source_root/ForServer/tools" -p '*_test.py' -v 2>&1 | tee "$prefix/logs/gateway-tests.log"
mkdir -p "$release/bin" "$release/tools" "$release/deploy"
backup=$(mktemp -d "$prefix/.upgrade.XXXXXX")
startup_attempted=0
old_current=$(readlink "$prefix/current" || true)
old_previous=$(readlink "$prefix/previous" || true)
for name in config.json service.py; do
    [[ ! -f "$prefix/$name" ]] || cp -p "$prefix/$name" "$backup/$name"
done
restore_upgrade() {
    result=$?
    if [[ "$result" != 0 ]]; then
        if [[ "$startup_attempted" = 1 ]]; then
            "$python" "$prefix/service.py" stop --prefix "$prefix" || {
                echo "Upgrade supervisor could not stop; leaving transaction files in $backup for recovery" >&2
                exit "$result"
            }
        fi
        for name in config.json service.py; do
            if [[ -f "$backup/$name" ]]; then cp -p "$backup/$name" "$prefix/$name"
            else rm -f "$prefix/$name"; fi
        done
        if [[ -n "$old_current" ]]; then ln -sfn "$old_current" "$prefix/current"
        else rm -f "$prefix/current"; fi
        if [[ -n "$old_previous" ]]; then ln -sfn "$old_previous" "$prefix/previous"
        else rm -f "$prefix/previous"; fi
        echo "Upgrade failed; previous binaries/configuration/supervisor restored" >&2
    fi
    [[ "$backup" = "$prefix"/.upgrade.* ]] && rm -rf "$backup"
    exit "$result"
}
trap restore_upgrade EXIT
cp "$release/build/gs-server" "$release/build/gs-frame" "$release/bin/"
cp "$source_root/ForServer/tools/gateway.py" "$release/tools/"
cp "$source_root/ForServer/deploy/service.py" "$release/deploy/"
cp "$source_root/ForServer/deploy/service.py" "$prefix/service.py"
control=("$python" "$prefix/service.py")
configure=(configure --prefix "$prefix" --models "$models" --devices "$devices" --port "$port" --worker-port "$worker_port" --bind "$bind")
configure+=(--instance-idle "$instance_idle" --cache-ttl "$cache_ttl" --cache-bytes "$cache_bytes" --max-instances "$max_instances")
[[ "$enable_multi_gpu" = 1 ]] && configure+=(--enable-multi-gpu)
[[ "$private_http" = 1 ]] && configure+=(--private-http)
[[ "$reconfigure" = 1 ]] && configure+=(--reconfigure)
[[ -n "$cert" ]] && configure+=(--cert "$cert" --key "$key")
"${control[@]}" "${configure[@]}"
if [[ -L "$prefix/current" ]]; then
    ln -sfn "$(readlink "$prefix/current")" "$prefix/previous"
fi
ln -sfn "$release" "$prefix/current.new"
mv -Tf "$prefix/current.new" "$prefix/current"
if [[ "$no_start" = 0 ]]; then
    startup_attempted=1
    if ! "${control[@]}" start --prefix "$prefix"; then
        if [[ -L "$prefix/previous" ]]; then
            ln -sfn "$(readlink "$prefix/previous")" "$prefix/current"
            echo "Startup failed; previous release will be restored (not automatically restarted)" >&2
        fi
        exit 1
    fi
fi
printf 'Installed: %s\nToken file: %s/token (keep secret)\n' "$release" "$prefix"
