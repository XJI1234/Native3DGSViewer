# Spec: Windows 原生模型米制测距

状态：用户于 2026-10-16 修订为允许任意比例尺并授权开始执行；能力 ID：`model-measurement`。本次授权覆盖后续计划、任务及实现。

## 已批准的比例尺与拾取约定

取消 JSON 与当前模型的文件名、字节数、SHA256 和来源对应关系校验；允许将任意有效 v1 metric JSON 应用到当前 PLY 或 SPZ。`model` / `provenance` 仅为可选诊断字段，不读取其指向的外部资源。不新增源指纹或 model-io 公共契约。坐标以当前源格式还原：PLY 按实际加载的 RDF/RUB 选项；SPZ 按 RDF 等价原坐标解释用户提供的标定。用户负责选择合适的比例尺。

仍验证 schema、坐标约定、质量状态、正有限比例尺、4×4 相似变换及可选逆矩阵（存在则必须正确）；界面显示“用户指定比例尺，未校验模型对应关系”以及 GPS 估算说明。切换成功后清除当前绑定，防止无意沿用。

拾取实现采用 GPU 已投影椭圆的按需候选收集，最多 16,384 个像素命中候选，读取固定大小约 256 KiB 缓冲；候选溢出明确返回资源上限，禁止截断后出数。CPU 仅对命中候选按合成顺序求贡献、按视深度取加权中位数；不读回完整排序/场景数组。与正常帧使用同一投影、点间隔和质量。此处允许按需候选读回，不引入普通浏览持续读回。

## 1. 目标与范围

用户在 Windows 原生桌面查看器打开模型，进入“测距”模式，按引导导入模型生成时输出的 `.metric.json`，先后点击模型上两个可见位置，得到两个三维端点间的实际直线距离，单位为米。测距计算、标定校验和拾取通过原生引擎/SDK 提供；WinUI 负责文件选择、输入与显示。

这是一个完整的用户能力；标定、拾取和界面是其内部环节，沿用现有模块依赖，不另建能力图。依据 [总技术计划](technical-development-plan.md)、[系统设计](system-technical-design.md)、[model-io](SPEC-model-io.md)、[render-core](SPEC-render-core.md)、[engine/SDK](SPEC-engine-sdk.md) 和 [桌面规格](SPEC-desktop-viewer.md)。

输入格式权威依据为工作区 `../3DGS/scripts/metric_scene/README.md`；真实例为 `../3DGS/exports/zhishan_metric.metric.json`。实现测试需使用小型合成夹具，不将约 491 MB 的外部 PLY 加入版本控制。

首版包含 PLY/SPZ 单模型、单条测量线、A/B 标记、米制结果、清除重测和退出测距。任意有效比例尺均可应用到当前模型；用户负责选择正确比例尺。

范围外：水平距离/高差独立读数、多段折线、面积、角度、多条历史记录、结果导出/持久化、手工输入比例尺、重新运行 GPS 标定、修改/导出模型、多模型和其他平台。首版不会把 GPS 估算包装为测绘精度承诺。

## 2. 已确认需求与交互约定

- 用户已明确要求：进入测距后引导导入比例尺矩阵数据；顺序选取两个点后显示实际距离。
- 用户已确认：采用“点击像素处可见高斯贡献估算深度，再沿点击射线还原三维端点”，并显示端点标记。
- 约定：以米显示三位小数；一次保留一条线；“重新测量”清除两个端点但保留本模型标定；退出模式保留本模型标定、清除端点，再进入时显示已绑定标定并允许重新导入。
- 约定：测距时保留轨道旋转、平移和缩放；暂停自由飞行的持续移动与鼠标锁定，退出后恢复先前浏览模式。
- 约定：新模型成功激活或主动关闭模型时清除测距会话；新模型加载失败/取消时保留旧模型及其测距会话。

## 3. 用户流程与状态

无活动模型时禁用测距。模型 Ready 后可点击工具栏“测距”；若无可用标定，显示说明“请选择此模型生成时导出的 .metric.json”，提供“导入标定”和“退出”。文件选择器筛选 `.json`，入口校验内容，不要求文件名必须以 `.metric.json` 结尾。取消选择返回等待导入，可继续浏览或退出，不作为错误。

| 状态 | 可见内容与动作 | 转移条件 |
|---|---|---|
| Off | 普通浏览 | 点击测距 → AwaitingCalibration 或 AwaitingFirstPoint |
| AwaitingCalibration | 导入引导，禁止选点 | 选择文件 → ValidatingCalibration |
| ValidatingCalibration | 正在校验；有真实字节进度时才显示百分比；可取消 | 成功 → AwaitingFirstPoint；失败/取消 → AwaitingCalibration |
| AwaitingFirstPoint | “请选择起点 A”，显示标定质量 | 成功拾取 → AwaitingSecondPoint |
| AwaitingSecondPoint | A 标记、“请选择终点 B” | 成功拾取 → Complete |
| Complete | A/B、连线、距离；“重新测量” | 重测 → AwaitingFirstPoint |

每次拾取有“正在定位”反馈；同一会话最多一个在途拾取，请求期间阻止再次选点。未命中显示“此处没有可测量的模型表面，请重新选择”，不改变已有端点。Complete 后不因第三次点击悄悄覆盖结果。A/B 重合允许得到 `0.000 m`。

点击定义为主按钮按下/松开在视口内且位移不超过 4 个 DIP；超过则作为拖动浏览，不提交拾取。右键平移和滚轮缩放保持原行为。`Esc` 退出测距，取消在途导入/拾取并清除标记。文件对话框获得焦点时不触发视口快捷键。所有控件提供中文可访问名称，进度、错误和距离可被辅助技术读取。

结果示例：`距离：12.345 m`；精度说明与结果同时可见。`estimated` 或 `validation.status=not_measured` 显示“GPS 估算尺度，未实测验证”；三位小数只是显示格式。仅在文档确实给出 validated 质量及独立验证证据时显示“已实测验证”，不能单凭状态字符串推导精度。质量告警提供简短说明，不展示未经处理的内部字符串。GPS 残差不得显示为长度误差或 ± 精度。

标记和连线从已保存三维端点重投影，随相机、镜像、窗口和 DPI 更新；端点在视锥外/后方时隐藏对应标记，线段按视锥裁剪。首版为辅助叠加线，不承诺模型遮挡处的逐像素隐藏线消除；通过线型/说明避免把叠加线解释为模型表面轮廓。叠加元素不拦截鼠标。

## 4. 标定文件契约与会话归属

### 4.1 schema v1

只接受本地 UTF-8 JSON 对象，大小上限 1 MiB，嵌套深度上限 64；超限及关键字段重复均拒绝。未知普通字段可忽略，保留未来扩展兼容性，但不接受未知 schema、坐标约定或质量状态。

| 字段 | 必须满足 |
|---|---|
| `schema_version` | 整数，等于 1 |
| `model` / `provenance` | 可缺省；不核验名称、字节数、指纹或来源，不打开字段中的路径 |
| `frame.source` / `frame.target` | `ply_local` / `local_enu` |
| `frame.units` | `meters` |
| `frame.matrix_convention` | `row_serialized_column_vector_left_multiply` |
| `quality.status` | 仅 `estimated` / `validated` 可启用 |
| `meters_per_model_unit` | 有限且大于零的数值 |
| `enu_from_ply`、可选 `ply_from_enu` | 4×4 有限数值仿射矩阵；正向为无反射的统一尺度旋转，提供逆矩阵时必须互逆 |

对 `insufficient_data`、`unreliable`、`unsupported_frame`，即使矩阵/比例尺非空也禁用米制测距，显示对应原因；这些状态的合法 null 字段应识别为“标定不可用”，而非误报 JSON 损坏。未知状态保守拒绝。

矩阵末行为 `[0,0,0,1]`，绝对误差不大于 `1e-8`。令正向 3×3 为 M，比例尺为 s，要求 `max_abs(MᵀM / s² - I) <= 1e-5`、`det(M) > 0`，存在逆矩阵时要求双向乘积相对单位阵的最大绝对误差均不大于 `1e-5`。验证中所有中间计算必须有限。不能只读取一个比例尺后忽略不一致的矩阵，也不能重复应用缩放。

`enu_origin`、`provenance`、`report_file`、`report_sha256` 供诊断，不能按其中路径自动读取文件、执行脚本或联网。不要求用户提供 frame/report JSON 或原始照片。

### 4.2 当前模型会话

不检查标定与模型对应关系，也不重新读取/哈希模型。标定只属于当前成功激活的 scene 会话，不能自动继承到新 RequestId。

JSON 解析和矩阵验证在后台执行，支持取消，不阻塞 UI 或持有引擎锁执行文件 I/O。成功只在检查完成且 scene 会话仍有效时一次性发布。首次导入失败保持未标定；重新导入失败保留原有效标定与端点；重新导入成功清空端点。文件选择取消保持原状态。

### 4.3 真例基线

示例声明源文件 `zhishan_metric.ply`，491,221,082 字节，SHA256 为 `40edf7815cc9199801c1f7e0450d7e374824890182a4654d5edf603ad685776f`；质量 `estimated`，验证 `not_measured`，比例尺 `82.8007956818355`。PLY 原始坐标相距 1 单位的两点应计算为该数值的米制距离。此断言是变换计算验收，不能代替真实场景的独立实测长度校验。

## 5. 坐标与距离定义

所有测量保存在 RUB 世界坐标及源坐标，使用 double。PLY 按实际加载选项还原，SPZ 按 RDF 等价源坐标解释。内部 RUB 世界中心为 `centerLocal + worldOrigin`；默认 PLY RDF → RUB 变换为 `(x,y,z) → (x,-y,-z)`，其逆相同；显式 `Coordinates::Rub` 输入则为恒等。必须记录本次加载实际坐标选项，不能根据扩展名推断。

显示镜像由现有相机及奇数轴时的合成画面水平翻转完成。鼠标映射先扣除视口相对根布局偏移，再按实际 RasterizationScale 转换物理像素；奇数轴补偿水平镜像只撤销一次。用与实际绘制一致的相机从点击像素中心构造射线；返回的 RUB 世界端点再应用本次源坐标变换的逆得到 PLY 端点。不要对已经使用镜像相机求得的世界点再次施加显示镜像。

若后续引入实体/父节点变换、居中或模型缩放，需将完整 `viewer_from_ply` 纳入可逆链；不能只除包围盒尺度或忽略非均匀显示缩放。当前 fit/reset 只移动相机，不改变模型尺度。

对原始 PLY 端点 a、b：

```text
distance_m = hypot(a.x-b.x, a.y-b.y, a.z-b.z) * meters_per_model_unit
a_enu = enu_from_ply * [a.x, a.y, a.z, 1]ᵀ
b_enu = enu_from_ply * [b.x, b.y, b.z, 1]ᵀ
```

JSON 矩阵按行序列化，数学为列向量左乘；禁止误当 DirectXMath 的存储/乘法约定直接拷贝。验收需验证 `norm(a_enu-b_enu)` 与比例尺距离一致。平移和 ENU 原点不影响长度。差分、范数和乘法都检查非有限与溢出；无效计算不给出数值结果。高程基准 unknown 不影响直线长度，但不得显示测绘高程。

## 6. 可见表面拾取语义

3DGS 不提供真实网格表面，本功能返回可见表面的深度估计，不吸附到高斯中心，也不取包围盒交点。用户接受的定义需在模型边缘、半透明结构和局部重建变形处有一致行为。

拾取使用当前已呈现 scene、相机、物理视口、镜像和实际渲染质量，包括点抽样间隔、Gaussian 截断、alpha cutoff、协方差 blur、半径上限与排序方式。不得在画面降采样时使用未显示的全量点造成看不见的位置命中。正常浏览不新增持续 GPU→CPU 深度或点数组读回。

采用以下估计规则：

1. 在点击像素中心求各可见高斯的 alpha，投影、裁剪和 alpha cap 与主渲染一致。
2. 按实际合成层序求各高斯对最终像素的可见贡献 `w_i = alpha_i * product(1-alpha_j)`，j 为在其前方合成的层；忽略背景颜色的贡献。不能把径向排序偷换成另一种排序。
3. 总贡献小于 0.1 时为未命中；对中心位于近远面外而仅支撑范围可见的高斯，不将钳制到近平面的投影深度伪造为测量深度。
4. 对有效候选的正向视深度，以可见贡献计算加权中位数作为估计深度；精确中点取较近层，同深度以原始点索引保持确定性。采用中位数以避免将两个分离表面的平均值变成悬空端点。
5. 沿点击像素中心的射线还原该深度处三维位置。深度是相机前向距离，不是射线长度，非中心像素需做相应换算。无有限有效深度、贡献不足或坐标不可恢复则返回未命中/结构化错误，不使用零坐标兜底。

单次拾取允许异步有界结果读回，结果不携带整帧图像、排序数组或全部点数据。开始时显示定位状态，5 秒内必须完成或超时退出，可取消、退出或关闭窗口。真实大模型的延迟单列记录，不将 5 秒终止上限当作性能成绩。UI 不同步等待 GPU fence。

## 7. 模块职责与异步契约

| 提供者 | 本功能职责 | 约束 |
|---|---|---|
| model-io / 源元数据 | 既有场景和实际 PLY 坐标选项，不新增指纹契约 | 不依赖 D3D12/WinUI；不解析 GPS 标定业务 |
| render-core | 与当前画面一致的屏幕拾取、有限深度结果和请求身份 | 不解析 JSON、不识别 PLY、不调用 UI |
| engine / SDK | 标定解析与验证、会话归属、坐标还原、米制计算、测距会话 | 非阻塞命令与值类型快照/结果；不持有控件 |
| desktop-viewer | 文件选择、模式引导、点击/拖动区分、端点/线与结果显示 | 不遍历 splat、不做逐点投影或标定业务计算 |

新增公共契约先分别修订提供者规格，再改公共头。`IEngine` 提供 `import_metric`、`set_measurement_enabled`、`measure_at`、`clear_measurement` 与权威快照；`IRenderer` 提供 `request_pick` 和 `take_pick_result`。取消导入通过清除/退出/关闭完成。

标定与测量请求包含单调操作 ID 和会话版本；拾取还绑定活动 scene 身份、surface generation、viewport revision、相机/镜像 revision 及质量 revision。拾取服务必须针对已呈现的对应帧；无法保留该帧必要状态时拒绝为 StaleRequest，不能拿新相机计算旧鼠标点击。

在途拾取遇到相机移动、resize/DPI、镜像、质量变化或重新导入标定，旧结果失效，保留已确认端点并提示重选。清除/退出递增会话版本；任何迟到结果都不能添加端点。结果保存在可查询状态，不仅依赖可能溢出的事件队列；资源及队列有界。

模型正在替换时暂停新测距请求；失败/取消后恢复旧会话，成功激活清除它。设备丢失时取消在途拾取并暂停测距；保留同一 CPU 源场景的标定和已确认端点；恢复且首帧呈现后可继续。0×0 视口暂停拾取。close/shutdown 立即撤销操作资格，后台完成资源回收，UI 无同步等待。

错误至少区分：无活动模型、无效 JSON、文件/嵌套超限、不支持版本/坐标约定、标定不可用、无效矩阵/比例尺、读取失败、取消、请求过期、繁忙、超时和设备不可用。未命中是正常拾取结果。错误提示固定中文映射；限长本地日志不记录 JSON 原文、完整模型路径或逐帧端点。

## 8. 技术栈与项目结构

沿用 Windows 11 x64、C++20、VS 2026 / MSVC、CMake 4.3+、Windows SDK / DXC 10.0.26100.0、D3D12 FL12.0 / SM6.0 / WaveOps、WinUI 3 / C++/WinRT。现有 Windows App SDK `1.8.260921001`、CppWinRT `2.0.250303.1`；GoogleTest 保持仓库锁定版本。

使用 Windows 内建 JSON（C++/WinRT，链接系统 windowsapp），不新增第三方 JSON/几何依赖，不引入模型 SHA256。纯原生 SDK 公共头不暴露 WinRT 类型。

| 位置 | 用途 |
|---|---|
| `include/model-io/`、`src/model-io/` | 既有源场景/坐标元数据 |
| `include/native3dgs/`、`src/engine/` | 测距值类型、校验、计算、会话与 SDK 命令 |
| `include/render-core/`、`src/render-core/passes/`、`shaders/` | 按需拾取和投影复用 |
| `GUI/MainWindow.xaml.*` 及宿主源文件 | 测距入口、选择器、输入和叠加反馈 |
| `tests/model-io/`、`tests/render-core/`、`tests/engine/`、`tests/sdk/` | 对应模块的契约、数学、GPU、事务与安装消费测试 |
| `docs/SPEC-*.md`、`docs/*verification.md` | 提供者规格与真实验证证据 |

计划和任务已追加到 `tasks/plan.md`、`tasks/todo.md`，保留既有未完成验收项。

## 9. 命令

从仓库根目录 PowerShell 执行，输出目录沿用既有工程：

```powershell
# 首次缺少依赖时初始化；不升级 pin。
git submodule update --init --recursive

# Release x64 完整构建。
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' .\Native3DGSViewer.sln /restore /m /p:Configuration=Release /p:Platform=x64

# 完整 VS 原生测试桥。
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe' .\out\Release\Native3DGSViewer.Tests.dll /Platform:x64

# 各模块及安装 SDK 消费组。
ctest --test-dir out/cmake -C Release --output-on-failure

# SDK 安装及 GUI 启动；安装消费验证沿用 tests/sdk/installed-sdk.ps1。
cmake --install out/cmake --config Release --prefix out/sdk --component SDK
& .\out\Release\Native3DGSViewer.GUI.exe

# 需要交付安装包时运行。
& .\packaging\build-installer.ps1 -SkipBuild

# 文档/变更空白检查。
git diff --check
```

新增文件需要同时进入 CMake/MSBuild 对应工程；shader 编译目标、安装清单和 SDK 示例按既有约定同步。尚无固定 clang-format 配置，不编造格式化命令。

## 10. 代码风格

使用 C++20、四空格、类/枚举 PascalCase、函数/变量 snake_case。已有边界字段保持现有命名，避免无关重命名。值类型携带明确坐标/单位；double 用于世界坐标和距离，HLSL 与宿主布局同步。

以下仅展示计算风格，不是已发布 API；实际入口必须先完成坐标、有限性及溢出校验：

```cpp
double distance_meters(const gs::Double3& a_ply, const gs::Double3& b_ply,
                       double meters_per_model_unit)
{
    const double dx = b_ply.x - a_ply.x;
    const double dy = b_ply.y - a_ply.y;
    const double dz = b_ply.z - a_ply.z;
    return std::hypot(dx, dy, dz) * meters_per_model_unit;
}
```

## 11. 测试策略与成功条件

GoogleTest 按行为命名；没有数字覆盖率目标。纯 CPU 测试验证格式、矩阵和距离；GPU 测试用合成场景验证拾取语义；engine 测试验证异步事务和版本过滤；安装 SDK 消费验证防止功能只在 GUI 内可用。WinUI 人工验收记录截图、输入文件哈希、相机、物理视口、DPI、镜像、实际质量和构建命令。性能独立于普通单测。

| ID | 可验证成功条件 |
|---|---|
| M01 | 从 Ready PLY 进入测距，导入引导 → 校验 → A → B → 米制结果完整可用；取消选择/空白点击不损坏已有状态 |
| M02 | 正常 v1、额外普通字段、模型改名通过；坏 JSON、重复关键字段、错类型、超限/过深、NaN/Inf/溢出、错误版本/状态/坐标约定均有稳定结果 |
| M03 | 缺少 model 字段、内容/名称/大小/指纹不同均允许；失败质量状态及 null 被识别为不可用 |
| M04 | 0/负/非有限比例尺、矩阵转置、非互逆、比例尺冲突、非均匀尺度/反射均拒绝；有效旋转+平移+统一尺度通过 |
| M05 | 合成 (0,0,0) 与 (3,4,0)、s=2 得 10 m；示例矩阵的单位距离得 82.8007956818355 m；CPU 距离误差不超过 `1e-9 * max(1, expected_m)` |
| M06 | RUB/RDF、局部原点、旋转平移、非中心屏幕射线、大世界坐标及显示镜像下，坐标还原和距离满足同一数学契约；重合端点为零 |
| M07 | 合成前后表面、透明前景/不透明背景、空像素、低总贡献、投影边界、近平面交叉验证第 6 节规则；GPU 有效深度误差不超过 `1e-4 * max(1, expected_depth)`，无效深度不发布端点 |
| M08 | 100% / 150% / 200% DPI、非零视口偏移、resize、Y 镜像 GUI 与 SDK 三轴八种组合均验证点击/标记对齐；合成端点重投影偏差不超过 1 物理像素 |
| M09 | 点击不旋转，拖动不选点；平移/缩放可用；退出恢复浏览模式；第三次点击不覆盖结果；重新测量/导入/退出清除规则成立 |
| M10 | 导入/拾取期间切换模型、取消、关闭、相机移动、镜像、DPI/尺寸、设备恢复及 shutdown 不接收过期结果，不把旧标定套到新场景 |
| M11 | 按真实 PLY+JSON 完成可见双点测距，显示“GPS 估算尺度，未实测验证”；记录截图与实际延迟；有独立实测长度时另报误差，无则标注未验证 |
| M12 | 测距关闭时无新增每帧读回；连续 100 次重测与 20 次模式进出后无持续资源/队列增长；单次拾取成功/未命中/取消/错误/超时均有终态且最长 5 秒 |
| M13 | Release 构建、完整 VSTest/CTest 和安装 SDK 消费通过，既有相机/加载/渲染/恢复回归无退化；新按需拾取的有限读回例外先写入 render-core 规格 |

真实模型的几何误差、GPS 尺度误差和端点选择误差分别记录，不能用合成数学通过替代真实长度精度证明。尚缺的独立实测、跨厂商 GPU 和 DPI 实测应明确列为未验收。

## 12. 边界与审阅门槛

- 始终执行：先更新提供者规格再修改契约；输入/资源/坐标检查；UI 非阻塞；允许任意比例尺；过期请求过滤；保留质量说明；改动后运行对应测试和全量必要检查。
- 先征求确认：扩展到其他平台、多线历史/导出/高差等新能力，改变显示质量或拾取定义，新增第三方依赖或升级 pin、改变 CI/发布渠道。常规可逆修复与既定范围内实现选择不额外加审批。
- 禁止：忽略质量状态、猜测未记录坐标帧、把 GPS 残差当长度精度、用高斯中心/包围盒命中冒充本规格的表面估计、UI 线程等 GPU/读大文件、复制整个模型用于测距、无提示降质量、修改第三方源码或外部训练产物、删除失败测试或伪造验证记录。

用户已确认表面深度估计、任意比例尺及开始执行。实现与验证记录见 [测距验证记录](model-measurement-verification.md)；既有外部性能/硬件待验收项继续有效。
