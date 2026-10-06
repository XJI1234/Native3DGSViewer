# preview.3 OCR 审查处置

2026-10-06，使用 `open-code-review` / `ocr review --audience agent`，背景为 `SPEC-parallel-decoder.md`。排除模型证据和本轮无关文档；完整 CLI 文本保留在本地 `.local/ocr-pthreads*.txt`。用户已授权审查并完善代码。

## 首轮：20 个文件、9 项 medium，无 high/critical

| 发现 | 处置与验证 |
| --- | --- |
| 基线初始化计时漏掉 factory | 计时移到实际初始化，重跑最终 kernel 证据 |
| SPZ 全零夹具无法检测分区错 | 改为不同非零点属性，跨 tile、有符号位置、旋转版本及 fractional bits；契约测试验证 |
| 模型短读 | 读取循环至完整批次，EOF 拒绝 |
| 浏览器测试证据写入失败阻止收尾（两处） | nested finally 确保 browser/profile 清理 |
| 无 single 对照或重复无效配置仍运行 | 配置必须包含 single 且唯一合法 |
| 精确 CPU fallback 初始化计时漏记 | 重建/重新探测耗时加入 initializationMs |
| pageerror 仅记录不失败 | 有 pageerror 非零退出 |
| runs=0 可形成假成功 | runs 必须正安全整数 |

## 复查：23 个文件、6 项 medium、1 项 low，无 high/critical

| 发现 | 处置与验证 |
| --- | --- |
| 资产哈希读取失败会泄漏新启动浏览器 | 将资产读取移到 browser 启动之前 |
| 输出只有文件名或 Windows 分隔符时报错 | 使用 node:path.dirname |
| 预期模型 SHA 被当作服务输入 SHA（两个工具） | 新增流式服务输入核验；后续测试在计时外 preflight。已完成历史测试另附结束后实物核验，不伪称当时有 preflight |
| 增强 decoder.mjs 的 import 不受 watchdog 约束 | import 与 factory 统一有界初始化；新增 mjs 挂起回退测试，迟到 import 不再启动 factory |
| baseline JS 解码阶段与 native task-max 计时范围不同 | 明示各行 scopes，报告仅用完整 batch wall 计算跨版本收益；不伪造对等纯解码阶段 |
| nested ternary（low） | 初始化分支改为明确 if/else |

## 第三轮：26 个文件、2 项 medium、1 项 low，无 high/critical

| 发现 | 处置与验证 |
| --- | --- |
| 消费测试资源来自仓库 dist，无法验证 tgz | 改为从安装后的 node_modules/@native3dgs/web/dist/assets 复制、核验包内 manifest，实际启动增强解码 |
| 对照没有核验服务器提供的 Spark | 解析 Vite 实际 import，记录优化模块及 source map SHA；原始 sourcesContent SHA 必须与固定 Spark module 匹配。该源码含嵌入的解码 Worker/WASM；不同或不可验证的服务拒绝 |
| nested ternary（low） | 配置映射改为 if/else |

## 第四轮：26 个文件、4 项 medium，无 high/critical

| 发现 | 处置与验证 |
| --- | --- |
| 消费目录合并可能保留上次资产 | 每次创建新的消费 workspace，包内资产不会由旧副本补齐 |
| rebase 后输出未在性能工具比较 | 使用固定非零偏移，计时外校验所有配置/重复的 rebase SHA；另有独立契约字节一致性测试 |
| input copy 未单列 | baseline/current 使用相同 malloc+HEAPU8.set 边界单列 inputCopyMs，完整墙钟仍含 copy/dispatch；重新生成 verified kernel 证据 |
| kernel 只记录 baseline mjs，不足以重现 | 加入 baseline/current/threaded mjs+wasm SHA，输入批次 SHA；完整模型与 manifest SHA 核验 |

最终复查状态及全套回归见 [验收报告](../../pthreads-2026-10-06.md)。优化保留以实际收益和完整输出为准，不以审查器的建议替代性能或正确性证据。
