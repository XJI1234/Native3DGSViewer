# Web 连续交互调优：结果、消融与边界

2026-10-05。用户授权依次实施并实测；本轮不以跨引擎画质差异为门槛，但仍保留全点/SH3和稳定排序正确性。

## 来源与推导

1. [WebGPU Queue](https://www.w3.org/TR/webgpu/#gpuqueue)：GPUQueue按提交顺序执行；onSubmittedWorkDone只等待调用时已经提交的工作，不是物理显示完成。MDN的[throttling work](https://developer.mozilla.org/en-US/docs/Web/API/GPUQueue/onSubmittedWorkDone#throttling_work)建议用它限制重计算积压，没有要求每帧只能保留一个提交。实验以2个在途帧重叠主线程准备与GPU执行，保留1/3作为消融。
2. [WebGPU usage scopes](https://www.w3.org/TR/webgpu/#programming-model-resource-usages)：compute pass每个dispatch是独立usage scope。radix各阶段具有dispatch之间的数据依赖，可在同一个compute pass中顺序编码，不能把有依赖的dispatch无序并行。下一实验合并40个pass的边界，保留所有dispatch与barrier。
3. [Spark官方性能建议](https://sparkjs.dev/docs/performance/)区分splat变换、排序、光栅化/混合瓶颈。maxStdDev、分辨率、LoD等参数可以减少工作，但本轮不通过减点、降SH或缩小Gaussian覆盖来提高结果。
4. 本机Spark2.3.1的dist/spark.module.js：onBeforeRender驱动autoUpdate；driveSort后台深度读回和Worker RPC排序，绘制可使用已完成ordering。其异步机制解释连续浏览与串行完成协议的差别。本轮不复制该源码，不将异步旧排序策略作为Web默认优化。
5. [Worker rAF](https://developer.mozilla.org/en-US/docs/Web/API/DedicatedWorkerGlobalScope/requestAnimationFrame)和OffscreenCanvas可隔离主线程UI，但不是额外的GPU算力。在当前主线程提交成本很低且rAF已接近刷新率上限时，先测门控和GPU成本；整套Worker渲染涉及Canvas不可逆转移、能力探针、恢复和host协议，不能以简单移线程替代完整验收。
6. [WASM Memory.grow](https://developer.mozilla.org/en-US/docs/WebAssembly/Reference/JavaScript_interface/Memory/grow#detachment_upon_growing)说明内存增长可使旧ArrayBuffer view失效。此前OPFS rebase读零问题需分别检查实际文件长度与WASM目标view，不以无限重试掩盖。

官方页面2026-10-05读取快照与SHA保存在evidence/parallel-2026-10-05/sources.json，来源仅作为API/语义数据。

## 实验与保留标准

逐项：A 单帧/双帧/三帧门控；B radix dispatch同pass编码；C 根据实际瓶颈决定计时读回或GPU shader改进。每项独立记录源码SHA、失败、三轮范围；无收益或收益在噪声内则撤回。不并发运行GPU测试、不在采样中触发HMR。

主指标为有可见窗口的Edge默认垂直同步连续旋转/缩放/平移：10秒预热，20秒×3，1920×1080、完整点数/SH3。提交FPS、p95/p99、1% low、camera-to-GPU-completion分别报告；GPU完成是延迟代理，不冒充显示延迟。独立trace记录浏览器DrawFrame/DirectComposition Present，含采集开销，单独解释。

GPU阶段采用单提交完成协议和timestamp，核验gpuFrameId，不能用异步重复timestamp当当帧数据。资源/取消/失效代际/候选首帧/截图/排序回归和真实SDK消费仍需通过。至少覆盖小、中、391万点；最大2248万点应作为容量与GPU瓶颈补测，不保证所有电脑120FPS。

## 最终结论

本轮保留默认双帧在途提交和投影阶段去除冗余 ellipse 清零。稳定排序仍为 4-bit radix，完整点数、float32、SH3 不变。RTX 3080 / Edge 154.0.4258.53 / 1920×1080 / 120 Hz，同日同协议391万点模型提交速率中位数从79.22提升到119.85次/秒，约51.3%。这不代表 GPU 算力提升51%，而是解除 CPU/GPU 完成门控造成的气泡。

|391万点最终补测，三轮中位|depth1|depth2|
|---|---:|---:|
|提交速率，次/秒|79.22|119.85|
|相机输入至 GPU 完成 p50，ms|16.6|16.7|
|相机输入至 GPU 完成 p95，ms|17.9|17.0|
|提交间隔 p95，ms|16.8|8.5|

depth3早期三轮约119.9，无额外收益，故默认2。上述延迟使用OCR修正元数据后的 `latency-final-depth1/2/results.json`，是GPU完成代理，不能解释成物理鼠标至屏幕延迟。

## Web 与 Spark 实际连续浏览

同相机轨迹，Spark 2.3.1 正常 autoUpdate，不在每帧调用 gl.finish/fence。10秒预热、20秒×3，以下是三轮提交速率中位数，非加载基准。

|模型规模/格式|Web，次/秒|Spark，次/秒|
|---|---:|---:|
|170,799 / PLY|120.0|120.0|
|804,758 / PLY|119.9|120.0|
|804,758 / SPZ|119.9|120.0|
|tumu_v1 / PLY|约120.0|120.0|
|tumu_v1 / SPZ|约119.9|120.0|
|3,914,609 / PLY|119.85|118.15|
|22,480,361 / SPZ|28.65|37.58|

391万点提交间隔 p95 为8.5/18.6ms，submission 1% low为101.05/47.72次每秒。最大模型Web仍慢于Spark；其p95提交间隔41.7/94.8ms，平均速率与尾部间隔应同时解释。帧提交并不等于显示完成；不能据此声称所有规模浏览都领先。

独立普通磁盘profile的CDP trace：depth1 Web Present 90.00/s、p95 16.958ms；depth2 Web 119.85/s、p95 8.639ms；Spark 118.86/s、p95 8.492ms。trace含采集开销、采样轮数为1，与无trace结果分开，Present也不是光学scanout。

`paired-final`、`large-final` 的FPS/interval有效，后续修正了RPC代际、初始pose timestamp和两引擎age采样位置，因此这些早期文件的age/sort完成字段不用于最终延迟结论。原始失败和原始记录均保留，不覆盖为成功。

## GPU 阶段与撤回实验

timestamp阶段基准：30帧预热、120帧×3、核验gpuFrameId，单提交完成协议。各阶段三轮p50中位数独立计算，不应直接相加等于总帧分位数。

|点数/方案|投影 ms|排序 ms|绘制 ms|总 GPU ms|
|---|---:|---:|---:|---:|
|3,914,609 原基线|1.901|1.507|2.818|6.423|
|3,914,609 去清零|1.638|1.507|3.015|6.291|
|22,480,361 原基线|11.076|8.782|16.187|35.979|
|22,480,361 去清零|9.765|8.782|16.122|34.603|

最大模型投影下降约11.8%，GPU总帧下降约3.8%。剔除项 key 为0xffffffff，稳定排序后位于有效key之后，drawIndirect只读取有效计数，因而无须初始化剔除ellipse。真实GPU回归覆盖“可见后全剔除”，避免沿用上一帧数据。

|实验|结果/处置|
|---|---|
|合并40个radix compute pass|排序无稳定收益，撤回|
|工作组local atomic可见计数|无稳定额外收益，撤回|
|按opacity cutoff收紧quad|像素最大差异2灰阶且无GPU改善，撤回|
|6-bit radix|正确性通过；最大模型排序8.78→12.45ms，总GPU34.60→38.34ms，撤回|
|移入OffscreenCanvas Worker|主线程提交约0.1–0.2ms，收益未证明；未改变Canvas所有权协议|

后续应优先研究大模型排序带宽、投影/混合成本与可见集压缩；异步旧排序、LoD、减点、降SH需独立质量与延迟契约，不应混入本轮完整质量性能结论。

## 完整加载、资源与审查

普通磁盘profile最终38/38（21PLY、17SPZ）完整加载，最大22,480,361点。最大PLY/SPZ约33.56/27.30秒，实际GPU准入6,575,533,896字节、磁盘backing5,305,370,624字节、CPU resident 0（不等于进程RAM为0）。12次load/cancel/recovery stress通过。100次生命周期、5分钟连续运动稳定性测试通过，dispose后设备、buffer、context、worker计数归零。

私密browser context仍偶发OPFS文件截短，WASM view长度正确；普通profile38模型通过，纯OPFS大文件多种写入变体通过。新增rebase前/后长度校验和带偏移诊断，不声称已修复私密浏览根因，不以无限重试掩盖。其他电脑须满足WebGPU、存储、内存与设备限制，无法保证资源不足时任意模型完整加载。

OCR三轮完成，最后5文件0 findings（session 6b149f80-a365-4472-ad6d-1e19d275593d）。修正相机age采样、Spark排序RPC代际、longtask排空、来源采样时机、独立绘制reference及OPFS full-write/cleanup。低收益实验代码已撤回。

新增模板所需Y反射与导航接口单独测试：canonical相机不变，反射视图行与相对eye，不复制场景也不增加CSS镜像；真实GPU与显式反射位置/椭球旋转的结果最大像素差0。默认无反射的渲染路径不变。47 unit、18 contracts、GPU排序/图像、集成及安装包React/Vue消费测试构成发布验证。

## 复现与证据

工具位于 `tools/interactive-benchmark.mjs`、`tools/frame-stage-benchmark.mjs`、`tools/summarize-parallel.mjs`、`tools/summarize-trace.mjs`。完整证据含每模型SHA、源码SHA、协议、逐帧数据、失败、截图、trace，位于 `docs/verification/evidence/parallel-2026-10-05/`；大型原始目录不进入Git。精选JSON随提交保留，原始证据另行归档。结果反映测量时源码SHA，新增模板不重新解释历史结果。


### 查看器SDK新增审查闭环

OCR初审5文件4个medium：fly look方向、迟到Pointer Lock、Canvas焦点转移、松开键后意外退出锁定。全部修复；后续复审指出空闲timestamp导致移动首帧跳变，已修复并以16ms恢复移动断言验证。最终1文件0 findings，session 9d08b22f-554e-4a60-a4d4-84a943ed4f21。最终47 unit通过。审查原始文本随精选证据保留。
