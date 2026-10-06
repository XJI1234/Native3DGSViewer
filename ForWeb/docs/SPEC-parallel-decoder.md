# 可回退的并行 WASM 解码（preview.3）

2026-10-06。用户已授权实现、完整测试、性能比较及迁移文档。沿用 model-io → engine → adapters；不改 native SDK、渲染数据 ABI、点数、SH、精度或相机交互。

## 契约与实现顺序

1. 保存 preview.2 单线程解码资产与阶段基线，原始模型使用既有 manifest/hash。
2. 单线程 `decoder.mjs/wasm` 保留；新增 `threaded/decoder.mjs/wasm` 及 Emscripten pthread 启动资产。依赖版本保持固定。构建编译并行 `--parallel` 不属于运行时线程数。
3. `EngineOptions.decoder?: { mode?: 'auto' | 'single' | 'parallel'; threads?: number }` 是兼容性增量，threads 为 1–8 整数上限；默认 auto。single 强制原路径；auto 在能力、模型大小和资源预算允许时尝试增强版，初始化不可用时清理并回退；parallel 用于显式验收，不隐藏增强版失败。不存在跨源隔离时 auto 不请求增强资产。
4. 模型事务、Source、Result、ErrorCode、取消/超时/释放、React hook/Vue composable 的已有签名保持；新选项在初始化时复制固定。新增只读解码诊断标明实际 backend/threads/fallback；与场景同步替换、关闭、恢复，不发布过期任务。
5. 分块 PLY 和解压后的 legacy SPZ 以 64 点 tile 对齐并行解码、规范化、验证和打包。每任务独立状态；合并 bounds/maxScale；输出范围不重叠并保持原点顺序。gzip 流保留校验和串行解压，不声称任意切割 DEFLATE 能并行。SPZ v4 原路径继续支持并明确诊断。
6. 全局 rebase 并行作为独立消融；存储写入仍由拥有同步句柄的协调 Worker 顺序完成。先测计算与读写份额，再实验流水线、缓冲复用；无稳定收益的实验退回基线并记录。
7. CPU admission 计入线程栈、额外模块/线程 scratch、批次输出、JS 拷贝、allocator headroom；不整模型复制给每线程。线程创建/失败/取消/成功均清理；增强版不能突破 wasm32/1GiB 上限或旧限额。

## 验证与性能门槛

- 运行 `pnpm run build:wasm`、`pnpm run typecheck`、`pnpm run test:unit`、`pnpm run test:contracts`、`pnpm run test:gpu`、`pnpm run test:integration`、`pnpm run build`、`pnpm run test:sdk`。
- 验证线程数非法值、非隔离回退、增强资产初始化失败回退、显式 parallel 错误、小模型/低预算、末尾 tile、重排 PLY、SH0–3、SPZ1–4、坏属性/压缩校验、超时、取消、最新请求、旧场景保留和 OPFS/子 Worker 清理。
- 计算消融：同一输入预载，1/2/4/8 线程，独立报告初始化、输入拷贝、decode+normalize、验证+pack、rebase；批次及结果等价校验。端到端另计 download/inflate/OPFS/upload/first frame；不将含 I/O 的阶段称为纯 kernel。
- 38 个模型单线程和增强版完整加载，核验 count/degree，关闭后存储清理。普通隔离浏览器 profile；不把私密模式的已知存储限制隐藏。
- SparkJS 固定既有版本和同等完整点/SH 策略；小、中、大、最大 PLY/SPZ 交错重复测量，报告中位数/波动与实测边界，不推算跨设备倍数。渲染阶段复用既有准确计时协议。
- 比较同机单线程基线，只有收益超出重复波动才将优化纳入 auto；无收益可保留显式实验选项但必须解释选择理由，不能称自动优化成功。
- API/SSR/React/Vue 旧调用不改仍能构建运行；新包递归资产拷贝和 manifest 校验，补充部署与 preview.2 → preview.3 迁移。最终 OCR 审查并修复重要问题。

## 文件与边界

实现位于 `native/`、`src/model-io/`、`src/engine/`；测试为 `tests/unit/`、`tests/contracts/`、`tools/*parallel*`；证据为 `docs/verification/evidence/`，报告和迁移文档位于 `docs/`。C++20 四空格/snake_case，TS strict 四空格/camelCase。保持第三方源码固定，不改 vendor 数学实现；不发布测试模型或机器凭据。当前部署文档改动保留，无关未追踪文件不纳入。

## 测量口径

内部 decodeTimings.readMs 记录批次读取从发起至完成的墙钟总和（包括 JS 调度与属性拼接）；readWaitMs 记录消费端 await 的阻塞总和。预取可与计算/写入重叠，不能将两者或各阶段直接相加。历史证据的 readMs 为旧 await-only 口径，必须标明，不据此声称磁盘本身变快。
