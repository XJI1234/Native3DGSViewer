# 小模型CUDA排序回退调研证据

2026-10-27；本轮仅调查，没有新增GPU性能测试或引擎改动。

[分析报告](../../../docs/windows-cuda-small-model-sort-analysis-2026-10-27.md)。

- `stage-comparison.json`：从2026-10-27已完成的42文件结果抽取排序、CPU提交与策略工作组数。
- `source-fingerprints.json`：引擎及CUDA13.3/CUB3.3.3本机头文件的路径和SHA-256。
- `official-sources.json`、`source-followup.json`、`source-additional.json`：官方站点及论文检索结果，保留404及超时，未将失败获取作为已读来源。
- `.txt`为读取的参考文本。CUDA13.3互操作内容优先于较早13.0归档；Graph API在线为13.4，已另外检查本机13.3 runtime头存在external semaphore graph节点及更新声明，但未实施拟议方案。

Graph/i32、纯排序event、interop-noop等数字来自仓库2026-10-17历史证据；与本次分段数据分别标识，不跨组混算新结论。
