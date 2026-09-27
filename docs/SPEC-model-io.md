# Spec: model-io（本地 3DGS 模型输入）

状态：model-io 已实现，验收证据见[验证记录](model-io-verification.md)；2026-09-26。依据[总技术计划](technical-development-plan.md)能力图。

## 1. 目标、用户与范围

`model-io` 为 Windows 原生查看器把本地文件转换为可交给 `render-core` 的不可变 splat 场景。直接使用者是 `desktop-viewer` 和测试/基准程序。用户应能打开标准 3DGS PLY 或 SPZ，看到真实的加载状态，取消时保留旧场景；损坏、超限或不支持的文件必须给出稳定错误，不能让窗口崩溃、卡死或显示半个模型。

首期接受：二进制小端、原始 Graphdeco 3DGS 属性布局的 PLY；Niantic SPZ v1-v4、SH 0-3 阶、无首期未支持的渲染扩展。首期明确拒绝 ASCII/大端 PLY、压缩 PLY、SPLAT/KSPLAT/SOG/RAD、SPZ SH 4 阶、要求 mip-splat 的 antialiased 标记和未知扩展。额外的 PLY 标量属性或非 vertex 元素可忽略，但标准 3DGS 必需属性不能缺失。后续新增格式通过内部 codec 注册表接入，不改变调用者的加载方法。

不负责：D3D12 资源分配、GPU 上传、相机、颜色管理策略、LoD、远程 URL、文件写回或 UI 本地化。`model-io` 返回 3DGS 训练色值域的 DC/SH 数值及明确元数据，不提前做 sRGB/线性光转换；渲染器决定颜色转换、上传和 CPU 场景寿命。

## 2. 设计依据与关键取舍

| 决定 | 原因及代价 |
|---|---|
| C++20 公共接口，解码器以独立辅助进程运行 | PLY 按块读取；Niantic SPZ 的加载 API 返回完整 cloud，仍需进程隔离处理取消、超时、崩溃和内存耗尽。Job Object 是故障/资源隔离，不宣称构成安全沙箱。 |
| PLY 头部预检与分块解码 + Niantic SPZ v3.0.0 | PLY 从已打开的文件句柄按属性偏移读取，每批目标约 4 MiB，避免整文件及整顶点数组复制；SPZ v3.0.0 读取旧版 gzip 与新版 ZSTD 文件。 |
| 共享内存交付不可变 SoA | 解码进程和宿主间不逐点发送消息，避免多次复制近 GB 场景；只在完整验证后发布。代价是需要严格校验共享内存布局和所有权。 |
| 源码级 codec 扩展，不承诺首期 DLL 插件 ABI | C++/STL 二进制 ABI 与工具链耦合，先以受测的内部注册表添加格式；未来确需第三方插件时另定义版本化 C ABI。 |
| 统一为右手 RUB 坐标、局部 float32 中心 + double 原点 | 与内部相机朝 -Z 的约定一致，降低大坐标场景的 GPU 量化风险；PLY 常见 RDF 坐标须显式转换，不能以文件扩展名推断所有数据的朝向。 |

依赖锁定：miniply 固定于 `1a235c70390fadf789695c9ccbf285ae712416b3`，仅作为历史解析器参考，不参与当前构建；Niantic SPZ v3.0.0 对应 `5bf2945de1a003cee07133b1e495fe9c6ffdc7e7`。项目提交时记录源码、补丁、zlib/ZSTD 等传递依赖版本及许可证。Spark 2.0 的 Rust 工作区标有 `Proprietary`，在许可厘清前仅作行为对照，不复制其代码。

## 3. 公共契约

`splat-types` 提供只读场景数据、`SourceFormat` 和 `SceneHandle`，`model-io` 提供加载接口。共享类型定义在 `gs` 而非 `gs::io` 命名空间，以便 `render-core` 只依赖 `splat-types`。下列声明是要实现的接口形状，命名可在模块评审中作不破坏语义的调整：

```cpp
namespace gs {
struct SplatScene;
enum class SourceFormat : uint8_t { Ply, Spz };
using SceneHandle = std::shared_ptr<const SplatScene>;
}
namespace gs::io {
enum class Coordinates : uint8_t { Rdf, Rub };
enum class LoadStage : uint8_t { Opening, Inspecting, Decoding, Validating, Ready };

struct LoadLimits {
    uint64_t maxInputBytes = UINT64_MAX;
    uint64_t maxSplats = UINT32_MAX;
    uint64_t maxSceneBytes = UINT64_MAX;
};
struct LoadRequest {
    std::filesystem::path path;             // Windows Unicode path
    Coordinates plyCoordinates = Coordinates::Rdf;
    LoadLimits limits{};                    // Caller may tighten defaults
};
struct LoadProgress {
    LoadStage stage;
    uint64_t bytesRead;                     // 0 when library cannot report it
    std::optional<uint64_t> totalBytes;
};
enum class LoadErrorCode : uint8_t {
    NotFound, AccessDenied, IoFailure, UnsupportedFormat,
    UnsupportedVersion, UnsupportedFeature, InvalidHeader,
    TruncatedData, InvalidAttribute, EmptyScene, ResourceLimit,
    OutOfMemory, Cancelled, Timeout, DecoderFailure, DecoderCrashed,
    ObserverFailure
};
struct LoadError {
    LoadErrorCode code;
    LoadStage stage;
    std::optional<uint64_t> byteOffset;
    std::string diagnostic;                 // Bounded, for logs; UI uses code
};
using LoadResult = std::variant<gs::SceneHandle, LoadError>;
using ProgressSink = std::function<void(const LoadProgress&)>;

class IModelLoader {
public:
    virtual ~IModelLoader() = default;
    virtual LoadResult load(const LoadRequest&, std::stop_token,
                            ProgressSink) = 0;
};
}
```

`load()` 是阻塞调用，必须由调用方放到工作线程；`desktop-viewer` 用 `std::jthread` 持有一次加载及其 `stop_token`。同一查看器同时只启动一个加载：选择新文件时请求取消旧进程、等待它退出，再启动新请求。进度回调在工作线程执行，UI 负责转发到 WinUI dispatcher；回调不得持有模块锁，回调异常由边界捕获为 `ObserverFailure`，同时终止该次辅助进程。每次调用恰好得到一个终态结果，不同时返回场景和错误。取消、失败和超时不改变当前已显示的场景。

`LoadRequest::limits` 只能收紧默认值。`model-io` 按场景布局、可用物理内存及提交余量准入；它不依赖 D3D12，也不提前假定某个渲染器的缓冲地址能力。`render-core` 在上传前继续按自身地址范围和 DXGI 当前预算校验，因而极大场景可能在成功解码后被渲染器拒绝。`diagnostic` 截断为 512 字节，不包含原始文件内容；用户提示由稳定错误码映射，不依赖第三方库的字符串。

### `SplatScene` 数据约定

| 字段 | 类型/布局 | 约定 |
|---|---|---|
| `count`、`shDegree`、`sourceFormat` | `uint64_t`、`uint8_t`、枚举 | `count > 0`，`shDegree` 为 0-3 |
| `worldOrigin`、`bounds`、`maxScale` | double3、double AABB、double | RUB 世界坐标；`bounds` 是 splat **中心**包围盒，`maxScale` 为所有尺度分量最大值；`centerLocal + worldOrigin` 得到世界中心 |
| `centerLocal`、`scale` | 每点 3 个 float32 | 中心相对原点；尺度为有限正值、单位为世界单位 |
| `rotation` | 每点 4 个 float32，`xyzw` | 归一化四元数 |
| `opacity`、`rgb0` | 每点 1、3 个 float32 | opacity 在 [0,1]；`rgb0 = 0.5 + C0 * f_dc`，保持训练色值域，不提前截断或应用 sRGB transfer |
| `shRest` | 每点 `3 * ((degree+1)^2 - 1)` 个 float32 | 系数优先、RGB 通道相邻；0 阶时为空 |

数组长度必须与 `count` 和 `shDegree` 的受检乘法结果精确一致。`SplatScene` 持有只读映射的 RAII 所有者并以 `std::span<const float>` 暴露分量数组；调用者不得保留超过 `SceneHandle` 生命周期的 span。共享内存布局是内部版本化协议，不能当作公开磁盘格式或稳定 DLL ABI。`render-core` 在上传 fence 完成前保留 `SceneHandle`，之后可释放 CPU 副本。

## 4. 解码与标准化

### PLY

1. 用文件签名和最多 64 KiB 的头部识别 `ply`、`binary_little_endian 1.0`、`vertex` 数量和属性表。对属性名、重复项、类型、逐点 stride、`count * stride`、文件实际长度做受检 64 位运算；拒绝 vertex 中的 list 属性及不足的文件体。未知标量属性和其他元素可跳过，属性顺序不固定。
2. 必需 float32 属性为 `x/y/z`、`f_dc_0..2`、`opacity`、`scale_0..2`、`rot_0..3`。`f_rest` 必须完整为 0、9、24 或 45 个 float32，分别对应 SH 0-3 阶；部分连续、重复或其他数量均报 `InvalidHeader`。
3. 在已打开的二进制文件流上定位 vertex 起始偏移，按预检属性偏移分块读取并直接写入共享场景。读取失败返回字节偏移，非法点返回点索引和字节偏移；不复制整个 vertex 元素。
4. 原始 PLY 的 `rot_0..3` 按 `wxyz` 解码为 `xyzw`，尺度取 `exp(logScale)`，透明度用数值稳定的 sigmoid，0 阶颜色按 `0.5 + 0.28209479177387814 * f_dc`。Graphdeco 的 `f_rest` 按颜色通道分组，转换为统一的“系数优先、RGB 相邻”布局；SH 符号随坐标转换同步调整。
5. 默认将常见 PLY RDF 坐标转换到 RUB；对明确来自 RUB 的文件由调用者指定 `plyCoordinates=Rub`。PLY 没有可靠的通用坐标元数据，因此不提供猜测式 `Auto`。转换同时作用于中心、四元数和方向性 SH，不改变点顺序。

### SPZ

1. 识别 gzip（v1-v3）或明文 `NGSP`（v4）。旧版只解压取得前 16 字节头部，预检最多消耗 1 MiB 压缩输入；v4 先校验 32 字节头、TOC 长度及各流声明大小。全部先验证 magic、版本、点数、SH 阶数和已知标记，再交给 Niantic 库。压缩包大小和解压声明都不是可信预算证明，辅助进程内存上限始终生效。
2. 用 Niantic SPZ v3.0.0 的内存指针 API 读取宿主传来的只读文件映射，`UnpackOptions.to = RUB`；不调用窄字符文件名 API。库返回空 cloud 时按 `DecoderFailure` 处理，因为首期不允许空场景。校验返回的各数组长度后才转换到共享输出。
3. SPZ `GaussianCloud` 的 scale 为 log-scale、alpha 为 logit、color 为 SH DC；应用与 PLY 相同的标准化。PLY/SPZ 的 alpha logit 为 `-inf` 或 `+inf` 时分别规范化为有限 opacity 0 或 1；NaN 和其他属性的非有限值仍拒绝。SPZ 四元数已经是 `xyzw`，SH 非零阶已按系数优先、RGB 相邻；不重复改变顺序。
4. SH 4 阶、antialiased 标记、LoD/未知扩展或未知 flags 返回 `UnsupportedFeature`，不得只显示被截断的低阶数据。新增支持须先扩展统一契约及渲染正确性测试。v4 基础文件在满足首期约束时正常支持。

两个 codec 共用 `IFormatDecoder::probe/decode` 内部契约、数值验证和场景发布逻辑。格式探测以内容为准：扩展名与内容不一致时按实际受支持格式解码，并在诊断日志中记录不一致；无法明确识别则拒绝。每个 splat 检查输入及转换后的有限性、正尺度、有效四元数范数，归一化旋转；还须以受检 float32 运算确认尺度平方可表示，避免 GPU 协方差立即溢出。任何一个无效点使整个加载失败，不静默丢点或替换默认值。颜色和 SH 系数允许有限的负值及高于 1 的值，供渲染阶段按参考算法处理。中心包围盒和有限正值的 `maxScale` 在标准化后计算，原点选中心包围盒中点并以 double 存储，再生成 float32 局部中心；超出 float32 可表达范围则报 `InvalidAttribute`。共享映射头同时保存 `maxScale`，宿主复核它与数组计算结果一致。特定画质参数下的支撑范围和相机可表示性由桌面/渲染层检查。

## 5. 故障隔离、资源预算和取消

宿主通过 `CreateFileW` 以只读、仅共享读取方式打开文件，按最终文件句柄确认它是本地常规文件（拒绝 UNC/远程卷）并取得实际长度；后续使用同一 HANDLE，避免文件名重新解析造成竞态。`CreateProcessW(CREATE_SUSPENDED)` 启动每次加载专属 `model-io-helper.exe`，以受控继承句柄列表只传入文件和 IPC 句柄；先加入启用 `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`、`JOB_OBJECT_LIMIT_JOB_MEMORY` 的 Job Object，再恢复运行。创建/配置 Job 或 `AssignProcessToJobObject` 失败时终止仍处于挂起状态的 helper 并返回 `DecoderFailure`，不能在无限额模式下继续解码。辅助进程与宿主同用户，故此机制只隔离崩溃/资源故障，并非隔离恶意代码的安全边界。

默认不以固定输入字节数、点数或输出字节数拒绝场景；调用者可收紧限制。辅助进程提交内存上限取启动时可用物理内存和提交余量较小者的 50%，小于 64 MiB 时拒绝启动。单次加载墙钟时间上限 180 秒。计算输出字节数时按 SH 阶数使用受检 `uint64_t` 乘加；SH 3 阶的规范化数据为 236 字节/点。PLY 按约 4 MiB 的块读取，SPZ 仍为完整 cloud。Job 限额只约束 helper，不覆盖宿主创建的共享映射、已有活动 `SceneHandle` 和待命场景；宿主按规范化输出大小、可用物理内存和提交余量预检。共享映射在两个进程中的视图不按两份物理数据重复计费，但提交/地址空间仍分别核实。实际分配失败仍返回 `OutOfMemory`；不足时保留旧场景，不偷偷释放旧模型或降低 SH/点数。

控制通道使用版本号与长度前缀的二进制消息，单条最大 64 KiB；传进度、布局、错误和完成状态。共享映射头包含 magic、协议版本、点数、SH 阶数、原点/包围盒，以及每个数组的 64 位 offset/length；数组按 16 字节对齐，offset 与长度用受检加法验证为互不重叠且位于映射内部。宿主在核对布局、上限及宿主 RAM/提交余量后创建共享内存映射，并把输出 HANDLE 交给子进程；子进程写入后提交 `Ready` 并正常退出。宿主确认退出码成功后以只读方式映射结果，重新校验布局、数值并计算包围盒，才生成 `SceneHandle`。只接受完整提交；子进程异常退出、IPC 损坏或超时均释放映射及句柄，返回结构化错误。临时缓冲与子进程由 RAII 持有，正常/错误/取消路径都必须清理。

`std::stop_token` 触发后宿主终止该加载的 Job Object，不等待第三方解码函数自行返回；目标是 UI 在 200 ms 内显示“取消中”，辅助进程与映射在 3 秒内回收，确认退出后才返回 `Cancelled` 并显示“已取消”。若取消早于最终 `SceneHandle` 发布点，即使 helper 刚刚成功也返回 `Cancelled` 并释放待命结果；发布成功后才到达的取消由桌面层的 `RequestId`/ticket 防止旧结果激活。单调阶段进度为 `Opening -> Inspecting -> Decoding -> Validating -> Ready`；可报告读取字节时给精确值，库内部无法报告时只显示阶段，不伪造百分比。成功结果只在所有校验结束后可见，调用方原有场景保持不变直到成功替换。

| 故障来源 | 返回码与处理 |
|---|---|
| 文件不存在、共享冲突或无读取权限 | `NotFound`、`AccessDenied` 或 `IoFailure`；不启动解码辅助进程 |
| 不认识的签名、版本、SH/扩展 | `UnsupportedFormat`、`UnsupportedVersion` 或 `UnsupportedFeature`；不尝试降级 |
| 截断、属性不合法或空场景 | `TruncatedData`、`InvalidHeader`、`InvalidAttribute` 或 `EmptyScene`；丢弃全部输出 |
| 声明超限、Job 内存限制或分配失败 | `ResourceLimit` 或 `OutOfMemory`；终止辅助进程并释放映射；Job 限额事件优先归为 `ResourceLimit` |
| 用户取消或达到 180 秒 | `Cancelled` 或 `Timeout`；终止该次 Job，保留旧场景 |
| codec 返回失败、辅助进程异常或进度回调抛错 | `DecoderFailure`、`DecoderCrashed` 或 `ObserverFailure`；日志保留限长诊断，UI 使用稳定错误码 |

第三方库 stdout/stderr 由宿主异步持续读取，日志只保留限长前缀、超限内容继续丢弃以免管道填满卡住 helper；仅用于本地诊断，不直接作为 UI 提示。默认日志不保存模型字节或完整路径。错误后下一次加载必须可独立成功，不依赖重启进程。

## 6. 工程结构、命令与代码约定

建议的工程边界：`include/splat-types/` 放跨模块只读数据定义；`src/model-io/client/` 放公共 API 与 Job/IPC 客户端；`src/model-io/helper/` 放进程入口、codec 注册与预算执行；`src/model-io/codecs/` 放 PLY/SPZ 适配；`src/model-io/normalize/` 放唯一的坐标、SH、数值转换；`tests/model-io/` 放单测、进程集成测试与语料；`third_party/` 放锁定依赖及许可文件。第三方源码补丁独立保存并说明理由，升级时逐项重放和复测。

使用 VS 2026 解决方案与 GoogleTest；先运行 `git submodule update --init --recursive`，再从仓库根目录执行：

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' .\Native3DGSViewer.sln /restore /m /p:Configuration=Release /p:Platform=x64
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe' .\out\Release\Native3DGSViewer.Tests.dll /Platform:x64
```

源码统一 C++20、`clang-format` 固定配置、类/枚举 PascalCase、函数/变量 snake_case；错误用 `LoadResult` 而非混合 `nullptr`、异常和空 cloud。公共头只暴露稳定数据与调用契约，不暴露 miniply、SPZ、Job Object 或 IPC 类型。任何公开接口改动先更新本文和消费者契约测试。

## 7. 测试与验收

| 类别 | 必测场景与通过条件 |
|---|---|
| PLY 属性 | 属性重排、附加标量/元素、SH 0/1/2/3、中文路径、旧版 `wxyz` 旋转；输出数值与手工小样本一致 |
| SPZ 版本 | v1-v4 基础样本，v3 工作区 509,812 点和 3,914,609 点样本；点数、坐标、旋转、颜色/SH 与可信解码结果一致 |
| 拒绝策略 | ASCII/大端/压缩 PLY、SPZ SH 4/antialiased/扩展、未知格式、零点、缺失属性；返回规定错误且无场景句柄 |
| 数值与完整性 | opacity logit 的正负 Inf 分别规范化为 1/0；其他 Inf、NaN、零四元数、溢出尺度、截断、长度不符、计数溢出、文件名/内容不符均拒绝；不崩溃、不发布半成品 |
| 资源与取消 | 超出输入/输出/helper/宿主 RAM 与提交预算、Job 赋予失败、取消与成功竞态、助手进程崩溃/无响应、stdout/stderr 洪泛、重复打开 100 次；宿主存活，旧场景保留，进程/句柄/内存回到稳定水平 |
| 坐标与画质 | RDF->RUB 后固定姿态与 Spark 对照；SH 通道排列、旋转符号及原点重定位经单测和图像对比确认 |

测试语料包括总计划中的 [1.ply](../../1.ply)、[jidaoshan.spz](../../Viewer_android/public/scene/jidaoshan.spz)、[zhihuizhimen.spz](../../Viewer_android/public/scene/zhihuizhimen.spz)，先记录 SHA-256 和来源授权。最小合成文件覆盖每个字段和非法边界。对头部解析、IPC 布局及 codec 入口建立变异/fuzz 测试：PR 上短时运行，夜间长时运行；正式发布前至少完成一次开启 AddressSanitizer 的持续语料测试，并清零可复现崩溃。第三方库升级必须重跑语料、取消/超限和差分画面对照。

验收记录必须包含命令、依赖提交、语料哈希、成功/失败数、峰值内存与句柄数。常规错误不得跨公共 API 抛出；除进程级不可恢复故障外，所有输入错误均映射到 `LoadErrorCode`。单次失败后立即加载有效模型必须成功，无需重启查看器。

## 8. 边界与后续

- 始终：在不可信文件进入 codec 前做格式与预算预检；对乘加、offset 和数组长度做溢出检查；保留第三方许可；只发布完整且不可变的场景。
- 需要规格更新：提高资源硬上限、增加格式/SH 阶数、改变坐标约定或错误语义、公开二进制插件接口。
- 不做：静默截断 splat/SH、用默认值掩盖无效属性、让 UI 线程直接运行解码、把 Job Object 称作安全沙箱、在许可未厘清前复制 Spark Rust 实现。

后续 F1 添加压缩 PLY、SPLAT、KSPLAT、SOG 时各建 codec 和兼容性语料；F3 的 RAD/LoD 流式页数据需要新接口，不能把本规格的完整单场景结果强行用于无限世界。新增格式与公开契约变更须先更新本规格和消费者测试。

## 参考

- [总技术计划](technical-development-plan.md)、[Spark 2.0 格式说明](../../spark-2.0.0/docs/docs/loading-splats.md)、[Spark 原始 PLY 解码行为参照](../../spark-2.0.0/rust/spark-lib/src/ply.rs)。
- [miniply README/API](https://github.com/vilya/miniply/tree/1a235c70390fadf789695c9ccbf285ae712416b3)、[Niantic SPZ v3.0.0 API 与格式说明](https://github.com/nianticlabs/spz/tree/v3.0.0)、[Windows Job Object](https://learn.microsoft.com/windows/win32/procthread/job-objects)。
