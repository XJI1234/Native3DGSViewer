# Native3DGS Web

可嵌入的 TypeScript/WebGPU 3DGS 引擎。C++20/WASM 解码 PLY/SPZ，独立 Worker 处理文件，WGSL 执行投影、SH0–3、全局稳定 radix 排序和透明绘制。React/Vue 是同一 SDK 的可选子路径。

当前为本地可构建、可消费的 0.1.0 工程版本，已在 Windows、Edge 154、RTX 3080 上验证。其他浏览器/系统、移动触控、线程/SIMD 和云客户端需独立实施或验收。实际门槛见[验收报告](docs/verification/implementation-report.md)。

已通过18个CPU测试、7个WASM契约、真实GPU/图像/安装包测试及100次生命周期+30分钟长稳。38个模型中8个完整加载、30个明确超限拒绝。Spark有节流/无节流对照已完成，20%中位间隔目标未达成；物理呈现及等动态排序质量仍待验收，详见性能报告。

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
- [性能及 Spark 对照](docs/verification/performance-report.md)
- [接口、异常机制与审查](docs/verification/review-and-interface-matrix.md)

默认输入256 MiB、GPU512 MiB，WASM最大1 GiB并提前检查估算峰值。超预算返回 ResourceLimit，保留旧场景；不减少点数或SH。第三方完整许可随SDK提供。仓库自有代码的公开分发许可需所有者决定。
