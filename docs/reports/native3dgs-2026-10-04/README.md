# 原生 3DGS 引擎报告交付说明

报告日期：2026-10-04；源码参考：`d088902bcc464ef7bea1fca857f2b7cd3f10836c`。

主报告为 33 页横向 A4 PDF，位于仓库 `output/pdf/native3dgs-engine-report-2026-10-04.pdf`。本目录的 `report.md` 为可编辑正文，`figures/` 提供 11 组独立 PNG/SVG 图表。四个平台分别绘图；SVG 文字使用 Microsoft YaHei，换机编辑时可安装该字体或替换为中文字体。

`evidence/` 提供 Windows 7 模型、21 个独立进程的日志与阶段统计，以及 Web/Spark 6 模型、12 组各 360 帧的原始数据和 18 个固定视角的图像比较。三个 CSV 为可直接导入表格软件的摘要，其中 38 模型加载阶段来自已标明日期的发布验证，没有在本轮重新测量。`report-manifest.json` 记录正文、图表和 22 项证据的 SHA-256。

重要口径：Windows 已有 Release 二进制没有在本轮重新构建；Windows 固定相机 GPU 阶段与 Spark 运动相机 WebGL query 不同。Spark 独立解码、投影和纯 Worker/WASM 排序未计时，报告明确列为未测。Web/Spark 单次首载未清缓存。完成帧墙钟包含同步与 timer 观察，不能换算成真实显示 FPS。新增 391 万点样本的前景质量差异已在正文披露。

## ZIP 布局和校验

ZIP 保留仓库相对目录，打开 `output/pdf/` 中的 PDF 或本目录 `report.md`。包内含 22 项引用源文件的快照，使 Markdown 的本地证据链接可用，并提供相关基准驱动；它不是完整源码仓库或 SDK。模型、二进制、依赖、原始 RGBA 中间文件和页面预览不分发。`bundle-checksums.json` 记录包内每个交付文件的字节数与 SHA-256。

在具备 pypdf 的 Python 环境中，从仓库根目录运行 `python docs/reports/native3dgs-2026-10-04/package_report.py` 可校验证据、PDF页数、CSV行数和本地Markdown链接，生成 ZIP，逐项复核其哈希并检查 ZIP 完整性。

## 重建和复测

图表：系统 Python 安装 matplotlib/Pillow 后运行 `figures.py`。PDF：Python 安装 ReportLab/Pillow/pypdf 后运行 `build_report.py`；当前字体路径针对 Windows。生成器读取仓库中的原始证据，不能只使用 ZIP 单独恢复完整构建环境。

Windows：在完整仓库根运行 `measure_windows.py`，按既有二进制重复测量。若需测试修改后的源码，先依仓库构建说明编译，并为新二进制保留独立证据。

Web：在完整仓库 `ForWeb/` 内安装已锁定依赖，设置 `GS_MODEL_ROOT=C:\Users\21544\Desktop\zhishan` 后启动 `pnpm run dev`。另一个终端设置 `GS_BENCH_MODELS=changjin_v1.ply,shengyi_v1.ply,spz/shengyi_v1.spz,tumu_v1.ply,spz/tumu_v1.spz,zhihuizhimen.ply` 与 `GS_BENCH_OUTPUT=../docs/reports/native3dgs-2026-10-04/evidence/web-paired`，运行 `node tools/completed-benchmark.mjs`，再运行 `python tools/image-metrics.py <输出目录>`。原始 RGBA 仅供本地质量重算，ZIP 中的无损 PNG 与指标保留测量结果。GPU 测试顺序执行，期间避免 HMR 或其他 GPU 基准竞争。
