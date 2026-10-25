# preview.3 → preview.4

包版本 `0.2.2-preview.4`；现有 createEngine、open、capture、cancel、closeScene、
recover、React hook 和 Vue composable 的调用方式保持。默认仍为 adaptive 排序、
auto/最多4线程。新增能力是完整质量的 SPZ v4 有界加载，以及随预算收缩批次。

升级本地 tgz 依赖和锁文件，清除旧安装包缓存后重新构建。递归替换全部
`gs-assets/`，包括 decoder.worker.js、decoder.mjs/wasm、threaded/、manifest
和 licenses；不能只换 Worker 或只换 wasm。使用带版本的资产目录或原子部署，
清除 CDN/Service Worker 旧缓存。

普通 HTTPS 网站可单线程解码完整 v4；COOP/COEP 仍只决定可选 pthreads。
OPFS 需要存储配额：URL 输入、解压属性、packed backing 在加载时暂时共存。
成功只保留 backing；closeScene/dispose 等待清理。GPU 完整质量需求没有降低。

可选初始化预算示例，普通站点同样适用：

```ts
const result = await createEngine({
    canvas,
    assets: { baseUrl: new URL('/gs-assets/', location.href),
        workerUrl: new URL('/gs-assets/decoder.worker.js', location.href) },
    decoder: { mode: 'auto', threads: 4 },
    limits: { cpuBytes: 64 * 2 ** 20 },
});
```

该 64 MiB 是保守引擎分配策略，不是进程 RSS/机器 RAM 限制。auto 自动使用较小
批次/单线程，保留全部点和 SH。本机最大 v4 已验证；其他 GPU/存储仍需实测。
options 在初始化时固定，运行中改变调用方对象不是动态调参接口。

`snapshot.decoder` 增加可选只读字段，旧调用方可以忽略：

| 字段 | 含义 |
| --- | --- |
| batchPoints | 实际批次容量；最后一批可能更小 |
| estimatedCpuPeakBytes | 本次准入的保守引擎 CPU 估算，含保留场景 |
| wasmHeapBytes | 实测 WASM 线性堆高水位；不是整个进程内存 |
| zstdWindowBytes | 最大 Zstd 窗口；非 Zstd 流为0 |

字段缺失表示此路径未提供，不能显示为0。数据跟随提交场景；失败加载保留旧
场景诊断，关闭清空，恢复沿用该场景数据。模板展示 WASM 内存和批次，使用公开
字段，避免访问 active/renderer/scene。

模型签名和内容校验仍严格：v4 TOC、窗口、checksum（如果存在）、长度和帧结束
任一失败都不会发布部分模型。ResourceLimit 仍可能来自 GPU、存储、窗口或最小
批次准入。详细分析和实测边界见[验证记录](verification/spz-v4-2026-10-25.md)。

初始化提前验证并固定 limits 与资产 URL；错误 URL 在申请 GPU 设备前返回 InvalidInput。同一 Canvas 只允许一个活动引擎，多个视图应各自创建 Canvas。恢复超时可先返回错误，但后续恢复等待之前的清理；dispose 等待实际清理完成后才发布 Stopped。DOM 输入使用与视口一致的有效像素比例。
