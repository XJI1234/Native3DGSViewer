# Web 技术栈与资料基线

状态：保留架构阶段的选型/查询记录；2026-10-04。实际经验证组合固定于package.json/pnpm-lock.yaml和[开发环境](development-environment.md)：Node24.14.1、pnpm11.4.0、Emscripten6.0.11、TS7.0.2、Vite8.3.2、React19.3.0、Vue3.5.43。下表的Node/pnpm查询快照未用于本次构建，TypeDoc未安装。版本升级需重新执行消费/GPU回归，不将历史latest查询视为实际验证。

## 1. 工具链快照

| 组件 | 查询版本 | 已发布日期 | 用途 |
| --- | --- | --- | --- |
| Node.js | 24.21.0 LTS，Krypton | 2026-09-07 | 开发/CI 运行时；选 LTS，避免 Current 漂移 |
| pnpm | 12.8.1 | 2026-09-28 | workspace、锁文件、隔离依赖 |
| TypeScript | 7.0.2 | 2026-07-08 | SDK 类型与严格检查 |
| Vite | 8.3.2 | 2026-10-01 | 示例、浏览器夹具、ESM 库构建 |
| Vitest | 5.0.3 | 2026-09-30 | CPU 状态/数学/适配层测试 |
| Playwright Test | 1.63.0 | 2026-09-04 | 浏览器流程、截图及消费测试 |
| React | 19.3.0 | 2026-09-09 | 可选 React 适配包与示例 |
| Vue | 3.5.43 | 2026-09-17 | 可选 Vue 适配包与示例 |
| `@webgpu/types` | 0.1.74 | 2026-09-19 | WebGPU 类型声明，不是运行时 polyfill |
| Biome | 2.5.15 | 2026-09-30 | TS/JS 格式与静态检查 |
| TypeDoc | 0.28.20 | 2026-07-05 | SDK API 文档候选 |
| Emscripten/emsdk | 待 M0 查询并锁定具体稳定 tag/commit | 本轮查询未成功 | 编译 C++20、SPZ、zlib/zstd 到 wasm32 |

来源：[Node 发布索引](https://nodejs.org/dist/index.json)、[npm Registry](https://registry.npmjs.org/)、[emsdk](https://github.com/emscripten-core/emsdk)。npm 查询为 `https://registry.npmjs.org/<URL 编码包名>` 的 `dist-tags.latest`、`time[version]`、`engines`、`license` 字段。

本轮查询 Node 与全部 npm 行成功。emsdk 的 GitHub latest release 接口返回 404，随后 tags 元数据读取超时，GitHub contents API 重试返回 403 rate limit exceeded，因此不填写猜测版本。M0 必须保存查询原文与哈希后才能封板。TypeDoc 与 TypeScript 7 的组合也须通过实际构建验证；不兼容时使用有证据的兼容版本并记录决定。

**“最新”指启动实施时重新核对可用稳定版本，再固定经验证的组合。** 不使用 `latest`、`*` 或宽松范围作为实际工具链锁定方式。`packageManager` 固定 pnpm 精确版本，Node 使用版本文件及 CI 固定版本，提交 `pnpm-lock.yaml`；npm 包记录完整性，emsdk 固定 tag 与 commit，WASM 构建记录编译参数。跨版本升级先跑全部消费者与 GPU 画质回归。

SPZ/zlib/zstd 和 GoogleTest 延续[仓库源码固定版本](../../third_party/README.md)。SPZ v3.0.0 的源码固定提交已支持基础 SPZ v1–v4；库版本与文件版本分别记录。更新它们时同时更新三个平台的契约回归与许可清单。FidelityFX HLSL 只作稳定排序算法/行为参照；WGSL 不能直接依赖其 WaveOps 内核。

## 2. 官方技术依据

| 资料 | 对应决定 |
| --- | --- |
| [WebGPU 规范](https://www.w3.org/TR/webgpu/) | `requestAdapter/requestDevice`、device limits/features、error scope、queue、Canvas 配置与资源使用 |
| [WGSL 规范](https://gpuweb.github.io/gpuweb/wgsl/) | host-shareable 对齐、工作组内存/屏障、uniformity、可选扩展 |
| [WebGPU limits](https://developer.mozilla.org/en-US/docs/Web/API/GPUSupportedLimits) | 根据实际设备限制分页；不把典型默认值当剩余显存 |
| [GPUDevice.lost](https://developer.mozilla.org/en-US/docs/Web/API/GPUDevice/lost) | 设备丢失 Promise 与有界恢复 |
| [OffscreenCanvas](https://developer.mozilla.org/en-US/docs/Web/API/OffscreenCanvas) | Canvas 交给 Worker 的所有权及独立能力检测 |
| [Emscripten 优化](https://emscripten.org/docs/optimizing/Optimizing-Code.html) | SIMD 候选、优化等级、内存与发布产物 |
| [Emscripten pthreads](https://emscripten.org/docs/porting/pthreads.html) | 多线程构建与 COOP/COEP，不能让基础 SDK 强制跨源隔离 |
| [WebAssembly JS API](https://webassembly.github.io/spec/js-api/) | WASM 线性内存与 JS 边界 |
| [React useSyncExternalStore](https://react.dev/reference/react/useSyncExternalStore) | 稳定快照订阅和 SSR 快照 |
| [Vue Composition API 生命周期](https://vuejs.org/api/composition-api-lifecycle.html) | mount/unmount 与 composable 清理 |
| [Vite 库模式](https://vite.dev/guide/build.html#library-mode) | ESM、外置框架依赖与资产发布 |
| [Playwright 浏览器](https://playwright.dev/docs/browsers) | 浏览器测试矩阵；其 WebKit 不替代真实 Safari GPU 验收 |

WebGPU、WGSL、Emscripten pthreads/优化、OffscreenCanvas 与 GPUDevice.lost 页面本轮访问成功；已核对 pthreads 需要 COOP/COEP、线程/非线程须分别构建，以及主动 destroy 后不应自动恢复的规则。其余链接作为官方设计依据索引；浏览器兼容性和工具链支持必须在 M0 实测，不因页面存在而宣称支持。

## 3. 拟议架构决定

| 决定 | 原因与代价 | 替代方案及当前处置 |
| --- | --- | --- |
| 直接 WebGPU/WGSL + 无框架 TypeScript 内核 | 控制 GPU 帧图、内存、全局排序；便于任意宿主集成；需要自行维护数学/资源管线 | Three.js/Babylon renderer 会引入其生命周期与数据结构；后续互操作另立契约 |
| C++20/WASM 解码，TS 协调，GPU 排序 | 复用现有格式规范化与样本；避免逐点 JS 桥接 | Rust 重写会增加两份 codec 及一致性维护，首期不采用 |
| 单线程 WASM + 独立解码 Worker 为基础 | 不要求 SharedArrayBuffer/跨源隔离；原生库阻塞时可终止 Worker | pthreads 仅作独立可选产物，在部署条件和性能收益实测后启用 |
| 可选 SIMD，基准 float32 GPU 数据 | 以实测改善解码；保留不支持 SIMD 的可用路径 | f16/量化/GPU 解压属于单列实验，先验图像再谈性能 |
| 原生二进制 Worker 消息协议 | 请求代际、transfer 所有权和背压可见且可测试 | Comlink 不作为大数组热路径依赖；不隐藏缓冲复制 |
| React/Vue 薄适配、同一底层 SDK | 框架版本解耦、SSR 安全、多视口隔离 | 内核不依赖组件状态或响应式代理 |
| 云客户端独立可选 | 保持模型保密边界与本地 GPU 解耦 | 不复活已退役的 ProjectedSplats 协议，不自动上传用户本地文件 |

以上均为 Proposed。本次系统设计集中记录决定；架构批准后，改变核心路线应新增本目录 `docs/decisions/` 下的 ADR，记录上下文、备选、证据及被替代决定，不删除历史。
