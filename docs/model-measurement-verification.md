# 模型测距验证记录

2026-10-16，分支 `codex/model-measurement`。依据 [测距规格](SPEC-model-measurement.md)，用户已确认可见表面深度估计，并授权任意有效比例尺，不核验 JSON 与模型对应关系。

## 实现与边界

原生引擎支持有界异步 metric JSON 导入、两点表面拾取、源坐标恢复、米制距离、重测/退出/模型替换及迟到结果过滤。WinUI 提供工具栏入口、导入引导、A/B 和裁剪后的辅助叠加线、质量说明、三位小数米值，以及点击/拖动区分。测距关闭时不增加持续 GPU 读回。

拾取复用实际画面的投影、抽样和排序，按点击像素收集最多 16,384 个候选，以可见贡献加权中位视深度还原端点。固定输出缓冲 262,160 字节，读回缓冲同大小；单个未消费操作，fence 完成后复用。过密候选返回资源上限，不截断出数。分配失败隔离到本次测距；设备移除保持原恢复流程。

JSON 采用 Windows 系统解析器，无新第三方依赖。保留 1 MiB/64 层上限、重复键拒绝、正有限比例尺和相似矩阵检查。model/provenance 不做对应校验，不读取其中外部路径。PLY 使用实际加载坐标选项，SPZ 使用 RDF 等价源坐标。SDK 和安装器的 shader 清单包含 pick_reset/pick_collect。

## 环境与命令

Windows build 26200、NVIDIA GeForce RTX 3080、16 逻辑处理器、约 32 GiB RAM；VS 2026 / MSVC 14.50、Release x64。GPU 测试使用硬件 D3D12；Session 调试层检查未报告新增错误。

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' .\Native3DGSViewer.sln /restore /m /p:Configuration=Release /p:Platform=x64
ctest --test-dir out/cmake -C Release --output-on-failure
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe' .\out\Release\Native3DGSViewer.Tests.dll /Platform:x64
git diff --check
```

Release 解决方案通过，0 错误、2 条既有 NuGet restore 警告；日志 `out/measurement-solution-build.log`。新增线段裁剪测试先因缺少 project_segment 编译失败（`out/measurement-clip-red.log`），随后实现并通过。早期 parser/pick/session 的失败及修复日志保留在 out，未纳入版本控制。

完整 CTest **5/5 通过**，总计 101.35 秒：

| 组 | 测试数/行为 | 耗时 |
| --- | --- | --- |
| ModelIo | 21 项既有输入/语料/隔离测试 | 6.72 s |
| RenderCore | 45 项，含 5 项 RenderPick | 34.41 s |
| ModelRenderIntegration | 1 项真实语料联合测试 | 6.34 s |
| Engine | 23 项，含 4 项 EngineMetric 和测距会话 | 24.40 s |
| InstalledSDK | 临时安装、重定位、独立构建和双点测距 | 29.46 s |

日志 `out/measurement-ctest.log`、`out/cmake/Testing/Temporary/LastTest.log`。完整 VSTest 原生桥 `RunCompleteGoogleTestSuite` **通过**，约 1 分 28 秒（桥本身报告 1 项，内部运行完整 GoogleTest 套件）；日志 `out/measurement-vstest.log`。`git diff --check` 通过。

新增检查覆盖任意/缺失 model、损坏/重复及转义重复键、层数/大小限制、质量不可用、比例尺冲突、反射、逆矩阵、非有限/溢出距离和取消读取；GPU 验证透明前景/不透明背景、空像素、候选溢出、实际点抽样、过期相机；CPU 验证偏轴射线、六平面线段裁剪及 1e9 世界原点下八种镜像组合。会话验证第三点拒绝、清除/退出、失败导入和模型加载回退、成功替换清空、取消导入，以及连续 **100 次双点重测、20 次模式进出**。

安装 SDK 消费实际输出：`MEASUREMENT OK meters=3.94463 scale=2 estimated=1 elapsed_ms=1166`，标定含 deliberately-unrelated 指纹。数值仅证明同一对拾取点按指定比例尺计算，不代表该语料的真实长度。

## 真实数据烟雾

从 out/sdk 独立配置 `out/measurement-sdk-consumer` 后运行：

```powershell
.\out\measurement-sdk-consumer\Release\sdk-host.exe C:\Users\21544\workspace\3DGS\exports\zhishan_metric.ply C:\Users\21544\workspace\3DGS\exports\zhishan_metric.metric.json
```

源 PLY：491,221,082 字节、2,081,439 点，示例声明 SHA256 `40edf7815cc9199801c1f7e0450d7e374824890182a4654d5edf603ad685776f`（本功能未重算/核验此值）。JSON 比例尺 `82.8007956818355 m/模型单位`，estimated / not_measured。640×480 合成交换链，默认 fit 相机与完整质量；示例在固定像素网格顺序搜索两次命中。

实际输出：

```text
SDK OK frames=82 active=1 splats=2081439
MEASUREMENT OK meters=107.223 scale=82.8008 estimated=1 elapsed_ms=1017
```

日志 `out/measurement-real-sample.log`。1017 ms 是包含空白查询、网格搜索和两次命中的整段用时，不是单次拾取延迟基准。结果与端点源坐标范数乘比例尺一致，但无独立实测长度，**未证明 107.223 m 的真实几何精度**。

## 尚未验收

- 桌面可见点击、选择器、拖动、Esc 和截图：自动化 helper 将构建窗口归属到同名已安装应用，重新初始化和重选后仍报 `window id 527832 no longer belongs to …; current owner is …`。停止 UI 输入；没有把旧版窗口截图算成本次证据。新 WinUI 代码已通过编译/链接，但人工交互尚待验收。
- 100%/150%/200% DPI 的可见对齐、真实大模型镜像标记对齐；八种组合目前是纯数学测试。
- 100 次重测已完成行为检查，尚无长期 GPU/进程内存曲线，不能声称已证明零泄漏。分配/超时故障注入及测距中设备恢复组合仍可补充。
- AMD/Intel、独立长度误差、真实拾取延迟分布、Spark 图像/整机性能门槛保持未验收。安装包已于 2026-10-17 构建，未执行安装。

代码审阅检查了线程/锁归属、fence 读回、失败导入事务、取消资格、模型替换、镜像补偿、深度与射线长度、shader/宿主布局及 SDK 部署。已修正资源分配影响普通 surface 和线段穿越裁剪面的问题。

## 2026-10-17 Windows 安装包

复用上述已通过完整构建和测试的 Release 输出，运行：

```powershell
.\packaging\build-installer.ps1 -SkipBuild
```

Inno Setup 编译通过，产物 `out/installer/Native3DGSViewer-0.2.2-Windows-x64-Setup.exe`，30,641,283 字节。SHA256：`49bf4af294b5e2b951fba90f6cd2e47c9ed1a37a1e82362149af994d00899a75`。日志 `out/measurement-installer-build.log`。

安装 staging 内查看器、pick_reset.cso、pick_collect.cso 与 Release 输出逐一 SHA256 一致；未包含 logs 或原生测试桥 DLL。沿用当前安装器版本号 0.2.2，本次仅构建，未安装或发布安装包。
