# Web SDK接口、异常机制与独立审查

2026-10-04。审查对象是ForWeb实际源码和测试；历史候选接口不视为已经导出。公共类型以src/index.ts、src/react.ts、src/vue.ts生成的声明为准。本版满足独立本地SDK配置的接口集合，尚未满足架构草案全部长期能力。

## 1. 公共接口覆盖

| 接口 | 实施机制 | 验证入口 | 约束 |
| --- | --- | --- | --- |
| createEngine / WebEngine.create | adapter/device、稳定排序自检、资源失败清理 | GPU、安装包、初始化失败CPU测试 | 实际WebGPU，无软件fallback |
| open / LoadOperation.result / cancel | latest-wins、Worker终止、总deadline、首帧完成后替换 | model-tests、integration、lifecycle | 失败/取消保旧，不半模型显示 |
| resize | 物理像素、viewportRevision、零尺寸等待 | 真实零Canvas、upload/first-frame collapse CPU夹具 | CSS/DPR由适配或宿主转换 |
| pause / resume / requestFrame | 按需rAF、隐藏跳过，不覆盖事务/Faulted | lifecycle、GPU轨迹 | pause不释放模型 |
| fitScene | 含Gaussian尺度支撑的包围盒 | camera、真实模型 | FOV60度 |
| Camera pose/orbit/pan/dolly/look/fly/reset | float64、relative origin、数值校验/roll | camera测试、轨迹/图像 | fly四参数是速度系数/时间 |
| getSnapshot / subscribe | identity稳定、嵌套copy/freeze、异常隔离 | immutability、真实订阅 | stats约4Hz，GPU时间来自旧frameId |
| closeScene | request/sceneEpoch失效、GPU完成释放 | immediate close/recover、恢复upload竞态、模型stress | 恢复按实际active重发元数据 |
| recover | 合并promise、有界两次重建、CPU场景/相机保留 | CPU竞态、真实显式重建、注入validation | 真实硬件reset未测试 |
| capture | 对齐读回、BGRA→RGBA、独立副本、累积reservation | 两页像素、相机图、并发capture CPU测试 | 稳定非零surface、黑背景 |
| dispose | 幂等、停止回调、等待upload/recovery、GPU释放 | 重入CPU、StrictMode、长稳 | Stopped不能复用 |
| capabilities | adapter/pageBytes/execution | GPU证据、SDK类型 | main，不是剩余VRAM查询 |
| React useNative3DGS | effect独占Canvas、迟到create清理、useSyncExternalStore | 开发3device→2active→0，生产2→0 | options在mount固定 |
| Vue useNative3DGS | markRaw/shallowRef、mounted/unmount、订阅清理 | 独立消费、SFC严格编译、卸载 | KeepAlive需宿主pause |
| ESM/assets | SSR安全、显式workerUrl、固定资产/哈希/license | 独立tgz、生产bundle、真实WASM | 复制完整资产，未发布npm |

没有导出LoD、云客户端、WebXR、renderWorker、可调FOV/透明背景参数。实际接入见[SDK](../SDK-guide.md)、[React](../React-integration.md)、[Vue](../Vue-integration.md)。

## 2. 输入与异常机制

| 情形 | 实施 | 验证及局限 |
| --- | --- | --- |
| 损坏/截断PLY、非有限字段、部分chunk | probe/SceneWriter拒绝，DecoderFailure | WASM契约、重排/SH语义 |
| SPZ1–4/SH0–3 | 精确头部与v4属性流长度、完整规范化 | 16组版本×degree合成夹具、截断拒绝、真实SPZ |
| 旧gzip恶意膨胀/尾部 | 64KiB scratch、精确inflated长度、CRC、无trailing成员 | 一点声明+8MiB压缩尾部、截断/拼接成员 |
| unsupported PLY/SH/flags | UnsupportedFormat，不降质 | shared probe及contracts |
| URL/下载 | HTTP(S)协议校验；HTTP/reader错误NetworkFailure | 真实file URL拒绝；CORS由浏览器执行 |
| input/CPU/WASM/GPU限制 | 下载检查、峰值准入、新旧总量、capture reservation | 最终38/38完整加载；明确超限夹具仍拒绝；估算不是RSS |
| cancel/timeout | 单结算、terminate、上传信号、含零surface deadline | latest-wins、真实stress和超时 |
| validation/OOM/lost | uncaptured进入Faulted，lost有界recover，主动destroy不恢复 | 注入validation/显式重建；真实OOM/reset未制造 |
| close/recover/dispose重叠 | sceneEpoch、barrier、coalesced promise、active metadata | deferred upload、subscriber reentry、立即close/recover |
| Worker/宿主回调异常 | 单结算cleanup、观察者隔离 | 实际订阅异常；未知浏览器Worker crash需宿主监测 |
| 初始化/适配失败 | renderer/context清理、迟到create立即dispose | CPU初始化失败、StrictMode+Vue |
| 零尺寸 | 首帧前后检查、revision重试重投影、lazy configure | 浏览器零Canvas/恢复、两个异步CPU夹具 |

ErrorCode：UnsupportedCapability、InvalidInput、UnsupportedFormat、ResourceLimit、OutOfMemory、DecoderFailure、Cancelled、Timeout、NetworkFailure、DeviceLost、Stopped。异步engine方法返回Result；Camera数学操作同步抛Error。diagnostic最多512字符，业务按code分支。

## 3. 独立审查闭环

应用code-review-and-quality技能，独立只读代理检查正确性、安全、生命周期、维护性和合同。第一个审查模型服务不可用，没有计为通过；随后代理完成初审和复审，未修改源码或运行GPU。

| 发现 | 修复 | 回归 |
| --- | --- | --- |
| P1 gzip绕过count准入 | bounded精确验证+完整头部复检 | 恶意尾部、截断、拼接gzip |
| P1 observer重入重复recover/dispose | guard先存，之后启动/发布 | promise identity、单次replacement/清理 |
| P2 upload时viewport归零 | upload后等待、首帧后revision检查 | 两个deferred upload/queue夹具 |
| 复审：重试复用-2 revision | 每轮唯一provisional revision | 两次render的revision不同 |
| P2并发capture绕过GPU预算 | 分配前reserve，完成/失败release，upload计入 | 并发拒绝、allocation异常后可重试 |
| P2snapshot浅冻结 | nested复制冻结 | 修改失败，旧快照不受影响 |
| P2fly缺seconds/单位错 | 四参数与距离×速度系数×clamp时间 | 声明、文档、实现对齐 |
| 浏览器零Canvas创建失败 | 延迟configure，失败创建unconfigure | 0×0→64×64→Timeout→recover |
| 全模型close/recover快照残留 | guard先存后同步失效，按active发布metadata | red→green CPU，38模型+12次stress复跑 |
| 失败恢复/被拒绝open导致close元数据残留 | close同步清空、失败终态按active/retained发布 | 两次create失败及Faulted再close |
| close等待期间新fault诊断被旧patch覆盖 | 关闭只同步发布一次 | deferred queue期间注入fault，诊断保留 |
| close解除active但GPU资源尚未释放 | renderer追踪residentBytes/ownedScenes直到真正release | 1100byte预算真实浏览器释放前拒绝/释放后可开 |
| close解除active但CPU页仍持有 | renderer追踪cpuResidentBytes并传入decode准入 | 18个CPU测试及80MiB浏览器夹具：10万→15万点释放前拒绝、释放后通过 |

独立初审六项与复审缓存遗漏全部关闭。全模型测试发现的快照问题也有失败再通过的回归。未删除失败测试、抽点、降SH或放宽上限换通过。

## 大型模型复审补充

独立只读复审覆盖64点分块、末尾padding、SH、OPFS引用、并发清理与CPU scratch。必改项已关闭：宽PLY同时限制源字节；fractionalBits25–30与原数学一致；加载完成promise在发布Loading前注册；close/dispose等待包括存储删除在内的操作收尾；清理异常结构化并不妨碍GPU销毁；timestamp等待纳入原2秒超时。回归覆盖subscriber重入与deferred操作。

最终最大PLY发现批次局部原点float32舍入误拒绝，流式finish保留world中心与原始bounds再用零origin执行完整共享validator。没有放宽native容差。12bytes/point快照计入现有native scratch余量。复审未发现未处理必改问题。65/127/129点全属性契约、129点三页/SH0–3和真实模型布局图像等价均通过；最终38/38及SDK证据见[大型报告](large-model-optimization-report.md)。

## 4. 维护与交付

新增源码/规格/证据限ForWeb，third_party未修改。C++固定formatter，TS严格模式/Biome，check:boundaries约束provider方向；集中verify，性能单独运行。WASM/Worker SHA及完整licenses随包部署。

模型原文件、RGBA、构建产物、依赖和临时项目被ignore；PNG/CSV/JSON保留。SDK含小规格/报告/指南，不含大evidence或私有模型。owned资源计数不是RSS/VRAM；跨平台和真实硬件fault仍待验收。交付版本为0.2.2-preview.1，Web PR和原生v0.2.2上的附加资产记录实际Web commit，不改变原生tag，也不发布npm。

六轮OCR审查、所有观察项的处置及最终回归见[OCR闭环报告](ocr-review-report.md)。最终源码新增可移除device-loss订阅、挂起等待取消、实际Canvas呈现验证与回滚、条件React/Vue容器重绑定、OPFS清理deadline和构建输出inventory验证；GPU、联合、真实安装包及38模型验收见[发布结果](large-model-optimization-report.md)。
