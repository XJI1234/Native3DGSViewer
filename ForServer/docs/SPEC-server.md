# 历史 v1 规格（已退役）

以下只用于旧实验追溯。当前公共契约见SPEC-image-frame/render-worker/service-gateway/linux-deployment/windows-client.md，不能按本文件调用当前服务。

# Linux NVIDIA 单帧服务器与 Windows 薄客户端

2026-10-02。用户已确定 Linux/NVIDIA、两个表示模式、Windows 测试客户端及 ForServer 实现范围。首期是可测量的固定视角研究后端，不是生产公网服务。

## 提供者与依赖

frame-types -> frame-codec / CPU reference -> CUDA renderer -> CLI / loopback HTTP worker；Windows D3D12 thin renderer 仅消费 frame-codec。所有新文件在 ForServer 内，既有 SDK ABI 不变。

## GPU 接口选择

原生 Ubuntu 22.04/24.04 NVIDIA 可使用 Vulkan 离屏图形管线，但本机 WSL 没有 NVIDIA Vulkan ICD，CUDA 能实际访问 RTX 3080。因此首期采用 CUDA 12.6/CUB 的完整 compute rasterization，不冒称 CUDA 是 Vulkan/传统图形 API。不需要 X11、Wayland、OpenGL、Surface、swapchain 或显示服务器。Vulkan 后端留作后续提供者，不交付未经硬件验证的假实现。

管线：规范化 PLY/SPZ 常驻 -> CUDA 协方差/SH/投影 -> CUB 稳定 radix 排序 -> 顺序 gather。ProjectedSplats 导出二维数据；ImageFrame 在 GPU 建立 tile/rank 引用、排序、按像素远到近混合、导出 RGBA8。不得 CPU 排序或静默降 SH/点数。

多 GPU 按模型复制、独立进程/device/port 的 worker 扩展，不将单个帧分片。单 GPU 命令接受 device index；进程池工具负责启动、信号清理，网关负责调度。只存在一张卡时明确标记多 GPU 实机未测。

## 帧契约 v1

画布左上原点，右/下为正；固定 stddev=3、alpha cutoff=0.0039、alpha cap=0.999、covariance blur=0.01 px²、支撑半轴上限1024 px、黑色 opaque 背景。Float40 的 center/axis0/axis1/RGB/peak opacity 是十个 float32，axis 是3σ半轴，radial camera distance 远到近顺序即数组顺序。颜色保持现有训练/显示编码域，禁止额外 gamma。

量化：f32（40 B）、f16（20 B）、q20（center 1/16 px int16、轴长 log uint16、角 uint16、RGBA16、flags）、q16（RGBA8）。q20/q16 输入须是正交 eigen axes（归一化内积绝对值≤1e-5），不静默丢弃非正交 covariance；f16 舍入后不得塌缩成奇异 ellipse。不可表示时整个请求明确失败，不截断；客户端解码校验正定/有限值。Projected 以 Zstd level1 编码，raw 对照；Image 为 RGBA8+Zstd 或 JPEG 95/85/70/50（Linux编码4:4:4，不套Zstd）。只允许模式与 profile 的有效组合。

版本化小端 header 携带 width/height、count、mode/profile、解压/压缩长度与 CRC。最大 4096x4096、800 万高斯、解压 512 MiB；任何越界/未知版本/截断/尾部附加数据拒绝。frame-codec 不处理外部 shader/模型路径。

source 模型独立限制为3200万点、input/normalized scene 各8 GiB；Image 路线 tile references≤6400万。source超出800万不意味着可发送全部高斯，也不保证能通过GPU内存准入。CUDA architecture 可由 CMake cache 配置，本机86只是RTX3080配置。

## CLI 与安全边界

提供 devices、render、serve、inspect/decode、Windows client，以及 Python benchmark/pool。服务只绑定 127.0.0.1，固定加载本地管理员给定模型，不接受上传/路径。不交付未加 TLS/鉴权的公网监听。HTTP body 有上限、读写 timeout，逐请求异常不破坏驻留模型。输出路径只属于本地 CLI。

失败退出码非零，参数校验先于模型加载/GPU工作，并按命令拒绝无关选项。GPU分配RAII、budget预检；CUDA错误带阶段诊断传到CLI/HTTP，当前错误文本不是完整业务错误枚举。所有提交在命令结束前同步。服务顺序处理单worker请求；进程池不用同一CUDA context多线程竞争。GET /health 返回ready及诊断X-GS-Worker-Pid，工具须验证是自己启动的进程，避免端口冲突时误报就绪。读/写使用Asio异步IO与总10 s deadline，不能用SO_RCVTIMEO冒充可靠的Beast同步超时。

## 测试与证据

先写 CPU/wire/量化负向测试，再实现；CPU reference 独立验证 CUDA 排序、投影和透明像素。GoogleTest、ASan/UBSan CPU、真实 WSL CUDA、Windows D3D12 截图/时间戳、模式/量化 benchmark。每个报告含 sample hash、GPU/driver、相机、实际可见数、payload、encode/decode/draw、CPU/GPU footprint、PSNR/SSIM 和理论传输时间。真实受限链路时延不得冒称由 bytes/bitrate 实测。阈值未达原样保留。

维护性：C++20、四空格、snake_case、无隐式全局设备、GPU/codec/HTTP/UI 解耦；新 kernel 不复制受限制的第三方 rasterizer。CUDA/CUB 是 NVIDIA Toolkit 依赖，GoogleTest/Zstd/SPZ 复用仓库 pin，系统 JPEG/Boost 版本记录在验证文档。WSL 24.04 通过不等于原生 Ubuntu22.04/24.04 或多卡部署已通过。

Windows Image 路线不得为显示图像预先要求FP32 blending或编译高斯PSO；Projected第一次使用才建立对应管线。debug测试可设GS_D3D12_DEBUG=1，缺少debug layer明确失败不静默关闭。GPU上传、draw与截图计时和resource allocation metric分开，后者包含upload/readback heaps，不等于纯VRAM。默认ready_with_capture不含客户端/模型初始化，也不是物理显示时延。
