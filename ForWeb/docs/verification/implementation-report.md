# WebGPU/WASM 实现与验收记录

日期：2026-10-04；本地工程版本0.1.0。本报告区分已实现、实测通过和仍待验收的能力。Windows/Edge154/RTX3080是本轮实测环境，不能代替跨浏览器、移动或跨厂商验收。

## 1. 交付结构

| 路径 | 职责 |
| --- | --- |
| src/splat-types | Result、错误、资源策略、场景与进度值类型 |
| src/model-io | Blob/绝对HTTP(S) URL、独立解码Worker、WASM ABI |
| native | C++20 Web封装、PLY字段预编译、SPZ安全验证、GPU页打包 |
| src/render-core | WebGPU设备/预算、WGSL投影/SH、全局稳定排序、绘制/读回 |
| src/engine | latest-wins事务、相机、surface、订阅、恢复与释放 |
| src/web-adapters、src/react.ts、src/vue.ts | DOM控制、React hook、Vue composable |
| apps | 本地查看器和独立SparkJS对照页面 |
| tests、tools | CPU/WASM/GPU/图像/联合/安装包/模型/长稳与性能工具 |
| docs、tasks | provider规格、ADR、任务、接入说明及验收证据 |

原生Windows、Android和云服务源码未修改。WASM编译复用根仓库model-io的probe/normalize/SceneWriter和固定SPZ/zlib/zstd源码。Web-only限制与gzip验证在native封装实现，不改变原生公开合同。开发流程采用“provider规格→任务→组件测试→联合测试→独立审查→证据归档”，测试工具选用Vitest/Node test/Playwright，与原生GoogleTest具有相同的分层验收原则。

## 2. 可重现环境

Node24.14.1、pnpm11.4.0、TypeScript7.0.2、Vite8.3.2、Vitest5.0.3、Playwright1.63.0、Emscripten6.0.11、CMake4.3.2、clang-format20.1.8。React19.3.0、Vue3.5.43为optional peer。SparkJS2.3.1/Three0.180.0只用于开发对照，不进入SDK。GPU为RTX3080 10GiB，驱动616.92；Edge154.0.4258.53，headless实际NVIDIA/Ampere adapter。

Vue示例编译使用vue-tsc3.3.12、@vitejs/plugin-vue6.0.9、TypeScript6.0.3；vue-tsc在TypeScript7.0.2下实测不兼容。引擎本身仍使用7.0.2。Playwright下载的Chromium在本机启动失败，未计为通过。完整配置见[开发环境](../development-environment.md)。

## 3. 已完成的验收

以下命令从ForWeb执行；GPU、模型和联合测试需要按开发环境文档启动localhost开发服务器。缺少真实硬件必须失败，不能跳过后标通过。

| 命令 | 结果与覆盖 | 证据 |
| --- | --- | --- |
| pnpm run build:wasm | 固定6.0.11，Release O3 wasm32，真实编译/运行 | 7个WASM契约测试、资产manifest |
| tools/verify.ps1 | 类型、lint、模块依赖、18个CPU测试、7个WASM契约、生产SDK构建通过 | tests/unit、tests/contracts |
| pnpm run test:gpu | 0/1/255/256/257/1025/65537/1048576稳定排序对CPU参考；两页全局透明顺序 | [gpu-tests.json](evidence/gpu-tests.json) |
| pnpm run test:images | SH0及全部15个高阶基项逐像素；旋转各向异性、相机背后剔除、subpixel预模糊 | [image-tests.json](evidence/image-tests.json) |
| pnpm run test:integration | 真实PLY/SPZ、坏输入/取消保旧、resize/orbit、异常订阅隔离、恢复保相机、close/dispose | [integration-tests.json](evidence/integration-tests.json) |
| 联合边界夹具 | 真实零尺寸Canvas初始化/延迟激活/总deadline；非法URL；注入validation事件进入Faulted、拒绝open、显式恢复 | 同上；注入事件不等于真实硬件reset |
| 实际资源准入 | 1100byte GPU预算、80MiB CPU预算，close fence未完成时拒绝新模型；真正释放后同一输入成功 | integration-tests.json，避免只凭active估计 |
| pnpm run test:sdk | 独立tgz安装、Node三个SSR入口、文档TSX/SFC严格编译、Vite生产/开发、真实WASM解码与卸载 | [sdk-consumer.json](evidence/sdk-consumer.json) |
| 最终本地SDK包核验 | 49个条目、全部部署资产SHA一致、完整许可及三入口/指南；不含私有模型、evidence、依赖或日志 | [package-check.json](evidence/package-check.json) |
| 安装包生命周期 | 生产创建2个device；开发StrictMode创建3个，最终只2个活动；卸载全Stopped、0device、0Canvas | 同上 |
| pnpm run test:models | 38模型逐个走实际加载/拒绝流程，6次替换+6次取消及close/recover | [model-tests.json](evidence/model-tests.json) |
| pnpm run test:stability | 100次生命周期、30分钟运动、60次采样；无错误，dispose资源全零 | [stability-tests.json](evidence/stability-tests.json) |
| pnpm run bench | 三模型、两引擎，30秒预热+60秒×3；同相机九图，保存CSV和PNG | [性能报告](performance-report.md) |

默认排序为分层前缀的4-bit稳定LSD；8-bit内部实验也通过CPU排序参考，并且九视角与4-bit RGBA完全一致。8-bit未改善整帧，未作为默认发布。

最终源码/配置及部署资产SHA见[build-manifest.json](evidence/build-manifest.json)。pnpm audit查询为0已知漏洞，见[dependency-audit.json](evidence/dependency-audit.json)，不代表没有未知风险。原始evidence属于仓库资料，不随SDK包分发；安装包中的报告链接需回到本仓库查看。

## 4. 真实模型覆盖与资源限制

模型来自用户指定的`C:\Users\21544\Desktop\zhishan`，只记录相对文件名、大小、头部信息和SHA-256，不把模型文件放入仓库/包。默认input256MiB、CPU估算1536MiB、GPU512MiB，WASM最大1GiB，960MiB保守峰值准入。

| 成功样本 | 点数 | 格式/SH |
| --- | ---: | --- |
| changjin_v1 | 170799 | PLY/SH3 |
| zhishan_v3 | 649520 | PLY/SH3 |
| shengyi_v1 | 804758 | PLY与SPZ/SH3 |
| PML_v1 | 851828 | PLY/SH3 |
| yulin_v1 | 1027833 | PLY与SPZ/SH3 |
| tumu_v1 | 1248730 | SPZ/SH3 |

共8个文件成功、30个文件返回ResourceLimit，没有把拒绝算作“显示通过”。样本有约5.3GB PLY及约500MB SPZ，完整解码超出本版wasm32/四页/预算配置。提高某一个limit不能消除其他上限；本版不抽点、不降SH、不启用隐式LoD。完整大型校园模型支持仍需要分段SPZ解码、流式GPU驻留/LoD等后续设计。

## 5. 稳定性

正式命令`pnpm run test:stability`完成100次生命周期循环及随后30分钟真实changjin模型相机运动，每30秒强制GC后采样。34次成功替换、33次取消、33次创建/卸载；每10次close回到1device/1context/4buffer/416bytes/0Worker固定资源。

最终elapsedMs为1800008，共60次采样、215996次运动回调，全部Ready且errors为空。场景驻留一直为1device/1context/20buffer/54742000bytes/0Worker；GC后JS heap范围46305693–46338417bytes，约46.31–46.34MB。close回到上述固定空场景资源，dispose后device/context/buffer/bytes/Worker全为0，phase为Stopped，[stability-tests.json](evidence/stability-tests.json)为complete:true。运动回调数量不是独立证明的物理呈现帧数。两个修复前被主动中断的记录不计通过。

ownedResources是测试插桩统计的资源所有权和GPUBuffer分配字节，JS heap来自浏览器performance.memory；它不代表实际VRAM residency、整个进程RSS或驱动内部缓存。安装包和模型功能复测可在独立浏览器执行，本项没有性能比较用途。当前长稳不是浏览器后台/睡眠恢复试验。

## 6. 未通过或未实施的门槛

| 项目 | 当前状态 |
| --- | --- |
| 对Spark可比的用户可见帧时间降低20% | 未通过；正式无节流循环中位间隔未降低20%，物理呈现/等动态排序质量尚未验收 |
| 跨AMD/Intel、macOS/Safari、Firefox、Android/iOS | 未测试；只有本机Edge/NVIDIA证据 |
| 真实硬件device loss/reset | 未测试；已覆盖模拟事件、CPU竞态和真实设备显式重建 |
| 大于当前wasm32/四页上限的完整模型 | 明确拒绝；后续流式解码/驻留设计 |
| 渲染Worker、SIMD/pthreads、LoD、WebXR、多模型、Three场景混合 | 未实施；不导出虚构参数 |
| 云渲染客户端/协议联调 | 未实施，见SPEC-cloud-client |
| pinch/多指/完整键盘飞行绑定 | 未实施；已提供Camera fly/look供宿主绑定 |
| 生产CSP/CORS、Next/Nuxt具体产品、公开发布许可 | 宿主/发布阶段验收；当前为本地SDK，无发布动作 |

详细审查修复与每个接口的机制覆盖见[接口矩阵](review-and-interface-matrix.md)。
