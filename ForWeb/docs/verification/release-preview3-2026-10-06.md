# Web preview.3 发布记录

目标：原生 v0.2.2 Release 的新版 SDK 与 React/Vue 源码模板。

## 默认特性与迁移

- `decoder` 默认 auto/4，安全上下文、隔离、规模及预算允许时使用 pthreads；增强初始化失败保留单线程回退，显式 parallel 仍报告错误。
- `sorting` 默认 adaptive；保持当前投影、SH、裁剪和全部点，在有界小位移期间复用完整排列。需要旧行为时选择 strict/single。
- 模板固定消费包内相同 vendor SDK，使用公开 options/snapshot；Vite dev/preview 配置 COOP/COEP，生产由服务器配置。Y 翻转与 canonical 鼠标输入机制沿用。
- 部署必须递归复制 threaded/、manifest 和 licenses，正确配置 HTTPS、mjs/wasm MIME 与跨源资源。

[SDK 指南](../SDK-guide.md)、[迁移](../SDK-migration-preview3.md)、
[React 接入](../React-integration.md)、[Vue 接入](../Vue-integration.md) 均已更新。

## 已完成证据与停止范围

用户明确要求停止实测并直接完成剩余工作。已停止基准和本任务开发服务；
此后不运行新的单元、浏览器、模型或性能测试。模板执行依赖更新、格式化和
TypeScript/生产构建；它们是构建检查，不冒称新 SDK 下的模板运行验收。

此前 SDK 已通过 57 单元、22 契约、GPU/图像/引擎集成、独立安装包的
SSR/React StrictMode/Vue/实际 pthreads/默认 adaptive/卸载检查，
38×single/auto 共 76 次完整加载与旧 count/SH/bounds/RGBA 基线相同，
16 能力/故障场景，100 生命周期和三分钟持续交互。

修正年龄归属后的 adaptive 三模型完整，strict 重测仅一模型，complete=false；
不完整样本不用于最终性能结论。先前同协议 strict/Spark 作为标明来源的参考，
本机最大模型 adaptive 32.55 次提交/秒（先前 strict 28.95、Spark 31.26），
1% low 21.99（先前 strict 23.55），不宣称稳定性全指标提升或物理显示 FPS。
[详细证据与限制](adaptive-sorting-2026-10-06.md)。

OCR 的 SDK 整合审查及处置见 [记录](evidence/adaptive-2026-10-06/ocr-disposition.md)。

## 资产与来源

SDK 源码提交：`372401c3f4abf3a5a5eb095d4a799c406c5c5c6c`。
模板源码提交以各源码包内部 MANIFEST.json 为准；相同 tarball 字节及
所有归档文件按内部清单核验。Release 不上传独立哈希文件；包内清单保留。

- Native3DGS-SDK-0.2.2-Web-WebGPU-preview.3.zip
- Native3DGS-Template-0.2.2-Web-React-preview.3.zip
- Native3DGS-Template-0.2.2-Web-Vue-preview.3.zip

上传新版并核验实际下载后，只删除旧 Web SDK/React/Vue ZIP 与旧 sidecars。
Windows/Android/Cloud 原生包和原生 tag 保留。未发布 npm，不自动合并 PR。

## 发布结果

三个 ZIP 已上传并实际下载核验：与本地产物逐字节一致，所有内部清单与
两模板 vendor SDK 相同。已删除 8 个旧 Web ZIP/sidecar，保留其余
6 个原生平台资产。模板源码为 `7746e81a9f5d5b6b1d919170ad893d128999324f`。
[发布核验](evidence/adaptive-2026-10-06/release-publication.json)。
