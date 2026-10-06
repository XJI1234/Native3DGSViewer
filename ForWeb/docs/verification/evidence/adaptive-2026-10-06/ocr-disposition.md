# preview.3 OCR 审查处置

使用 open-code-review 技能与 OCR CLI，
`ocr review --audience agent --background-file ... --concurrency 3`。
模型证据和本轮无关根文档排除；原始 CLI 输出保留于 .local。
用户已授权修正并提交发布，不另设确认门槛。

## 排序整合首轮（31 文件）

9 项：5 medium、4 low，无 high/critical。

| 发现 | 处置 |
| --- | --- |
| 消费目录积累 | 外层 finally 清理新临时目录；显式调试开关可保留；独立消费回归通过 |
| GPU 测试墙钟断言脆弱 | 注入受控策略时钟；三个布局正确性测试通过 |
| 页面 route 无法覆盖专用 Worker | context.route，要求注入计数非零；16 场景完整通过 |
| 输出目录硬编码 | 使用 dirname(output) |
| 未断言 single 后端 | single case 必须实为 single；38×2 通过 |
| Native 排序姿态年龄归属错误 | 保留 stats.sorted 时的输入时间，复用帧从该时间计算；重新实测，不将旧估计混为新版口径 |
| kernel malloc 未核验 | malloc 非零断言，finally 释放；算术路径不变，原始完整输出证据保留 |
| 服务来源缺少策略模块 | 增加 sorting-policy 与 decoder-options 的实际服务核验 |
| persistent soak 视口未生效 | 显式设置 1280×900，证据记录；100 循环+3 分钟新默认 soak 通过 |

## 默认整合复查（33 文件）

2 项：1 high、1 medium。

- high：审查器认为 persistent context.browser() 必为 null。当前固定 Playwright
  版本已实际跑完 16 场景，因而该必然失败判断不符合实测。仍采纳更直接的
  context.newCDPSession(page)，并把 page/CDP 创建移入清理作用域，减少假设。修后 16 个能力/故障场景再次全部通过。
- medium：prefetch 的 readMs 实为等待时间。最终增加 readWaitMs，readMs 移到
  readBatch 发起至完成边界；旧报告行标为 await-only 历史口径，不能推断
  存储本身加速。PLY/legacy SPZ/最大 SPZ 重新验证全部通过；新口径真实读取/等待分别记录。

核验修正以真实测试和来源归属为准，未降低完整点数、SH、float32 或取消/
恢复/释放契约。此前 pthreads 四轮审查见同目录下
[解码审查处置](../pthreads-2026-10-06/ocr-disposition.md)。
