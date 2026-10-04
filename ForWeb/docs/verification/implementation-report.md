# WebGPU/WASM 实现与验收记录

日期：2026-10-04；本地工程版本0.1.0。本报告区分已实现、实测通过和仍待验收的能力。Windows/Edge154/RTX3080是本轮实测环境，不能代替跨浏览器、移动或跨厂商验收。

最新完整大型模型实现及最终证据见[大型模型优化验收](large-model-optimization-report.md)。第5节的30分钟长稳与旧无节流对照属于早期实现的历史数据。

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
| pnpm run build:wasm | 固定6.0.11，Release O3 wasm32，真实编译/运行 | 12个WASM契约测试、资产manifest |
| tools/verify.ps1 | 类型、lint、模块依赖、25个CPU测试、12个WASM契约、生产SDK构建通过 | tests/unit、tests/contracts |
| pnpm run test:gpu | 0/1/255/256/257/1025/65537/1048576稳定排序对CPU参考；两页全局透明顺序 | [gpu-tests.json](evidence/gpu-tests.json) |
| pnpm run test:images | SH0及全部15个高阶基项逐像素；旋转各向异性、相机背后剔除、subpixel预模糊 | [image-tests.json](evidence/image-tests.json) |
| pnpm run test:integration | 真实PLY/SPZ、坏输入/取消保旧、resize/orbit、异常订阅隔离、恢复保相机、close/dispose | [integration-tests.json](evidence/integration-tests.json) |
| 联合边界夹具 | 真实零尺寸Canvas初始化/延迟激活/总deadline；非法URL；注入validation事件进入Faulted、拒绝open、显式恢复 | 同上；注入事件不等于真实硬件reset |
| 实际资源准入 | 1100byte GPU预算、80MiB CPU预算，close fence未完成时拒绝新模型；真正释放后同一输入成功 | integration-tests.json，避免只凭active估计 |
| pnpm run test:sdk | 独立tgz安装、Node三个SSR入口、文档TSX/SFC严格编译、Vite生产/开发、真实WASM解码与卸载 | [sdk-consumer.json](evidence/sdk-consumer.json) |
| 最终本地SDK包核验 | 早期包49个条目、部署资产SHA一致；最终包单列于大型报告、完整许可及三入口/指南；不含私有模型、evidence、依赖或日志 | [package-check.json](evidence/package-check.json) |
| 安装包生命周期 | 生产创建2个device；开发StrictMode创建3个，最终只2个活动；卸载全Stopped、0device、0Canvas | 同上 |
| pnpm run test:models | 38/38完整加载，精确点数/SH、临时存储清理，6次替换+6次取消及close/recover | [model-tests-final.json](evidence/model-tests-final.json) |
| pnpm run test:stability | 100次生命周期、30分钟运动、60次采样；无错误，dispose资源全零 | [stability-tests.json](evidence/stability-tests.json) |
| pnpm run bench | 三模型、两引擎，30秒预热+60秒×3；同相机九图，保存CSV和PNG | [性能报告](performance-report.md) |

默认排序为分层前缀的4-bit稳定LSD；8-bit内部实验也通过CPU排序参考，并且九视角与4-bit RGBA完全一致。8-bit未改善整帧，未作为默认发布。

最终源码/配置及部署资产SHA见[build-manifest.json](evidence/build-manifest.json)。pnpm audit查询为0已知漏洞，见[dependency-audit.json](evidence/dependency-audit.json)，不代表没有未知风险。原始evidence属于仓库资料，不随SDK包分发；安装包中的报告链接需回到本仓库查看。

## 4. 真实模型覆盖与资源限制

模型位于用户指定的 `C:\Users\21544\Desktop\zhishan`；manifest只记录名称、大小、格式与SHA，模型不进入仓库/包。发布源码38/38成功，包括最大PLY与SPZ各22,480,361点、完整SH3；结果和stress见[model-tests-release.json](evidence/model-tests-release.json)。旧model-tests-final与maximum-final-repeat为此前重复验收记录。

默认input/scene/GPU各8GiB、CPU512MiB、timeout600000ms。大型PLY与SPZ v1–3通过OPFS、有界WASM和不限页数的投影突破整场景wasm32限制；最大模型GPU场景6,575,533,896字节，backing5,305,370,624字节。引擎没有保留整场景ArrayBuffer，浏览器/OS缓存及进程RSS不等于零。发布模型套件最大PLY加载39.381秒、SPZ30.382秒，清理另计；此前两次最大模型加载与清理PLY33.65–35.94秒、SPZ27.13–27.46秒属旧测量。没有用降低点数或SH换取成功。

安全来源、OPFS配额、设备buffer上限和可用VRAM仍是条件。小PLY及SPZ v4保持既有有界内存路径，超出其准入返回ResourceLimit。本机38/38不是任意电脑/浏览器的无条件保证。先关闭旧场景可降低替换峰值，但不能消除物理资源上限。

## 5. 早期实现的历史稳定性

正式命令`pnpm run test:stability`完成100次生命周期循环及随后30分钟真实changjin模型相机运动，每30秒强制GC后采样。34次成功替换、33次取消、33次创建/卸载；每10次close回到1device/1context/4buffer/416bytes/0Worker固定资源。

最终elapsedMs为1800008，共60次采样、215996次运动回调，全部Ready且errors为空。场景驻留一直为1device/1context/20buffer/54742000bytes/0Worker；GC后JS heap范围46305693–46338417bytes，约46.31–46.34MB。close回到上述固定空场景资源，dispose后device/context/buffer/bytes/Worker全为0，phase为Stopped，[stability-tests.json](evidence/stability-tests.json)为complete:true。运动回调数量不是独立证明的物理呈现帧数。两个修复前被主动中断的记录不计通过。

ownedResources是测试插桩统计的资源所有权和GPUBuffer分配字节，JS heap来自浏览器performance.memory；它不代表实际VRAM residency、整个进程RSS或驱动内部缓存。安装包和模型功能复测可在独立浏览器执行，本项没有性能比较用途。当前长稳不是浏览器后台/睡眠恢复试验。

## 6. 未通过或未实施的门槛

| 项目 | 当前状态 |
| --- | --- |
| 对Spark可比的用户可见帧时间降低20% | 早期无节流循环未通过；最终完成帧协议单列测量，物理呈现仍未验收 |
| 跨AMD/Intel、macOS/Safari、Firefox、Android/iOS | 未测试；只有本机Edge/NVIDIA证据 |
| 真实硬件device loss/reset | 未测试；已覆盖模拟事件、CPU竞态和真实设备显式重建 |
| 大型PLY/legacy SPZ完整模型 | 本机38/38通过；其他设备需独立资源/兼容验收，SPZ v4流式未实现 |
| 渲染Worker、SIMD/pthreads、LoD、WebXR、多模型、Three场景混合 | 未实施；不导出虚构参数 |
| 云渲染客户端/协议联调 | 未实施，见SPEC-cloud-client |
| pinch/多指/完整键盘飞行绑定 | 未实施；已提供Camera fly/look供宿主绑定 |
| 生产CSP/CORS、Next/Nuxt具体产品、公开发布许可 | 宿主验收；预览包携带完整第三方许可，自有代码许可由所有者决定 |

详细审查修复与每个接口的机制覆盖见[接口矩阵](review-and-interface-matrix.md)。

## 7. 0.2.2-preview.1发布验收

最终实现通过36个CPU测试、18个契约测试（含WASM与基准守护），真实GPU排序与129点/3页/SH0–3非黑图像、streaming gzip/HTTP/宽PLY、联合timeout→retry→capture、实际tgz的SSR/生产/StrictMode/条件Vue消费均通过。所有38模型完整加载、100次生命周期＋5分钟soak通过，释放后owned资源归零。本版稳定性使用stability-release，不以早期30分钟记录替代。

最终完成帧、布局消融、2248万点阶段数据及局限见[大型报告](large-model-optimization-report.md)。六轮OCR和82条观察处置见[审查闭环](ocr-review-report.md)；最后原始轮0critical/0high、3medium均修复并回归。构建manifest记录完整源输入和dist输出SHA。Web预览资产以实际commit/tree标识附于原生v0.2.2，不移动tag，不自动合并PR，不发布npm。
