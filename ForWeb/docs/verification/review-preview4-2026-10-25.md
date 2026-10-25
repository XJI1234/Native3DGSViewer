# preview.4 核心与 SDK 审查

审查覆盖实际 ForWeb 全部核心源码、C++/WASM、公开 SDK、React/Vue 适配器与模板，并检查共享 normalize 实现。采用手工合同/所有权检查、open-code-review 1.13.3 全源码审查（45 项，12 条反馈）及最终增量复审；逐条依据见 [source-review.json](evidence/spz-v4-2026-10-25/source-review.json)。工具反馈经复现和代码核验处理，未直接照单修改。

| 范围 | 检查重点 | 本轮处理 |
| --- | --- | --- |
| 输入/WASM | uint64 TOC、窗口、帧尾、checksum、数值有限性、线程状态 | v4 有界解码；保留 vendor unpack；非有限元数据拒绝；验证容纳 float32 局部坐标舍入 |
| 资源/存储 | CPU 准入、旧场景驻留、窗口/栈/I/O、句柄释放 | 64点对齐批次收缩；OPFS finally 关闭；失败删除可重试且底层不重叠 |
| Engine | async 初始化、取消、替换、恢复、dispose、迟到结果 | limits/URL 在 await 前固定；恢复清理串行；重复 GPU dispose 共用完成 promise；Canvas 独占 |
| 公共 SDK | 可选字段、只读快照、SSR、错误合同 | 新诊断增量兼容；提交后发布，失败保留，关闭清空；安装包消费验证 |
| 适配器/模板 | host 重绑、StrictMode、DPR、错误布局、完整资产 | 极大像素比输入有界；错误栏独立行；同步 .4 SDK 与诊断 |

另外复核三条误报/既定合同：React RefObject 仅用于固定 host，动态 host 使用已文档化的 HTMLElement callback-ref；审查快照没有复制构建配置，真实工程构建正常；pthread decoder 状态使用 GS_DECODER_LOCAL/thread_local，非共享全局。abortable 的注释已明确为停止等待，调用方负责底层取消和迟到资源清理。

手工新增发现：初始化只浅拷贝 limits 和 URL 时，宿主在设备申请等待期间可改变已经验证的预算与资产。先写失败测试复现 cpuBytes 从64MiB变成0，再在首个 await 前固定值；错误 URL 提前拒绝，设备创建后异常仍释放。诊断快照的冻结、失败保留和关闭清空有专门测试。

最终验证：67 个 Web 单元测试、24 个 WASM/守护契约测试、类型/格式/模块边界、GPU 稳定排序与 SH0–3 图像、真实事务/恢复/独占 Canvas、独立 tgz 的 SSR/React StrictMode/Vue/卸载。Windows Release 全解构建成功，ctest 的 ModelIo/RenderCore/Joint/Engine/InstalledSDK 五组全部通过；共享 normalize 大跨度和无穷元数据回归通过。单线程与 pthreads 两套 WASM 重建成功。

最大 v4 的本机完整质量、64MiB 策略预算和普通非隔离网页的成功均有 [算法与验证记录](spz-v4-2026-10-25.md)。这些测试不能证明所有设备没有缺陷；实际 GPU 内存/OPFS、其他浏览器/系统和物理低配机仍须独立验收。历史长稳数据不作为本次新跑结果。

最终增量复审覆盖33项并返回6条反馈，见 [final-review.json](evidence/spz-v4-2026-10-25/final-review.json)。实际修复包含：将旧 Renderer 的 cleanup 放在重建重试循环前只等待一次；检查 double 差值/容差的中间溢出；恢复创建传递取消信号并在发布 Canvas 所有权前再次检查。SH0 夹具已过滤空流，单 Worker 的 Zstd ABI 无跨任务共享，均以实际调用链与通过的契约验证。模板格式化由最终构建门禁执行。
