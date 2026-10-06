# 计时外收尾诊断

停止 Vite 服务时读取到其积累的浏览器控制台日志。以下为工具输出的人工摘录，不是新增实测，也不是完整服务器日志文件。页面计时窗口内的 errors / warnings 以各轮原始 JSON 为准。

- 多个 Spark 页面初始化显示 `THREE.WebGLProgram: Program Info Log: (451,6-34): warning X3203: signed/unsigned mismatch, unsigned assumed`。原始 JSON warnings 保留，无编译失败。
- 18:05:11、18:06:00、18:20:38 等 Spark 页面的采样结束 / dispose 时显示 `Unhandled rejection Error: Worker terminate`，栈位于 `SplatWorker.dispose -> SparkRenderer.dispose -> apps/benchmark.ts:126`。
- 18:21:27 最后一轮 Spark SPZ 采样结束 / dispose 时显示 `Unhandled rejection Error: No target`，栈位于 `SparkRenderer.readbackDepth -> SparkRenderer.driveSort`。该轮交互开始为 18:21:02（先预热 5 秒，再采样 20 秒），两者时间边界相符。

服务器时间为 Asia/Shanghai，原始结果中的 Started 字段为 UTC，相差 8 小时。该工具在记录完交互结果后将 `disposing=true`，忽略 dispose 阶段 pageerror；对这些消息保留说明，而不将其伪称为零生命周期错误。此阶段未纳入图中的加载或连续浏览窗口，SDK / Spark 已按原有工具释放并关闭独立浏览器进程。没有改动 Spark 依赖或在本轮采样中插入额外的 worker 等待。
