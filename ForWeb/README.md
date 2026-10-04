# Native3DGS Web

可嵌入的 TypeScript/WebGPU 3DGS 引擎。C++20/WASM 解码 PLY/SPZ，独立 Worker 处理文件，WGSL 执行投影、SH0–3、全局稳定 radix 排序和透明绘制。React/Vue 是同一 SDK 的可选子路径。

当前为本地可构建、可消费的 0.2.2-preview.1 工程版本，已在 Windows、Edge 154、RTX 3080 上验证。其他浏览器/系统、移动触控、线程/SIMD 和云客户端需独立实施或验收。实际门槛见[验收报告](docs/verification/implementation-report.md)。

发布源码已通过36个CPU测试、18个契约测试（含WASM与基准守护）、38/38完整加载、100次生命周期和5分钟稳定性测试，最大22,480,361点保留全部SH3。GPU/图像、独立SDK消费及阶段性能见[大型模型优化验收](docs/verification/large-model-optimization-report.md)，六轮OCR处理记录见[审查报告](docs/verification/ocr-review-report.md)。早期30分钟长稳和无节流数据保留为历史证据；物理呈现与跨平台门槛仍待验收。

## 开发

初始化根仓库 third_party 子模块后，在本目录执行：

```powershell
pnpm install --frozen-lockfile
pnpm run build:wasm
pnpm run build
$env:GS_MODEL_ROOT = 'C:\Users\21544\Desktop\zhishan'
pnpm run dev
```

打开 http://127.0.0.1:5173 选择模型。模型不会复制到仓库；开发服务器只在显式配置本地目录时提供 /models。

```powershell
pnpm run test:contracts
pnpm run test:unit
pnpm run typecheck
pnpm run lint
pnpm run check:boundaries
pnpm run test:gpu
pnpm run test:images
pnpm run test:integration
pnpm run test:models
pnpm run test:sdk
pnpm run test:stability
pnpm run bench
```

GPU/模型/性能命令需要开发服务器和真实硬件。独立 SDK 测试打包、重新安装并使用自己的生产服务器。详见[开发环境](docs/development-environment.md)。

## 网站集成

本地 pnpm pack 产生安装包，没有发布 npm。入口为 @native3dgs/web、@native3dgs/web/react、@native3dgs/web/vue。生产复制 dist/assets 并显式设置 baseUrl 和 workerUrl。

- [SDK/API 与部署](docs/SDK-guide.md)
- [React 完整接入](docs/React-integration.md)
- [Vue 完整接入](docs/Vue-integration.md)
- [能力图](CAPABILITY-MAP.md)、[系统设计及实施差异](docs/system-technical-design.md)
- [Spark解码/WASM/建树源码研读](docs/verification/spark-decoder-source-study.md)
- [性能及 Spark 对照](docs/verification/performance-report.md)
- [接口、异常机制与审查](docs/verification/review-and-interface-matrix.md)

默认input/scene/GPU策略预算各8GiB、CPU512MiB、加载期限10分钟。大型PLY/旧gzip SPZ用OPFS及有界WASM批次，WASM最大1GiB；GPU/存储不足仍会拒绝。超预算返回 ResourceLimit，保留旧场景；不减少点数或SH。第三方完整许可随SDK提供。仓库自有代码的公开分发许可需所有者决定。
