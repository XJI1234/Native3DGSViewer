# RFC：云端—端侧协同 3DGS 单帧渲染架构

日期：2026-10-02。状态：**研究与架构提案，待审阅，不是已批准模块规格或性能承诺**。

本文面向 Windows D3D12 与 Android Vulkan 工程。仅交付文档；未修改公共接口、着色器、SDK 或构建系统，未实施服务器和网络客户端。

## 0. 结论、假设与审阅入口

### 0.1 建议主线

建立**以屏幕空间单帧为交付对象、可切换计算切分点的原生云端—端侧引擎**：

- 模型、SH、三维协方差、GPU 投影和稳定排序常驻服务器；端侧不下载完整 PLY/SPZ，不创建本地 SplatScene。
- `ProjectedSplats`：发送当前视角已经着色、排序的二维高斯；端侧只解包、Gaussian 光栅化和透明合成。这条路径严格验证“云端算投影/排序、端侧完成泼溅”。
- `ImageFrame`：服务器进一步光栅化，发送单帧图像；端侧解码、纹理绘制、UI 和呈现。它是带宽/算力基线，不冒称端侧 Gaussian 光栅化。
- `HybridTiles`：屏幕划分互不重叠的像素区域，稀疏区域发二维高斯，密集区域发已合成图像；最弱终端允许全部区域使用图像。

**不承诺“任意百万点场景只靠二维高斯压缩即可实现低带宽、低端侧开销”。**卸载投影/排序后，高斯数量、透明 overdraw 和传输字节仍可能很大。固定视角不需要保留全部高斯可编辑性，区域预合成是必须实测的竞争方案。

若要求“Gaussian 权重与逐点透明混合必须全部发生在端侧”，只允许 `ProjectedSplats`，并接受部分场景可能不满足带宽目标；不能偷偷切成图像流。

### 0.2 显式假设

1. 服务器已有获授权模型；上传、训练、资产管理不在本轮范围。
2. 客户端为 Windows/Android 原生宿主，不引入 WebView；本地查看模式保留。
3. 一请求、一确定相机、一分辨率、一质量配置、一帧自包含结果；不做连续运动、帧间 delta、预测、重投影、AR/VR 或 mesh 遮挡。
4. 首期可以用 Windows GPU 服务器；Linux 首发需另计 Vulkan headless 和解码进程移植成本。
5. 带宽暂按 1/5/10 Mbit/s 可用下行，RTT 20/40/80 ms 实验；不是用户已确认 SLA。
6. 1080p 主档，720p 明确标识的节流档；不得未声明降低分辨率、SH 或点数。
7. 图像/瓦片能否作为正式模式待确认；保留严格泼溅模式供判断。

### 0.3 Phase 0：待审能力图

遵循 spec-driven-development：只提出能力图和研究设计，审阅前不生成模块 SPEC、正式任务清单或进入实现。

| 稳定模块 ID | 提供者职责 | 依赖 |
| --- | --- | --- |
| `remote-frame-types` | 相机请求、帧身份、颜色/alpha、2D 高斯/瓦片值类型 | 平台无关数学 |
| `cloud-render` | headless GPU 驻留、投影/排序、导出高斯/离屏像素 | 帧类型、既有 model-io/render-core 适配 |
| `frame-codec` | 量化、压缩、序列化、校验、受限解码 | 帧类型 |
| `remote-transport` | 鉴权、请求/取消、可靠传输、流量控制 | 帧类型 |
| `cloud-service` | 准入、模式选择、GPU 调度、缓存、编码响应 | cloud-render、codec、transport |
| `thin-render` | 2D 光栅化/纹理合成、Surface 生命周期 | 帧类型、平台图形 API |
| `remote-client` | 请求事务、接收解码、旧帧保留、呈现确认 | transport、codec、thin-render |

候选顺序：值类型 → 无网络的离屏导出/薄渲染闭环 → 编解码 → 可靠传输 → 服务/客户端事务 → 模式选择与测量。依赖无环；提供者定义协议，消费者不复制约定。UI 留在既有两平台宿主。

## 1. 仓库现状与复用基础

### 1.1 工程资料与边界

主要依据：根 README、Windows 总体计划/系统设计、model-io/render-core/engine-SDK 规格和 SDK 指南/接口手册；Android 总体计划、系统设计、能力图、shared-types/render-core/engine/bridge/package 规格。状态优先采用 dated verification，不把早期计划或 development-memory 的旧状态当成当前事实。

| 层次 | Windows | Android | 本次复用/补齐 |
| --- | --- | --- | --- |
| 场景 | 不可变 SceneHandle；double 原点、float 局部属性 | 同一 RUB/SH/所有权语义 | 服务器保留；内部共享内存 ABI 不是网络格式 |
| 解码 | Win32 helper / Job、PLY/SPZ、zlib/zstd | FD、Service、共享场景导入 | Windows 可复用；Linux 新进程适配 |
| GPU | D3D12/HLSL、FidelityFX 稳定 radix | Vulkan 1.1/GLSL/SPIR-V、稳定 radix | 复用数学，新增离屏/export |
| 投影态 | `shaders/splat.hlsl` Ellipse 为 40 B | `ForAndroid/shaders/project.comp` 为 48 B，含 pad | 不直接发送 GPU struct 内存 |
| 生命周期 | DXGI；Ready 绑定首次成功 Present | Surface/generation/ticket/Present | 新增 RemoteFrame，不能伪装成本地场景 |
| 静止行为 | 排序可复用 | 规格已规定按变化绘制 | 远程帧保持最后画面，无重复计算 |

**现有公共 SDK 没有可直接使用的跨平台服务器 headless/export 接口。**Windows 以 swapchain/Present 为核心，Android 以 Surface 为核心；把 SDK 放进服务进程不是完成服务器化。

### 1.2 性能证据

- `ForAndroid/docs/verification/device-performance-2026-09-30-adreno750.md`：1,888,950 点最终 subgroup 路径，GPU project/sort/draw 阶段均值约 19.54/17.84/16.06–16.08 ms；显示间隔约 50 ms，1% low 约 13–15 FPS。两轮热状态变化，不能视为完整受控比较；移动 30 FPS 门槛未通过。
- 0.75 Surface 档仍提交全部点，投影/排序基本不变，支持卸载这两阶段；剩余约 16 ms draw 说明**只卸载前半管线仍不能保证弱客户端轻量化**。
- Adreno 750 storage-buffer descriptor range 为 128 MiB；48 B 投影缓冲仍限制大场景准入。薄客户端不能膨胀回完整本地模型缓冲。
- `docs/windows-performance-verification.md`：RTX 3080、22,480,361 点、1080p/完整 SH3，早期稳定 GPU 帧约 41.5–41.9 ms，后续排序调度约 39.6 ms；解码约 9.675 s，上传约 1.512 s。约 390 万点场景约 7 ms。都是诊断，不是服务器 SLA。
- `docs/large-scene-verification.md` 的早期 300 ms 级 smoke 值不是当前稳定帧性能。GPU 时间、Present 调用间隔、真实显示、冷模型加载分别记录。

### 1.3 必须先修订的契约

1. 现有生产帧禁止排序数组 readback；云端另立“GPU 顺序 gather/pack → 有界 readback”导出路径，仍禁止 CPU 排序。
2. 离屏完成发 `FrameProduced`，客户端实际呈现后才发 `FramePresented`；不复用本地 `SceneReady`。
3. Windows 可允许内存不足时 SH/point_stride 缓解；远端质量基准禁用自动缓解，或声明且授权实际降质。
4. Windows fragment 读取 quality 的 cutoff/max_stddev；Android fragment 目前硬编码 0.0039/半径平方 9。新薄渲染须锁定一致 raster profile，不假定参数都已等价。
5. Windows odd-axis 镜像还有宿主水平镜像语义；请求须声明完整查看变换，不能只传姿态。

## 2. 调研与路线筛选

截至 2026-10-02 核对作者论文/项目和官方文档，不宣称穷尽全部论文。来源见第 14 节。

| 工作 | 真正卸载/优化 | 本项目启示 | 限制 |
| --- | --- | --- | --- |
| 原始 3DGS，SIGGRAPH 2023 [R1] | 非分布式、各向异性 splatting | 投影/SH/透明数学 | 排序/截断不自动等同本仓库，许可须审查 |
| TIGAS，2026-05-09 arXiv v1 [R2] | 后端 rasterization，发 JPEG | 单帧响应、驻留、编码与网络联合优化 | “2D projections”是图像，不是 2D 高斯；Python/browser 路线不同；默认 SH0，SSIM 0.88 不是本项目 SH3 门槛 |
| Nebula，2025-12-23 arXiv v1 [R3] | 云端 LoD 搜索、Gaussians/跨帧更新 | 联合优化中间态与通信 | 端侧仍做 preprocessing/渲染，含专用加速器；多帧收益不等于单帧收益 |
| ECO-GS / IRAC，ICASSP 2026 [R4] | 小本地 GS/远端大 GS 协作 | 按资源约束选择路线 | 本地小模型不符首期无模型；不是本文的二维协议 |
| LightGaussian [R5] | 剪枝、蒸馏、量化，减少存储/部分计算 | 后续资产优化 | 需处理模型/新画质证据，不是透明网络压缩器 |
| WebP/JPEG/Zstandard [R6–R8] | 不同载荷编码 | 按载荷选择 | 官网压缩比不是本项目实测 |

首期没有多帧相干性，不能靠前帧缓存节省初始帧；直接比较**该视角的二维高斯与图像表示**。图像不必必然更小，稀疏场景也可能相反。

| 模式 | 服务器 | 端侧 | 负载规模/定位 |
| --- | --- | --- | --- |
| `ProjectedSplats` | 筛选、SH、投影、排序、gather | Gaussian 光栅化/alpha | O(可见高斯数)，严格切分必做 |
| `ImageFrame` | 再做光栅化、编码 | 解码、纹理绘制 | O(像素/图像熵)，轻客户端基线必做 |
| `HybridTiles` | 完整像素区域选择表示 | 每区域只用一种表示 | 两者及重复引用；前两者完成后验证 |

Hybrid 是待验证方案，不是已经证明的新算法；若不能同画质胜过最好的单模式，就删除自适应复杂度。

## 3. 总体架构与单帧数据流

```text
Windows / Android 原生宿主
    相机 + 视口 + 允许的模式/质量 + 客户端资源上限
                       |
                 TLS 请求（先 HTTP/2）
                       v
鉴权/准入 -> cloud-service -> GPU worker（场景常驻）
                              |
        model-io -> SplatScene -> GPU 3D 属性/SH
                              |
       view/SH/covariance -> 可见集 -> stable radix
                              |
                   GPU 按排序顺序 gather
                    /                     \
          二维高斯量化/pack          离屏光栅化/区域预合成
                    \                     /
                frame-codec + 帧目录 + 有界发送队列
                              |
                    可靠二进制帧响应
                              v
remote-client：校验/解码 -> 待命 RemoteFrame
                              |
thin-render：2D splat 或图像纹理 -> UI 合成 -> Present
                              |
               首次呈现成功后激活，失败保留旧帧
```

### 3.1 服务器单帧管线

1. **预加载/驻留**：解码和 GPU 上传不属于暖请求。场景按内容哈希/version 管理，维护 CPU 恢复快照和 GPU 预算；冷加载单独报告。
2. **请求锁定**：验证 double 相机、完整查看变换、物理像素宽高、质量 profile、deadline 和租户权限，生成不可变 ViewKey。CPU 先减 double worldOrigin，再送相对 float 到 GPU。
3. **投影/着色**：复用现有 `J Σ3 Jᵀ`、特征轴、近面保守处理、半径 cap 和 SH 方向/clamp 规则。服务器执行全部 SH，不发送 SH 系数。
4. **筛选/排序**：第一版保留既有完整候选数组/sentinel，GPU stable radix；后续才实测 stable compaction 先于排序。不能用无序 atomic append 破坏同键原索引顺序。
5. **顺序导出**：GPU `out[k] = projected[sorted[k]]`，只导出有效前缀；端侧不再接收源索引、排序键或对索引重新排序。二维位置直接是最终画布坐标。
6. **分支**：Projected 路径 pack 二维流；Image 路径绘制完整 offscreen target；Hybrid 可先用完整 reference image + 区域高斯列表构造，正确性优先，不一开始实现复杂 selective rasterizer。
7. **readback/编码**：输出缓冲与计数经 fence 后读回，不读回原模型。D3D12 使用 readback buffer、GetCopyableFootprints/row pitch、明确 fence [R9]；Vulkan 对 host-visible memory 处理同步与必要 invalidate。首期允许 GPU→CPU→网络，不能以“native”声称零拷贝。
8. **发送**：manifest 先行、payload 分块；严格全帧模式接收完成后才激活。资源按最后使用 fence 回收，网络背压不得无限积压 GPU 输出。

服务器可复用一个场景供多个请求，但每个在途视角拥有独立 scratch/target/FrameId。首期一 GPU worker 限量串行或少量在途请求，不假设一个 GPU 能无限并发。

部署前在实际服务账号/会话和云 GPU 驱动下做 adapter、WaveOps、排序自检、offscreen/readback 探针；桌面本机成功不等于 Session 0/云 vGPU 成功。解码 helper 的 Job/限额是资源隔离，不是完整恶意输入安全沙箱。

颜色首先保持仓库既有训练/显示编码色值域，不悄悄切成物理 linear/sRGB 光照流程。纹理格式、codec 色彩元数据与 UI 合成空间显式统一；decoder 不得未经约定应用第二次 gamma/ICC 转换。opaque 背景在云端按约定完成，透明帧保留独立 alpha。

### 3.2 薄客户端应当不做的工作

不解码 SPZ，不驻留三维位置/旋转/SH，不生成协方差，不做可见筛选和排序。只验证帧、解压，上传二维记录或纹理，用已有风格的 quad/fragment 完成最后步骤。

Projected 使用网络顺序直接 instanced draw，关闭深度写入，远到近预乘 `ONE / INV_SRC_ALPHA`。跨 chunk 按全局 rank 顺序绘制，不能按完成解压的先后提交；等深度稳定性在云端解决。

Image 使用 opaque RGB 时只做纹理替换；透明 RGBA 时使用正确的预乘语义。一次成功绘制后进入 idle，Surface 重建使用当前帧缓存或重取，不能自动下载模型。

## 4. 二维高斯编码：先无损闭环，再有界量化

### 4.1 保真 profile：`ProjectedFloat40`

显式小端序序列化，不依赖 C++ padding 或 Vulkan std430：

| 字段 | 大小 | 语义 |
| --- | ---: | --- |
| center_x/y | 8 B | float32，画布左上为原点，x 向右、y 向下 |
| axis0_x/y + axis1_x/y | 16 B | float32，已包含服务器实际截断/半径 cap 的 quad 轴 |
| color_r/g/b + opacity | 16 B | float32，SH 后颜色与峰值 opacity；不是逐像素已预乘颜色 |
| 合计 | 40 B | 数组次序即最终 draw order |

不发 depth：本阶段不在客户端做新相机排序、mesh 遮挡或重投影。raster profile 在 header 中记录 stddev、alpha cutoff、alpha cap、颜色域及显示镜像，实际 fragment 用相同规则求 Gaussian 和预乘。

Float40 的“无损”指相对服务器导出数值无额外量化，不保证跨厂商浮点光栅化逐位一致；画质用逐像素夹具与容差验收。

### 4.2 候选量化 profile：`ProjectedQuant20`

| 字段 | 大小 | 建议编码 |
| --- | ---: | --- |
| center_x/y | 4 B | 两个 int16，chunk 原点 + 固定/显式步长；候选 1/16 px |
| major/minor radius | 4 B | 两个 uint16，对已截断轴长做有界 log 量化 |
| orientation | 2 B | uint16，周期 π 的椭圆方向 |
| RGB + peak opacity | 8 B | 四个 UNORM16，首期不急于降到 RGBA8 |
| flags | 2 B | 保留位、异常标记，未知 mandatory flag 拒绝 |
| 合计 | 20 B | 不含 chunk 原点/尺度/目录等 overhead |

这是提案，不是既有格式。1/16 px 量化的中心最大误差为 1/32 px，但**参数误差不能直接推出最终图像误差**：细长椭圆、高不透明度和深层透明叠加会放大误差。

端侧重建两根正交轴；等特征值、axis 符号、量化后最小正半径有唯一约定。超出 center/radius 范围、极细高斯或质量不达标的 chunk 回退 Float40；禁止截断坐标或扩大 opacity。以 chunk profile 实现例外，不假定所有数据都能放进 20 B。

### 4.3 通用压缩流水线

`GPU gather/pack -> chunk -> SoA channel shuffle -> 可选有界差分 -> Zstd -> wire`。

- 利用现有 pinned zstd 1.5.6，先比较 level 1/3、raw 和可选 LZ4；新增依赖需许可与版本锁定。低级别压缩是起点，不预设最优。
- 可按 32/64/128 KiB 未压缩 chunk 比较；Projected chunk 必须对应连续 global-rank 区间。
- 按通道排列不改变逻辑顺序；不为改善熵而把整帧高斯 Morton 重排，否则 alpha 合成被改变。
- 深度次序下相邻点不一定空间相邻，中心差分不一定有效；记录每种变换的净收益和解码代价。
- compressed 比 raw 大时发 raw。字典仅在显式 dictionary id/hash 已可获得时使用，首帧账本包含下载成本；首期不依赖训练字典。
- 不将已 JPEG/WebP 编码的图像再统一 Zstd；不把 SPZ 输入压缩比当成二维帧压缩比。
- CPU 解压到有上限的 staging；随后 GPU 小型 unpack 或直接读取 packed records。最终是否扩展成 40/48 B 缓冲以实际 GPU 时间/峰值决定，而不是宣称 packed wire 等于 GPU footprint。

### 4.4 可见性与贡献裁剪的边界

视锥外、支撑完全在画布外、已满足既有 cutoff 的高斯可以按锁定规则剔除。**不能像 opaque mesh 一样丢弃“被前方高斯遮挡”的所有后方点**；3DGS 是透明叠加。front-to-back transmittance early stop 或 contribution pruning 要新增误差阈值与证据，不能混入无损模式。

首期不做任意 stride 抽点、SH 降阶或大高斯拆分/合并。模型 LoD/剪枝作为后续资产层提案，不以改变模型掩盖传输方案的瓶颈。

## 5. 图像与混合瓦片：控制端侧 overdraw

### 5.1 单帧图像 codec

- 保真基线：raw RGBA 与 PNG/WebP lossless。用于判断编码损失，不承诺尺寸小。
- 低延迟候选：libjpeg-turbo，优先比较 JPEG 4:4:4 与 4:2:0；后者容易损伤彩色细线，须图像检查 [R7]。JPEG 不含 alpha，只有明确背景的 opaque 模式可直接使用。
- 透明 UI：WebP RGBA，或 RGB + 无损 alpha 双平面。明确 straight/premultiplied 变换；不对接近零 alpha 的颜色随意做除法。透明低 alpha 夹具必测。
- WebP lossy/lossless 与 JPEG 做同画质、编码/解码墙钟、输出字节比较 [R6]。首期不默认 AVIF 或视频 NVENC：单帧无时间压缩收益，硬件支持和初始化开销需独立测量。
- WebP 保真夹具若要求包括全透明像素 RGB 的数值保留，需显式 exact 设置；默认 lossless API 可能不保留不可见 RGB。是否只验可见合成结果应在质量契约中明确。
- 移动端不保证 JPEG/WebP 一定硬件解码；采用平台解码器或 C++ codec 后，按具体设备测 CPU/内存。不要把一个平台 API 名称当成硬件能力。

### 5.2 为什么不能简单把高斯拆成“背景图 + 前景点”

预乘层的组合为 `C = C_front + T_front * C_back`、`T = T_front * T_back`。它可以结合，但不能交换。若不同表示的高斯在深度上交错，先把任意一组压成背景图再盖点会破坏次序。

安全选择有两类：按排序中的连续区间预合成，再按原顺序合成；或按**最终像素区域**划分，每个区域计算全部对它有贡献的高斯。前者可能产生多层图像、增加带宽；首期 Hybrid 选择后者。

### 5.3 `HybridTiles` 候选设计

1. 尝试 64/128/256 px tile。画布被不重叠整数像素 rect 完整覆盖，每像素只有一个 owner。
2. 投影椭圆支撑跨 tile 时，加入每个相交区域。GPU 生成 `(tile_id, global_rank)` 引用，按 rank 稳定排序或从全局排序序列稳定构造列表，保留全帧混合次序。
3. image tile 包含**全部影响该 rect 的高斯**最终结果；splat tile 包含同一全集，draw 使用 scissor 到 owner rect。不能只把中心落在 tile 内的点加入列表。
4. image 最简单从完整 reference image 裁切；首期 tile 纹理按 pixel-exact/nearest 显示。若引入线性过滤或放缩，需全帧一致的采样、halo/UV 和 codec 对齐策略，避免拼缝；不可把压缩后边缘当真空边界。
5. 初版允许重复二维记录，显式计入复制因子；大高斯跨许多 tile 时可能很差。后续候选为全帧记录池 + tile 有序引用、或直接切回 ImageFrame；不预设 tile 必优。
6. image tile 的像素结果直接写入最终帧对应区域，不再覆盖绘制该区域的 splat；不出现重复 alpha。

### 5.4 按全局预算选择模式

候选估算 `bytes_tile`、`encode_ms`、`decode_ms`、`upload_ms`、`client_draw_ms` 和画质误差。在客户端总预算、payload 上限、deadline 和质量约束下选择。不能独立贪心选每 tile 最小字节，忽略共享纹理、重复点、draw-call 与 encode 开销。

初版用明确阈值/已标定成本表，无 ML 决策器。各帧响应声明每区域的表示与真实质量。若纯图像是最优可选择它；若产品不允许图像，拒绝超预算或返回明确可接受的质量协商请求，不静默变更切分点。

## 6. 请求、帧格式与生命周期契约草案

本节仅定义待评审语义，不是已冻结 wire layout。正式版本由 `remote-frame-types`、`frame-codec`、`remote-transport` 提供者分别落规格。

### 6.1 请求内容

| 类别 | 必需信息 |
| --- | --- |
| 身份 | 协议版本、RequestId、幂等键、scene_id + 不可变 scene_version、租户权限 |
| 视角 | double position/orientation、FOV、near/far、world/view 变换、镜像、相机约定版本 |
| 目标 | 物理宽高、显示方向、背景/透明模式、颜色 profile |
| 质量 | sort mode、SH cap、stddev、blur、radius cap、alpha cutoff/cap；是否允许量化/显式分辨率协商 |
| 能力 | supported frame/codec profiles、最大下载/解压/GPU 字节、最大 splat/tile 数、客户端时间预算 |
| 时效 | deadline、可用吞吐估计、是否允许 preview、请求取消关联 |

控制 JSON 可用于首期调试，64 位 ID 用字符串避免数值解析损失；帧数据必须二进制。场景摘要提供 bounds/初始 fit 相机，客户端不需要模型来选择固定视角。

### 6.2 响应层次

1. `FrameHeader`：magic、major/minor、header_length、RequestId/FrameId、scene_version、ViewKey、width/height、实际 quality/profile、mode、chunk_count、压缩/解压总字节。
2. `FrameDirectory`：每 chunk 的 ID、codec/profile、payload offset/length、解压上限、校验码；Projected 带 global-rank 区间，tile 带唯一 rect/表示/引用范围。
3. `FramePayload`：独立 raw/Zstd records 或图像 payload；所有必需数据来自本响应，不依赖上一帧。
4. `FrameEnd`：完整性摘要和服务器阶段统计；防止把半帧当成成功。

通用整数采用小端序，长度/偏移用受检 64 位算术。mandatory 未知版本/字段/codec 拒绝；可跳过扩展由 minor 规则明确。CRC 用于错误检测，不代替 TLS 认证。目录、长度、解压输出、rank 覆盖/重叠、tile 覆盖、正半径和 finite 值全部校验；限制图像解码像素数与 workspace，防压缩炸弹。

不要把 GPU struct、COM/Vulkan handle、SceneHandle、span 或 STL 对象放入网络 ABI；不得接收客户端 shader 或任意服务器文件路径。

### 6.3 状态机与旧帧事务

```text
Idle -> Requesting -> Receiving -> Validating/Decoding
     -> Uploading -> FirstPresentPending -> Ready(FrameId)
             任意失败/取消 -> 本次终态，保留旧活动帧
```

- 网络帧携带 RequestId、FrameId、ViewKey；Surface 有独立 generation/viewport revision。不能把服务器 FrameId 当 SurfaceGeneration。
- 首期严格模式只有完整验证、GPU 上传完成、首次成功 Present 后才能激活。Surface 0x0 时可保存有界待命帧，不发 Ready。
- resize 后旧分辨率结果标记过期；如允许临时缩放旧帧，必须声明，不称为新视口的正确渲染结果。
- Surface 重建可以重传当前二维帧/纹理；device loss 不等待失效 fence。采用既有有限恢复、异步 shutdown、fence 后释放规则。
- 取消尚未提交的 GPU 任务可删除；已提交任务仅停止后续编码/发送，在安全点回收，不能保证中断任意已运行 shader。
- 网络进度是已接收/已知总字节；服务器排队/投影阶段只显示阶段，不捏造百分比。每请求恰好一个逻辑终态。

### 6.4 preview 的限制

默认不启用。若需要，低分辨率独立 preview 与 final 分开 FrameId/质量标签，final 到达时替换而不是 alpha 叠加。普通 depth chunk 不是可随意乱序显示的渐进 refinement；不能多次累计绘制同一高斯。按瓦片部分显示也要单独定义 partial 状态，不把它计入完整首帧时延。

## 7. 传输、缓存与服务调度

### 7.1 首期传输建议

优先可靠 HTTPS 请求响应，不自研 UDP/FEC，不引入 WebRTC 会话或视频容器。一个固定帧的完整性比连续交互丢弃旧帧更重要。

建议拓扑：客户端 HTTP/2 + TLS 到网关，网关转到 C++ worker 的 HTTP/1.1 二进制接口。Boost.Beast 只承担 HTTP/1.1，不冒称它原生支持 HTTP/2；亦可后续选择独立 HTTP/2 adapter。首期本机闭环可以直接 worker，但正式外网不能省略 TLS/鉴权。

候选 API 为 `GET /v1/scenes/{id}/metadata`、`POST /v1/render-frame`、取消接口及健康/诊断接口；目前这些 endpoint 不存在。首期 POST 可直接流式返回一帧，不额外强制“提交任务→轮询→再次下载”的多 RTT 流程。请求关闭与显式取消配合，deadline/幂等键避免重试重复消耗 GPU。

### 7.2 HTTP/3/QUIC 是后续 A/B 项，而不是带宽药方

QUIC 的可靠有序 stream 允许跨 stream 避免传输层 HoL；同一 stream 缺失字节仍等待重传，HTTP/3 也有控制/QPACK 等依赖 [R10–R11]。

一帧装在一个 response stream 时，不会因为“用了 QUIC”就摆脱该帧的重传等待。只有独立 tile/preview stream 才能减少互相阻塞；若验收要求等齐全部 tile，完整帧仍受最迟必需数据约束。它也不会改变 `8 * bytes / goodput` 下界。

HTTP/3 可由网关/成熟 client stack 终止；MsQuic 是 QUIC 库，不是直接可替代 HTTP server 的完整 HTTP/3 框架 [R12]。测试 UDP 被禁时的 HTTP/2 fallback。昂贵渲染请求首期禁用 0-RTT，避免 replay 放大。吞吐测试须包含重传、握手和冷拥塞窗口；已有空闲连接也不等于已经跑满链路。

### 7.3 有界调度与缓存

- 以场景/租户为键做 CPU/GPU residency；按实际预算和恢复副本收费，设置 LRU、引用计数和 eviction 安全点。显存不足拒绝或显式切质量，不能超卖后静默抽点。
- GPU worker 限制排队请求、在途 scratch 和输出字节。编码线程池与网络队列有界；一慢客户端不能锁住所有 worker。
- 缓存键包含租户/授权、scene 内容版本、完整相机/镜像、物理分辨率、实际质量、颜色/alpha、codec 参数、backend/shader 版本；不能只按 camera pose 缓存。
- 不为提高命中率偷偷量化相机；canonicalization 只消除明确定义的等价表示，NaN/Inf 拒绝。视角变化已超出首期需求。
- 明确分组：模型暖/帧缓存未命中；精确帧缓存命中；冷模型；冷连接。只拿缓存命中宣传计算性能不可接受。
- 模型和投影态可能泄露内容；TLS 不等于 DRM。禁止跨租户缓存混用，日志不记录完整模型、完整路径或帧像素。

## 8. 技术栈取舍

| 层 | 首期建议 | 理由/注意 |
| --- | --- | --- |
| 共享核心 | C++20、版本化 plain value/binary contracts | 延续两平台工程，不承诺跨编译器 STL ABI |
| GPU 服务 | Windows x64 / D3D12 / HLSL SM6 / 既有 DXC | 复用当前证据最多；新增 OffscreenTarget、导出/readback |
| 服务宿主 | 无 WinUI 的 C++ worker、服务/console 生命周期 | 不创建隐形桌面窗口或 fake swapchain 冒充 headless |
| 网络 | Beast/Asio HTTP/1.1 worker + TLS/HTTP2 gateway | 图形核心不依赖 HTTP；网关可以独立部署 |
| Windows 客户端 | WinHTTP 网络适配 + C++ codec + D3D12 thin-render | 与 WinUI surface 规则协调；网络线程不阻塞 UI |
| Android 客户端 | OkHttp 网络适配 + Kotlin/JNI + C++ codec + Vulkan thin-render | 复用现有 SurfaceView/C ABI；不复用模型 Service 下载帧 |
| 高斯编码 | 现有 pinned zstd 1.5.6；raw 对照 | 新 LZ4 仅在净收益明确后加入 |
| 图像编码 | libjpeg-turbo、libwebp 候选 | 新版本/许可证先锁定；分别测编码与解码 |
| shader | 服务器 HLSL；客户端 Windows HLSL、Android GLSL/SPIR-V | 共享数值契约/夹具，不冒称二进制可跨 API 复用 |
| 测试 | GoogleTest、Android 端侧 tests、PIX/PresentMon、Perfetto | 网络模拟/codec fuzz 与图像回归另加，不将性能断言塞普通单测 |

保留当前 Windows CMake 4.3+/VS 2026/SDK 10.0.26100.0 与 Android JDK/Gradle/NDK 既有锁定链。新增 codec/network/gateway 版本尚未选定，不声称已安装或生产验证。

### 8.1 Linux 服务器演进

Linux + Vulkan 可作为第二服务 backend，但不是现有 Android renderer 的零成本搬运：需隔离 ANativeWindow、Surface 和 Android 日志/JNI，增加无 swapchain 的 color target、memory/readback 和设备恢复；移植 FD 解码及受限 helper，测试 SH/近面/排序/颜色一致。

Vulkan 纯 offscreen 渲染不要求创建展示 Surface；`VK_EXT_headless_surface` 仅在确实需要 headless presentation 时评估，不作为导出像素的必需条件 [R13]。若云厂商仅提供 Linux，可调换首期 backend，但须重新建立证据。初版不要求 CUDA、NVENC 或 GPU Direct；可选 vendor encoder 必须与 portable 基线 A/B。

薄渲染器要有独立设备能力探针：ImageFrame/已排序高斯绘制本身不需要 radix/WaveOps/subgroup，不应启动完整 renderer 的排序自检或分配场景 scratch。首期沿用既有平台支持范围；将较弱设备纳入支持需单独验证，不能凭 Android API 版本保证兼容。

## 9. 单帧时延、带宽下界与内存预算

### 9.1 测量定义

完整单帧时延从客户端发起请求到目标帧实际显示：

`T_full ≈ T_connection + RTT + T_queue + T_gpu + T_readback + T_encode + 8*S/goodput + T_decode + T_upload + T_draw + T_display`。

这是非重叠预算模型，不是测量公式。分块编码/传输可重叠后，要以事件时间线的关键路径统计，不能把所有阶段求和后再任意减掉一项。goodput 是有效应用吞吐，不是链路标称速率；wire bytes 另计协议/重传。

`T_first_byte`、preview 首次显示、完整 final 显示是不同指标。Present 返回/帧提交不等于屏幕已亮：Windows 用 PresentMon，Android 用 Perfetto/SurfaceFlinger；硬件显示证据不足时明确只报告 submit/present-call。

### 9.2 只发送投影高斯为何可能失败

以下为理论传输时间，十进制 Mbit/s，不含 RTT、计算、codec、协议、丢包和握手；KiB/MiB 使用二进制单位。

| 应用 payload | 字节 | 1 Mbit/s | 5 Mbit/s | 10 Mbit/s |
| --- | ---: | ---: | ---: | ---: |
| 候选压缩图像 300 KiB | 307,200 | 2.458 s | 0.492 s | 0.246 s |
| 候选压缩图像 1 MiB | 1,048,576 | 8.389 s | 1.678 s | 0.839 s |
| 5 万点 × Quant20 | 1,000,000 | 8 s | 1.6 s | 0.8 s |
| 100 万点 × Quant20 | 20,000,000 | 160 s | 32 s | 16 s |
| 100 万点 × Float40 | 40,000,000 | 320 s | 64 s | 32 s |
| 1920×1080 raw RGBA8 | 8,294,400 | 66.355 s | 13.271 s | 6.636 s |

300 KiB 和 1 MiB 是**假设输出大小**，不是编码后实测，也不保证 SSIM。Quant20 尚未计目录或熵压缩。即使假设 100 万点 Quant20 熵压缩达到 10:1，仍约 2 MB、5 Mbit/s 需要 3.2 s；10:1 本身未验证。

要把 20 MB 缩到 300 KiB，需要约 65:1 总缩减。仅改 float→16 bit 不足以保证这一点。可见点明显少于总点时会更好；但不能从“看一个视角”推定所有场景的可见点都很少。

### 9.3 候选暖请求预算

条件：模型已驻留、帧缓存未命中、连接已建立；有效下行 5 Mbit/s、RTT 40 ms、1080p。建议先争取以下预算，全部待测：

| 阶段 | 建议预算 |
| --- | ---: |
| RTT | 40 ms |
| 排队 | ≤ 10 ms |
| GPU + readback + encode | ≤ 60 ms |
| 300 KiB 传输下界 | 491.52 ms |
| decode + upload + draw | ≤ 25 ms |
| display 等待示例 | ≤ 16.7 ms |
| 合计理想预算 | 约 643 ms |

因此提议完整帧 p95 ≤ 650 ms 与 payload p95 ≤ 300 KiB，但余量很紧，复杂场景或弱设备可能失败；必须共同满足画质，不能报“保证”。1 Mbit/s 下同大小已经至少 2.46 s，不承诺 1080p 亚秒。

若允许明确标识的 720p 节流档，可另测 96 KiB：1 Mbit/s 仅传输约 786 ms，尝试完整帧 1 s；它不是 1080p 等画质验收。若要求 1 Mbit/s、1080p、高画质、100 ms 同时满足，先承认预算冲突，不用协议名称掩盖物理下界。

### 9.4 内存与客户端工作量

端侧预算必须计 compressed 接收、解压工作区、未激活帧、活动帧、上传页、GPU records/纹理、swapchain 和 Surface 重建双份资源。

1080p RGBA8 一张纹理约 7.91 MiB；两张活动/待命纹理约 15.82 MiB，但不代表应用总占用只有这些。Quant20 若全部展开成 Android Ellipse48，100 万点仍约 45.8 MiB，另加网络/解压/上传与 draw overdraw。

建议首期轻客户端 profile 的**远程子系统增量峰值**≤64 MiB，不含既有应用运行时但包含归属本功能的 CPU/GPU/swapchain；提交 GPU draw ≤8 ms 为实验目标，不能仅按 splat 个数保证。Projected profile 可有更高显式上限，但不得标成已满足最低资源档。

SH3 场景仅规范化三维属性/SH 就约 236 B/点，尚不含排序和投影 scratch。服务器按实际资源布局和动态预算准入；一个模型可共享，但并发视角的临时缓冲不能免费。按模型/请求拆 residency 与 scratch 池，限制在途数量。

## 10. 实验矩阵、质量门槛与退出条件

### 10.1 样本与公平参照

用父工作区已有 SPZ/PLY，小/中/大集合至少包含约 118 万、188 万、267 万、391 万与 2248 万点代表样本；记录源文件 SHA-256、规范化点数/SH、相机 double 数值、实际可见点数、颜色/镜像、shader/backend/codec 版本。

每场景选多个**各自固定**视角：全景、近距离高 overdraw、稀疏外缘、细线/透明、近面相交。仍是每请求一固定帧，不研究实时相机控制。服务器不量化相机、不抽点、不降低 SH 的完整质量离屏图像作为参照；不能把投影态数量当作最终贡献点数。

合成夹具包含 SH0–3、同键稳定排序、远原点、细长椭圆、大半径、alpha 极值、重叠层、跨 tile 支撑、viewport 边缘、镜像/竖屏/色条。对已有 Android/Windows fragment 参数差异先锁定基线，不修改旧实现后悄悄改 reference。

### 10.2 逐项对照

| 比较 | 目的 |
| --- | --- |
| 云端完整 reference vs Float40 → 两平台 thin-render | 证明切分/顺序/颜色正确，不混入有损压缩 |
| Float40 raw vs Float40 Zstd | 纯熵编码的字节、encode/decode 和净时延 |
| Float40 vs Quant20，再比较 RGBA16/8 | 找到量化误差与带宽拐点 |
| PNG/WebP lossless/JPEG 444/420/WebP lossy | 在质量约束下比较图像净成本 |
| 最佳 Projected / 最佳 Image / Hybrid | 验证混合方案是否真有优势 |
| chunk 32/64/128 KiB，tile 64/128/256 px | 头部、并行、压缩、重复高斯与 draw-call 权衡 |
| 稳定 compaction 开/关 | 额外 pass 是否净减少排序/导出 |
| HTTP/2 vs HTTP/3，冷/暖连接，0/0.1/1% loss | 只按真实单帧完整时延评估网络栈 |

网络控制 1/5/10 Mbit/s 与 RTT 20/40/80 ms；先用受控链路/网关 shaping，再测真实 WAN。每配置至少 30 次独立单帧请求，报告 p50/p95、样本数和离群原因，不用三次平均伪装可靠 p95。模型暖但帧缓存关闭是主结果，精确缓存命中另列。

### 10.3 待确认的成功标准

1. Float40 相对 reference 通过合成逐像素容差、透明/排序/颜色测试；真实场景建议 SSIM ≥0.99，容差须按实际跨 GPU 基线确定，不宣称 bit-identical。
2. 有损 Quant/Image/Hybrid 在相同最终分辨率下：每个受测视角建议 SSIM ≥0.95 且 PSNR ≥35 dB，另检查细线、透明边缘、ROI 误差和人工截图。透明 RGBA 分别比较 RGB/alpha，必要时统一背景再比较；全局 SSIM 高也不能掩盖缺块。
3. 暖模型、帧缓存关闭、5 Mbit/s/40 ms/1080p：payload p95 ≤300 KiB、完整显示 p95 ≤650 ms；轻客户端远程增量峰值≤64 MiB。都只是提议，数据不足或失败必须原样记录。
4. 端侧 tracing 无模型解码、SH、投影、radix 排序 pass；网络结束后不持续重绘。GPU draw/decode 能耗单独报告，不能只以 FPS 表示轻量化。
5. 取消、超时、损坏长度/压缩流、乱序/缺 chunk、未知版本、旧 ViewKey、resize/后台/Surface loss、GPU OOM/device loss 均保持确定终态，保留旧活动帧；重复 100 次无资源持续累积。
6. Windows RTX 3080 与 Android Adreno 750 只能作为已有平台起点；最低性能目标机与 Mali/其他 GPU 无证据时标未验证，不写全设备支持。

复杂真实场景若无法同时达成质量和大小，则报告该场景的 Pareto 曲线，并请用户决定质量/分辨率/时延优先级；不降低门槛后宣称通过。

### 10.4 证据输出

独立 benchmark CSV/JSON：RequestId/FrameId、scene/camera/shader/codec hashes、每阶段本机 monotonic 时间、服务排队、GPU pass 时间、readback、编码/解码、application/wire/重传字节、初始/实际分辨率、candidate/visible/transmitted 数、duplicate factor、CPU/GPU 峰值、真实显示证据、质量分数和误差热图。

跨服务器/手机的 monotonic clocks 不可直接相减。客户端记录 end-to-end，服务器返回自身阶段 duration；需要跨机绝对时间线时另做时钟同步并记录误差。frame stats 不包含模型或像素内容，低频汇总而非逐帧同步写日志。

## 11. 研究检查点：不是已批准实施计划

能力图批准后，按提供者依赖逐个写 SPEC，再审技术计划和任务；下列只说明需要取得哪些证据。

| 检查点 | 要证明的内容 | 未通过的处理 |
| --- | --- | --- |
| C0：契约与无网络闭环 | 固定相机云端 Float40 导出、Windows/Android 薄渲染与 reference 对齐 | 不进入压缩收益讨论，先修坐标/颜色/次序 |
| C1：图像竞争基线 | 相同 reference 的图像编码/解码/纹理呈现，冷暖链路时延 | 如图像已经最好，接受“低端模式是图像”这一事实 |
| C2：二维压缩 | Quant20、SoA、Zstd 的净收益及错误热图 | 保留 Float40，拒绝不达标的量化，不宣传压缩倍数 |
| C3：可靠单帧网络 | 帧完整性、取消、backpressure、Surface 事务、真实 WAN | 不优化连续视角；定位队列、readback、编码或链路瓶颈 |
| C4：混合表示实验 | Hybrid 相对最佳单模式的 Pareto 改善及 tile 边缘正确性 | 无优势则不纳入首期，避免架构过度设计 |
| C5：资源受限设备 | 目标低端机内存、decode/draw、温度/能耗与恢复 | 不能用 Adreno 750 的通过替代目标低端机 |

初版最有价值的可运行切片是：**一个模型、一个相机、一个 Windows GPU worker、一个 Android 客户端，先 Float40，再单帧图像，进行同质量 A/B**。这不是要求两条路线同时生产化；而是用证据确定该场景应该切在哪一层。

## 12. 工程落点与现有命令

### 12.1 建议目录，不是已经创建的模块

```text
include/remote-frame-types/        平台无关请求/帧值类型
include/frame-codec/               codec 提供者边界
include/remote-transport/          传输适配边界
include/cloud-render/             offscreen/export 提供者边界
include/thin-render/              非本地场景的轻量渲染边界
src/cloud-render/d3d12/            第一服务器 backend
src/cloud-render/vulkan/           Linux 后续 backend
src/frame-codec/                   binary/quant/Zstd/image adapters
src/cloud-service/                 worker/准入/缓存/诊断
src/remote-client/                 与 UI 无关的帧事务
src/thin-render/d3d12/             Windows thin backend
ForAndroid/src/thin-render/        Android Vulkan thin backend
ForAndroid/src/android-bridge/     remote C ABI/JNI 适配
tests/remote-frame,frame-codec/     格式/损坏输入/数学
tests/cloud-render,thin-render/    GPU 导出/图片/生命周期
tests/remote-client/               取消/迟到结果/Surface 事务
bench/remote-frame/                独立固定帧网络/codec/资源基准
```

不重构既有全部 engine，不移动已发布 SDK 头，不让 render-core 引用 HTTP/文件解码器；优先增加 target/export adapter 和独立 thin-render。需要公共契约变化时先改 `SPEC-render-core.md` / Android 对应提供者规格，另加 remote 模块规格。文档布局遵循现有 `docs/` 与模块 ID，地图批准后由地图索引正式规格。

### 12.2 当前已有的基线命令

以下命令来自既有工程，用于未来实现后维护回归；本次文档工作没有运行构建/GPU/网络实验，也没有生成尚不存在的服务 executable 命令。

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' .\Native3DGSViewer.sln /restore /m /p:Configuration=Release /p:Platform=x64
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe' .\out\Release\Native3DGSViewer.Tests.dll /Platform:x64
ctest --test-dir out/cmake -C Release --output-on-failure
& .\ForAndroid\gradlew.bat -p ForAndroid :sdk:assembleDebug :sdk:assembleRelease :GUI:assembleDebug :GUI:assembleRelease
```

codec fuzz/离屏导出/网络集成命令待正式构建目标存在后写进提供者规格。不要让读者把候选 API 或 benchmark 名称当成当前可执行功能。

## 13. 决策、风险与待用户确认事项

| 决策 | 当前建议 | 原因/风险 |
| --- | --- | --- |
| 是否传完整模型 | 不传 | 端侧资源按当前帧而非三维模型增长 |
| 是否只保留二维高斯路线 | 不作为全设备唯一模式 | 高斯流和 overdraw 都可能超预算 |
| 是否以图像替代严格泼溅研究 | 不替代，二者必做对照 | 明确技术目标，不偷换需求 |
| 是否立即做 Hybrid | 前两条闭环测完再决定 | 重复引用、draw call/编码 overhead 可能反而更差 |
| 云端 API | 先 D3D12、后 headless Vulkan | 最小复用风险；Linux 首发会改变首期成本 |
| 压缩强度 | 无损 + 有界量化 + 图像同质量比较 | 不用 codec 名称代替质量/时延数据 |
| 网络 | 先 HTTPS/HTTP2 网关，H3 A/B 后引入 | 固定单帧不天然受益于跨帧去 HoL |
| 是否做连续交互 | 否 | 预测、delta、重投影/遮挡属于下一项规格 |

需要确认的三个产品输入：

1. **终端最终渲染是否必须包含 Gaussian 光栅化？还是允许服务器对密集区域预合成图像，终端只做纹理/区域合成？**这是架构分叉，不能由实现者默默决定。
2. 目标最低设备、最终分辨率、允许吞吐与完整首帧时延，以及图像质量优先级；本文数值均是建议预算。
3. GPU 服务器是否允许 Windows，还是必须 Linux；以及模型是否预驻留。模型冷加载的秒级成本与单帧传输不能混成一个指标。

本文建议始终执行：边界校验、明确质量、有限预算、fence 后释放、旧帧事务、真实显示证据。需先确认：模式/画质让步、公共 ABI、依赖升级、Linux 首发。禁止：静默抽点/降 SH、CPU 排序回退、跨租户缓存、无限排队/重试、把理论表格或他人论文结果当成本项目实测。

## 14. 原始来源与核对范围

论文按明确版本/题名识别，官方库的动态页面只用于能力核对，不作为最终依赖 pin。外部资料无本仓库配置下的性能保证；所有方案与数值预算是本项目推导。

- **[R1]** Kerbl, Kopanas, Leimkühler, Drettakis, *3D Gaussian Splatting for Real-Time Radiance Field Rendering*, SIGGRAPH 2023：[作者项目](https://repo-sam.inria.fr/fungraph/3d-gaussian-splatting/)。用于数学/路线依据，不据此宣称原始代码可任意商用。
- **[R2]** Artioli 等，*Thin-Client Interactive Gaussian Adaptive Streaming over HTTP/3*，2026-05-09，arXiv:2605.08699v1：[摘要](https://arxiv.org/abs/2605.08699v1)、[正文](https://arxiv.org/html/2605.08699v1)。核对单帧 JPEG、PyTorch/ASGI/aioquic、默认 SH0、质量统计；“2D projections”不得误读为 primitive stream。
- **[R3]** Zhu 等，*Nebula: Enable City-Scale 3D Gaussian Splatting in Virtual Reality via Collaborative Rendering and Accelerated Stereo Rasterization*，2025-12-23，arXiv:2512.20495v1：[作者预印本](https://arxiv.org/abs/2512.20495v1)。依据公开摘要的 LoD/时序/立体 raster 路线；不引用其带宽倍数作为本方案证据。
- **[R4]** Wan 等，*Edge Collaborative Gaussian Splatting with Integrated Rendering and Communication*：[作者预印本 arXiv:2510.22718](https://arxiv.org/abs/2510.22718)、[ICASSP 2026 DOI](https://doi.org/10.1109/ICASSP55912.2026.11465042)。核对公开摘要/作者元数据；非本文协议算法的复现依据。
- **[R5]** Fan 等，*LightGaussian: Unbounded 3D Gaussian Compression with 15x Reduction and 200+ FPS*，NeurIPS 2024：[作者预印本](https://arxiv.org/abs/2311.17245)。只引用剪枝/蒸馏/量化路线，不将标题指标当成本工程结果。
- **[R6]** WebM Project，[libwebp 官方 README](https://github.com/webmproject/libwebp/blob/main/README.md)、[API 说明](https://github.com/webmproject/libwebp/blob/main/doc/api.md)。Google 格式说明直连曾超时，使用作者维护的库文档核对。
- **[R7]** [libjpeg-turbo 官方 README](https://github.com/libjpeg-turbo/libjpeg-turbo/blob/main/README.md)。核对 SIMD codec、内存 API、pixel ordering/opaque alpha 等；应用采样策略需要自己的图像证据。
- **[R8]** Meta，[Zstandard 官方 README](https://github.com/facebook/zstd/blob/dev/README.md)。本工程实际依赖仍以 `third_party/README.md` 中的 1.5.6 revision 为准，不自动升级到 upstream dev。
- **[R9]** Microsoft Learn，[Read back data via a buffer](https://learn.microsoft.com/en-us/windows/win32/direct3d12/readback-data-using-heaps)。readback heap/fence/Map 语义；纹理 row pitch 还需遵循 copy footprint 契约。
- **[R10]** IETF，[RFC 9000: QUIC](https://www.rfc-editor.org/rfc/rfc9000.html)。可靠 stream、拥塞/流量控制、握手与同流有序交付。
- **[R11]** IETF，[RFC 9114: HTTP/3](https://www.rfc-editor.org/rfc/rfc9114.html)。HTTP 到 QUIC 的映射、独立请求 stream 与控制依赖。
- **[R12]** Microsoft，[MsQuic 官方 README](https://github.com/microsoft/msquic/blob/main/README.md)。区分 transport library 与 HTTP/3 应用栈。
- **[R13]** Khronos，[VK_EXT_headless_surface 官方规格源](https://github.com/KhronosGroup/Vulkan-Docs/blob/main/appendices/VK_EXT_headless_surface.adoc)、[Vulkan Samples](https://github.com/KhronosGroup/Vulkan-Samples)。refpage 直连有访问限制，核对官方仓库规格源；该扩展的 swapchain present 默认 no-op，不等于自动获得网络渲染/像素导出。
- **[R14]** Boost，[Beast Introduction](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/introduction.html)。核对其 HTTP/1/WebSocket 边界，HTTP/2/3 交由独立适配/网关。

仓库对应证据入口：[Windows 性能验证](windows-performance-verification.md)、[Windows render-core 规格](SPEC-render-core.md)、[engine/SDK 规格](SPEC-engine-sdk.md)、[Android 系统设计](../ForAndroid/docs/system-technical-design.md)、[Adreno 750 性能与兼容记录](../ForAndroid/docs/verification/device-performance-2026-09-30-adreno750.md)。
