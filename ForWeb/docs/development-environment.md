# 本地开发环境与可重现构建

2026-10-06。本机实测 Node 24.14.1、pnpm 11.4.0、CMake 4.3.2、Ninja、Emscripten 6.0.11；RTX 3080 10 GiB，驱动 616.92。工具链是经本轮构建验证的固定组合，区别于架构阶段的版本查询快照。

## Emscripten

本机安装在 `C:\Users\21544\.codex\tools\emsdk`；emsdk 源码 commit `96c657fc60920d2a6a82318aa50e0abf82749604`，6.0.11 工具链 release `f6264d4a4dd9ba24a9f0a5702835a44d1463de13`。默认没有修改系统永久 PATH。

C++格式固定clang-format20.1.8及native/.clang-format，本机VS路径为`C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\bin\clang-format.exe`。Node由.node-version固定；build:wasm核对Emscripten版本文件为6.0.11，偏离则失败。

在 PowerShell 中配置新机器：

```powershell
git clone https://github.com/emscripten-core/emsdk.git C:\dev\emsdk
git -C C:\dev\emsdk checkout 96c657fc60920d2a6a82318aa50e0abf82749604
& C:\dev\emsdk\emsdk.bat install 6.0.11
& C:\dev\emsdk\emsdk.bat activate 6.0.11
$env:GS_EMSDK = 'C:\dev\emsdk'
git submodule update --init --recursive
pnpm --dir ForWeb install --frozen-lockfile
pnpm --dir ForWeb run build:wasm
pnpm --dir ForWeb run test:contracts
pnpm --dir ForWeb run build
```

安装 pnpm 使用你已有的 Node 工具管理器固定到 11.4.0。构建脚本读取 GS_EMSDK，缺失时使用本机用户目录 `.codex/tools/emsdk`，仅构建本地固定源码，无 FetchContent 网络下载。`build:wasm` 顺序构建 wasm32 单线程基础版与 pthreads 增强版；两者最大线性内存均为 1 GiB、启用异常边界捕获、Release O3。增强版预加载有界 pthread pool（2 MiB 栈），默认至多 4 个线程（含协调线程），基础版不要求 COOP/COEP。当前没有 SIMD 产物。

本机 CMake 4.3.2 对 Emscripten 共享库发兼容性警告；本项目只构建静态库及 WASM 可执行模块，构建成功。若开发共享库，需用工具链推荐的兼容 CMake 版本，不沿用此结果。

`public/assets` 和 `dist` 是生成产物，不提交 Git；pack 前先 build:wasm/build。基础及 threaded/decoder.mjs/wasm 在 SDK 中必须同时部署，不能只复制 wasm。Worker 为 Vite 构建的独立模块，生产资产路径见接入指南。

## 验证与样本

```powershell
$env:GS_MODEL_ROOT = 'C:\Users\21544\Desktop\zhishan'
pnpm --dir ForWeb run dev -- --port 5173
# 另一个 PowerShell，进入 ForWeb：
pnpm run typecheck
pnpm run lint
pnpm run check:boundaries
pnpm run test:unit
pnpm run test:contracts
pnpm run test:gpu
pnpm run test:images
pnpm run test:integration
pnpm run test:models
pnpm run test:sdk
pnpm run test:stability
python tools/model-manifest.py --root $env:GS_MODEL_ROOT
```

本轮浏览器测试使用已安装 Edge 的独立临时 profile，headless WebGPU 实测 NVIDIA/Ampere；不使用登录状态、不接受软件 adapter。其他机器可设置 GS_BROWSER_CHANNEL，但需安装匹配的浏览器。Playwright 下载 Chromium 的本机启动出现 spawn UNKNOWN，不能将该下载版本写成已验收；Edge 154 路径通过实际 GPU 测试。

`pnpm run bench`默认保留浏览器rAF节奏；设置`GS_BENCH_UNCAPPED=1`可请求`--disable-frame-rate-limit --disable-gpu-vsync`，结果记录实际参数。必须看持续渲染的间隔分布验证是否真的解除节流；空白页或参数存在不构成证明。正式性能跑与稳定性/其他GPU工作顺序执行。

模型不随仓库或 SDK 分发。manifest 只记录相对名、大小、count/SH 和 SHA-256。开发服务器的 `/models` 仅 localhost、显式 GS_MODEL_ROOT 下的 PLY/SPZ；生产 SDK 没有此文件服务。

## pthreads 实测与复现

普通开发服务保持默认非隔离；增强路径需要第二个隔离服务：

```powershell
$env:GS_MODEL_ROOT = 'C:\Users\21544\Desktop\zhishan'
$env:GS_CROSS_ORIGIN_ISOLATED = '1'
pnpm run dev -- --port 5187 --strictPort
# 另一个终端，进入 ForWeb，按顺序运行以免 GPU/磁盘竞争：
node tools/parallel-capability-tests.mjs # 同时需要默认 5173 非隔离服务
$env:GS_PARALLEL_MODELS = 'shengyi_v1.ply,spz/shengyi_v1.spz,zhihuizhimen.ply'
$env:GS_PARALLEL_CONFIGS = 'single,2,4,8'
$env:GS_PARALLEL_RUNS = '3'
node tools/parallel-browser-tests.mjs
node tools/parallel-spark-benchmark.mjs
```

内核旧版基线需要旧 preview.2 WASM，路径及 SHA 见并行验收报告。测试使用普通临时持久 profile，逐次清理 Worker/OPFS；模型和 GPU 测试不并发运行。不能将 completed 帧吞吐当作显示帧率，或将含 I/O 的阶段计时当作纯 CPU 时间。
