# 三引擎基准工具代码审查

2026-10-06。使用用户指定的 [open-code-review 技能](https://github.com/alibaba/open-code-review)，OCR 两轮审查均按低 effort、2 个文件 worker 执行，只检查基准工具，排除未相关根文档与测量输出。审查并发是代码文件分析，不是并行 GPU 采样。

首轮会话 `c9644619-975f-473f-a3e3-650ff1f3143c`：5 个文件，3 项 medium，无 high / critical。复查会话 `d143d09f-2fb1-4534-b766-88c784dbfc37`：5 个文件，1 项 medium，无 high / critical。

| 问题 | 修正 | 验证 |
| --- | --- | --- |
| Python assert 在 `-O` 下失效 | 显式 `require` 抛错，输入 hash / 点数 / SH / 时间窗口仍校验 | 错误点数和短采样窗口在优化模式下被拒绝 |
| 原生进程启动或超时未写失败，可能残留旧成功证据 | 启动前写 `complete=false` 并清空旧姿态；进程调用纳入 try/finally；超时保留 stdout/stderr | 缺失可执行文件和模拟超时均使旧成功无效；部分输出保留 |
| 汇总只比模型，未核验相机 / 时间 / frame depth | 校验 native 与 browser 初始姿态、相机文件 hash、5s / 20s / depth2，源文件 hash 跨轮一致 | 全部真实结果在生成图表前统一验证 |
| 浏览器驱动早期失败可残留旧 `results.json` | 启动每轮 Node 前覆盖不完整标记 | 模拟缺失 Node 后旧成功证据无效 |

故障测试命令：

```powershell
python -O -m unittest discover -s bench -p test_measurement_guards.py
```

4 项通过，无 GPU / 模型重跑。另外对完成的 JSON 证据做 4 项计时外篡改注入：错误相机、错误预热时间、错误 frame depth、不同实际服务模块 hash 均被汇总器拒绝，且没有输出 summary；见 [aggregation-guard-verification.json](aggregation-guard-verification.json)。C++ 可见原生宿主已 Release 构建，并以全部 6 模型 × 3 次真实执行验证。加入 ASAN 目标列表后 Release 再构建通过，二进制 hash 与实测时完全一致。Node 工具语法检查通过，Git diff whitespace 检查通过。

成功测量使用的原生 C++ / 二进制以及浏览器应用 / SDK 源码均未因 OCR 改变。对 Python 驱动的修正只影响失败路径和证据完整性；实际测量首版驱动另存 `windows-measured-driver.py` 与 `browser-measured-driver.py`。首版辅助 1% low 使用 floor 取最慢 1%，汇总统一从原生逐帧日志按 ceil 重新计算，与浏览器定义对齐；加载时间、FPS 和主图帧时间没有被修改。

此次只新增基准工具，不改变公共 SDK API、渲染质量、默认特性或 Release 包。后续测量证据以单独提交加入既有 PR，不需要重打包已发布 SDK。
