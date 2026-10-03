# 云端 + Windows 薄端架构

日期：2026-10-02。设计基于仓库的 technical-development-plan、system-technical-design、model-io/render-core/engine SDK 规格，以及 Android/Windows 既有性能证据。旧 RFC 的 Windows-first 候选被用户此次 Linux/NVIDIA 首发要求替代；本目录是独立研究工程，不修改 SDK 契约。

## 1. 工程切分

```text
管理员 PLY/SPZ -> model-io probe/normalize/decode -> immutable SceneHandle
                                                    |
Linux gs-server   CUDA scene residency -> SH + covariance + projection
                    -> CUB stable radix -> gather (far-to-near)
                         |                         |
                   ProjectedSplats             ImageFrame
                   f32/f16/q20/q16             tile count / prefix
                         |                    tile/rank stable sort
                    quantize + Zstd           pixel compositing
                         |                    RGBA / JPEG encode
                         +--------- bounded frame packet --------+
                                                              HTTP/1.1
Windows gs-client  WinHTTP -> CRC/budget/decode -> D3D12
                       Projected: Float40 upload -> quad/alpha -> float target
                       Image: RGBA texture upload
                       -> RGBA8 offscreen capture / optional Win32 Present
```

服务器负责三维模型、完整 SH、投影和 GPU 排序；端侧只有当帧二维高斯或像素。`ImageFrame` 也完成 Gaussian 光栅化；它不是把 GPU 高斯计算偷留在客户端。`ProjectedSplats` 保留最后 Gaussian 光栅化，但其成本仍是 O(可见高斯与 overdraw)，不适合作为所有弱设备的唯一方案。

| Provider/target | 职责 | 不依赖 |
| --- | --- | --- |
| `gs_frame` | 版本化 codec、CRC、量化、CPU reference | CUDA、模型解码、HTTP、窗口 |
| `gs_server_model` | trusted PLY/SPZ -> immutable scene | Windows、WinUI、D3D12 |
| `gs_cuda` | 显式 device、驻留、GPU frame graph | HTTP、Windows UI |
| `gs-server` | 参数、模型选择、请求校验、JSON 计时 | 既有 Desktop SDK surface |
| `gs_thin` | WinHTTP 与 Windows D3D12 provider | CUDA、PLY/SPZ |
| `gs-client` | 请求、统计、固定图像窗口 | WinUI、完整模型 |

复用的是 scene 数学契约和已有解码器，不是直接将 Android 的 48 B 或 Windows 内部 buffer 作为网络协议。未复制受限第三方 3DGS rasterizer。

## 2. Linux/NVIDIA API 决策

### 已实现 CUDA 12.6/CUB 路径

GPU memory/kernel/event 是 CUDA Runtime；投影、SH、排序、tile raster 都是原生 C++/CUDA。没有传统 graphics draw call，也没有 Vulkan、OpenGL 或 EGL context。CUDA 是当前可硬件验证的 headless GPU 计算接口，不应宣传为 Vulkan graphics engine。

WSL 的 `nvidia-smi` 成功不保证 Vulkan ICD 存在。这里 NVIDIA CUDA 能使用 RTX 3080，所列 ICD 不含 NVIDIA；软件 Mesa Vulkan 不满足 NVIDIA 渲染验证。WSL 不安装 Linux NVIDIA 驱动。原生 Linux 可用独立 NVIDIA 驱动，但要另做原生系统验收。

### 原生 Ubuntu Vulkan provider 设计（未实现）

若业务严格要求图形 API，建议下一阶段单独实现 `VulkanOffscreenRenderer`，不包装 CUDA 后假称 Vulkan：

1. 无 Surface 的 VkInstance/physical-device 选择；查询 compute/graphics queue、subgroup、timeline、storage buffer range 和 memory budget，不固定臆测驱动上限。
2. Scene 属性使用 SSBO/分段 descriptor 与显式 std430 host/GLSL 布局；SH/projection 为 compute dispatch，输出版本化二维 primitive。
3. native radix histogram/scan/scatter compute pass 维护稳定 key/index 顺序；跨 pass 显式 shader storage barrier。不要 CPU 排序。
4. Projected 路线增加 GPU compact/quantize/pack -> transfer buffer -> fence -> bounded host export。
5. Image 路线用 tile-based compute raster，或 indirect instanced quads 与明确 far-to-near alpha；RGBA8 attachment/storage image -> transfer readback。float accumulator 的质量需要与本 prototype 重做对照。
6. 不需要 `VK_EXT_headless_surface` 才能渲染离屏 image；该扩展并不自动提供网络传输。CUDA/Vulkan external-memory/semaphore interop 属于独立复杂度，首期不混用。

本实现用 provider 边界隔离这一变化，但**没有交付或测试 Vulkan backend**。

## 3. GPU frame graph

### 3.1 驻留与投影

load_scene 用现有 probe 与规范化 PLY/SPZ decoder，转换成 RUB 语义和 double 世界原点 + float 局部坐标。Linux mmap 保存只读 scene；上传 center、scale、quaternion、opacity、RGB0、SH rest 六组 SoA。RGB0 已经由 model-io 解码，不再次添加 SH0。

每点一个线程：三维各向异性 scale/quaternion -> 相机协方差 -> perspective Jacobian -> 二维 covariance -> eigen axes；加入 0.01 px² 对角 blur，输出三倍标准差支撑的 axis0/axis1。近/远与画布粗筛；完整 SH1–3 用模型对应 degree 求色，最后 clamp [0,1]。半轴最多 1024 px，所有 profile/对照使用同样规则，不暗中切换质量。

fit-camera 对包围盒拟合，六个 orbit 方向、60° FOV、上方倾角固定。世界原点差在 double 中求出再转 float。本阶段不做任意相机、模型镜像或客户端外部姿态 API，包围盒离群点可能令物体在画面中偏小。

### 3.2 排序与两条出口

距离平方是非负 float，其 uint 位模式单调；取补码作为远到近 radix key，无效点用 sentinel 排到尾部。**排序度量是 radial camera distance，不是 view-z depth**，保持本仓库路线。CUB SortPairs 稳定保留同键原始 index。GPU gather 得到有序二维高斯，CPU 不排序。

Projected 只 readback 有效 Float40，然后量化/封包。Image 不 readback 高斯，只在 GPU 上建立 tile/rank 流：

- 16×16 tile：每个 ellipse 保守 AABB 统计覆盖 tile 数。
- GPU exclusive-prefix 分配连续引用区域；每条 key 为 `(tile_id << 32) | global_rank`。
- GPU SortKeys 后按 tile 建立 [start,end)，rank 与全局稳定顺序一致。
- 每个 16×16 pixel block 分批读取 128 个 ellipse 到 shared memory，全部线程都参加 barrier；边缘尺寸线程不写越界像素。
- 各像素按远到近执行 `C = color * alpha + C * (1-alpha)`，float 累积，最后 round RGBA8，alpha 固定 255。

tile/rank 引用最多 6400 万；这是可观测资源限制，不是承诺所有大场景总能渲染。没有 early-transmittance 策略、LoD 或静默抽样，benchmark 不把训练/资产优化的收益归给网络切分。

### 3.3 Raster 契约

像素中心在 x/y+0.5。axis 是 3σ 支撑半轴；Gaussian coordinate 映射到 σ 后，radius²>9 丢弃，alpha=`min(0.999, opacity*exp(-radius²/2))`，alpha<0.0039 丢弃；opaque 黑背景。不加额外 gamma/OETF，颜色沿用现有编码域。

Windows Projected 使用 instanced quad/float40 StructuredBuffer、R32G32B32A32_FLOAT alpha blend 和独立 RGBA8 resolve。直接在 RGBA8 中逐层混合会产生量化误差，已避免。SM5 shader 即可，薄端不需要 WaveOps/radix；与原本完整 D3D12 renderer 的最低功能要求不同。

Windows Image先走直接copy，只有第一次Projected请求才检查FP32 blend并编译相应PSO；不让Image被未使用的高斯管线能力挡住。可选GS_D3D12_DEBUG启用debug layer并将error/corruption转成明确失败。此按需初始化在当前RTX3080上未证明稳定的冷启动时延改善，不据此承诺性能。

## 4. 帧协议与量化

header 固定 64 B、显式小端；CRC 是 IEEE CRC-32，不是 CRC-32C。CRC 只检查损坏，不能提供真实性/鉴权。

| offset | bytes | 内容 |
| --- | --- | --- |
| 0 | 8 | `NGSFRM01` |
| 8 / 12 | 4 / 4 | version=1 / profile enum |
| 16 / 20 / 24 | 4 / 4 / 4 | width / height / count |
| 28 | 4 | compression 0=raw，1=Zstd |
| 32 / 40 | 8 / 8 | unpacked bytes / payload bytes |
| 48 / 52 | 4 / 4 | payload CRC / header CRC（本字段置零后计算） |
| 56 | 8 | reserved=0 |

f32/f16 顺序：center_xy、axis0_xy、axis1_xy、RGB、opacity。远到近就是数组顺序，不再传 3D depth/index。f32 40 B；f16 20 B，全部十个值为 IEEE half。half 不等于“每个点永远保持固定像素误差”，1080p 绝对坐标大时中心步长可到 1 px，须实测质量。

q20/q16：前 10 B 为 signed center_x/center_y 两个 int16、major/minor log-radius 两个 uint16、angle uint16；随后 RGBA16（8 B）或 RGBA8（4 B），最后 2 B flags=0。轴符号翻转不改变 Gaussian covariance，因此量化 orientation 到 [0,π)。

该表示要求输入为正交 eigen axes，归一化内积绝对值≤1e-5；一般的非正交 Float40 basis 须先正规化，当前 encoder 明确拒绝而非偷偷改变 covariance。f16 对舍入后 axes 的 determinant 也做检查，不能返回客户端无法解码的塌缩半精度 ellipse。

- center：相对画布中心，单位 1/16 px；单轴舍入误差≤1/32 px，但只可表达 [-2048,2047.9375] px 的相对范围。
- radius：log2 范围 [-12,11]，uint16，包含 [1/4096,2048] px。单轴相对舍入上界约 `2^(23/(2*65535))-1`。
- angle：π/65535 步长，角误差≤π/(2*65535)。
- color/opacity：q20 单分量误差≤1/(2*65535)；q16≤1/(2*255)。alpha 的最终累积图像误差没有相同简单上界。
- 越界请求整体失败，不能 wrap、截断、静默删除高斯。解码拒绝未知 profile/flags、NaN、无穷和奇异 axes。

Zstd level=1，压缩膨胀则回退 raw，但 header 明确记录；f32 little-endian 可一次复制经过验证的无 padding Float40，其他平台走显式序列化。CRC slicing-by-eight 保持 wire 格式不变。Image `rgba` 无损 + Zstd；JPEG 95/85/70/50 使用 Linux libjpeg-turbo 4:4:4，Windows WIC 解码，不再套 Zstd。

JPEG quality 数字没有跨 codec 的共同画质标尺。现阶段不加 WebP/AVIF/NVENC；先用已经实测的 CPU codec 完成单帧闭环，避免 NVENC 视频 session/startup 被误计为天然低延迟。GPU quantize/pack、pinned memory、异步压缩/发送属于后续优化，不冒称已经实现。

## 5. 请求、资源与安全

`GET /health` 返回 ready；`POST /frame` text body 为 `NGSREQ1 width height view profile`。模型由 worker CLI 预选，客户端没有资产路径。每 worker 顺序处理请求，读与写总 deadline 分别 10 s，header≤8 KiB、body≤1 KiB，无无限队列/自动重试/缓存。

协议最大 4096²、800 万输出高斯、解压 512 MiB；source 模型单独允许 3200 万点、input/normalized scene 各 8 GiB。超过 protocol 的 source 可以请求 Image，但不代表一定在 GPU budget 或 tile 引用上限内。CUDA 每次分配查询可用内存，单次申请超过当前 free 的 95% 失败；不以 available RAM 保证 WSL 不触发系统 OOM。

CUDA buffer RAII 与显式同步管理释放。Windows fence 等待有 5 s 上限；设备丢失/超时返回失败，当前没有生产级 context 恢复。CLI 返回非零退出码；HTTP 请求错误用 400 和有界诊断，当前没有细分业务错误枚举/HTTP 503。

loopback 是安全边界，不是公网服务器。原生部署可由 TLS/鉴权网关反向代理到本机 worker，并独立实现用户配额、模型准入、任务 deadline、connection limit、监控与调度。HTTPS 客户端能力不代表已部署或测试网关。Linux decoder 处理 trusted 本地文件，未添加上传与隔离 sandbox。

## 6. 实验定义与下一步

模型初始化、warm resident 服务器阶段、fresh WinHTTP session 网络、CPU decode、D3D12 upload/draw、截图 readback、working set 分别记录。HTTP network_ms **包含服务器计算等待及传输**，不能再次与 server_ms 相加；ready_with_capture_ms 包含 screenshot，但不含进程启动、模型加载、物理显示或用户输入 RTT。

client_committed_resource_bytes 是帧 ID3D12Resource 的 allocation-size 之和（default/upload/readback），不等于纯 VRAM，更不包含所有驱动/descriptor/PSO开销。server_peak_gpu_bytes 是本 renderer 跟踪的 CUDA allocations，不是 nvidia-smi 整机显存。单机 CUDA/D3D12 共用 RTX 3080；这组测试不能代表低端 GPU 或真实客户端机器。

PSNR/SSIM 对 RGB 与独立 CUDA RGBA8 reference 比较：全图、非黑 foreground（max RGB>8）、foreground 包围盒+4 px margin 的 local-window SSIM 同时报告，防止黑背景抬高质量分数。capture 不做性能与质量混跑的不可见抽点；报告 p50/p95 和完整逐次 rows。

选择流程：先明确 foreground 质量门槛，再在通过门槛的 profile 中比较 packet、编码/解码和端 GPU 负载。对于密集大模型，Image 很可能比保留全部高斯更符合低带宽/弱端目标，但该结论只使用本次实测样本，不对任意稀疏场景保证。

下一阶段优先级：固定任意相机/相机文件与 asset bounds 策略；真实弱 Windows 设备和独立服务器；原生 Ubuntu Vulkan provider；GPU pack/pinned export；warm HTTP/TLS 网关；故障恢复与租户隔离。多帧预测、delta、重投影、HybridTiles 必须另立规格和证据，不混入本次单帧结果。

## 7. 官方依据

部署与 API 能力核对入口（不是性能保证）：NVIDIA CUDA on WSL User Guide、CUDA Runtime API、CCCL CUB DeviceRadixSort/DeviceScan 文档；Khronos Vulkan Specification 的 offscreen image、同步和 headless_surface；Microsoft Learn D3D12 readback heaps/timestamp query、WIC 与 WinHTTP；libjpeg-turbo 官方 README；Boost.Beast 官方 HTTP/1 文档。已有研究工作及原始链接保留在仓库 `docs/cloud-edge-single-frame-design.md` 第14节，论文的数值不作本项目实验结果。
