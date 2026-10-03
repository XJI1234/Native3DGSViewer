# 单帧实现与验证记录

2026-10-02。这里的数值来自本机执行，不把论文指标、理论链路时间、截图就绪或 WSL 验证冒称为生产验收。

## 1. 验证范围

- 实现 Linux CUDA/CUB native backend、CLI、驻留模型 loopback HTTP、两种帧表示、九个 profile、Windows D3D12/WIC/WinHTTP client。
- 单 GPU worker 正常退出、端口冲突、无效第二 device 清理已验证。只有一张卡，**没有多卡硬件结果**，没有单帧跨卡分片或网关负载均衡实现。
- 当前服务器是 **CUDA compute rasterization，不是 Vulkan graphics backend**。原生 Ubuntu Vulkan 管线只有 architecture.md 中的设计，没有未测代码冒充实现。
- 固定六个 fit-camera 视角。没有任意相机、连续运动、移动客户端、LoD/抽点、跨帧缓存或生产公网服务。

## 2. 环境与复现

| 项目 | 实测配置 |
| --- | --- |
| Linux | WSL Ubuntu 24.04.1，约15 GiB RAM + 4 GiB swap |
| GPU | RTX 3080，10240 MiB；driver 616.92；server/client 共用此卡 |
| CUDA | nvcc 12.6.85，cudart/CCCL 12.6.77；86-real + 86-virtual；`--fmad=false` |
| Linux compiler/build | g++12.4.0，CMake3.28.3，Ninja1.11.1，Release |
| 系统依赖 | Boost1.83，libjpeg-turbo2.1.5，zlib1.3（Ubuntu包版本见evidence/environment.txt） |
| 仓库 source pins | Zstd1.5.6、SPZ3.0.0 revision、GoogleTest1.17，见根 third_party/README.md |
| Windows | VS2026/MSVC19.50，Windows SDK10.0.26100.0，CMake4.3；hardware D3D12 FL11.0+ |
| Python | NumPy2.3.5，Pillow12.0.0，scikit-image0.25.2 |
| 格式 | clang-format20.1.8，仓库 Microsoft/4空格/100列配置；17个C++/CUDA头源文件检查通过 |

WSL 的 Vulkan ICD 列表没有 NVIDIA：asahi/gfxstream/intel/lvp/nouveau/radeon/virtio 等不是 NVIDIA 硬件 backend 的验收。没有安装 Linux NVIDIA driver，CUDA 走 Windows 驱动桥接。

构建与主实验命令见 README。实验阶段没有并发运行其他 GPU 基准；后续回归与 sanitizer 在性能采集结束后运行。每 profile 独立启动 Windows 进程，discard3个暖机请求后采集30次；每次 WinHTTP 都是新 session/connection，模型驻留、没有帧响应缓存。

## 3. 测试资产

没有修改模型，也没有把模型复制进 Git。SHA-256 全值在 CSV/sample evidence 中，以下显示前12位便于检索。

| 资产 | 源字节 | source点数 | 用途 |
| --- | ---: | ---: | --- |
| changjin_v1.ply | 40,310,041 | 170,799 | 720p/1080p，view0/2，九profiles+五raw，优化A/B与限速；hash7c7cb96bed46 |
| shengyi_v1.ply | 189,924,365 | 见paired CSV | PLY/SPZ格式闭环；每profile5次，不能当30次正式分位统计 |
| spz/shengyi_v1.spz | 17,927,619 | 见paired CSV | 同名SPZ资产独立reference；不是声称与PLY逐像素相同 |
| spz/juyuan_v2.spz | 52,605,719 | 2,670,017 | 两分辨率九profiles；final1080p；hash24133da7801f |
| zhihuizhimen.ply | 923,849,202 | 3,914,609 | 两分辨率九profiles；final1080p；hashd86fa4be8a41 |
| jiulonghu_v1.ply | 5,305,366,675 | 22,480,361 | 大模型Image3次及Projected明确拒绝；完整hash见largest-scene.json |

覆盖6个文件/5个场景，不是扫描完38个模型的验收。大SPZ、其他视角和原生系统仍需扩展测试。

## 4. 自动化检查

| 检查 | 实际结果 |
| --- | --- |
| Linux Release CTest | 2/2组：10个frame契约测试 + 4个CUDA测试，通过 |
| Windows Release + D3D12 debug layer | 2/2组：10个frame测试 + 2个thin-render测试，通过；debug queue不允许error/corruption |
| CPU Debug ASan/UBSan | 1/1组、10个frame测试，通过；不等于系统JPEG/Zstd库也被重新instrument |
| Compute Sanitizer memcheck/leak-check | 4个CUDA测试，0 errors |
| racecheck/synccheck | tile raster/empty/recovery两个测试，0 hazards/errors |
| HTTP/Windows/CLI集成 | 23个检查通过：损坏/超限请求、真实总deadline、后续请求恢复、九profiles各两次、非法CLI和非loopback明文URL拒绝 |
| worker pool | 3个检查通过：SIGTERM清理、端口冲突不误认已有服务、device99失败后清理device0 |
| 既有仓库CTest | ModelIo/RenderCore/ModelRenderIntegration/Engine/InstalledSDK共5/5组通过，71.36 s |
| 格式/脚本 | clang-format dry-run/Werror通过；Python脚本在实际闭环使用，均保留日志 |

16个新增独立C++测试（跨平台frame测试会重复执行），不是声称达到某一百分比覆盖率。关键断言：

- 1,000,003个重复密集/边界uint key稳定排序与CPU stable_sort完全一致。
- isotropic投影中心/半径独立解析解；SH1/2/3各代表项独立求值，非只拿GPU投影再自我比较。
- 非16倍画布CUDA tile raster与独立CPU raster最大通道误差≤1；Windows float target raster最大误差≤1；Image upload逐字节一致。
- 全部64-byte header的512种单bit损坏、截断/尾随、NaN、范围、半精度溢出/塌缩、非正交量化basis、JPEG损坏/尺寸不符、空帧和恢复。

检查中发现并修正：Beast同步IO不可靠地受SO_RCVTIMEO约束（原partial-header测试15 s失败，改为Asio异步总deadline后约10 s通过）；libjpeg longjmp的stack context风险；统计header拼接成无效JSON；CPU极端float转int范围；half塌缩和非正交量化。均保留负向回归测试。Windows Image不再为图片预先要求FP32 blend/编译高斯PSO，但本机冷初始化变动没有证明稳定时延收益，不夸大这项优化。

原始检查日志在 evidence/，完整大输出在忽略的 out/。没有执行公网/TLS、device-lost注入、真实Present/显示器延迟、原生Ubuntu22/24或其他GPU验收；已有SDK文档里的外部画质/设备门槛仍然有效。

## 5. 带宽、质量与端负载

完整归档 **127个配置、3660个暖机后采样**，另有暖机/参考请求。小模型矩阵56配置，main36，CPU优化5，paired6，Image按需初始化2，final18，限速4。paired的6配置只测5次，其余每配置30次。全部失败列表为空；22M的Projected拒绝是另外一个明确设计内准入测试，不藏在失败列表外冒称能传全模型。

质量 reference 是同一相机、同一完整SH和同一raster参数的CUDA RGBA8，**不是原始照片/独立训练渲染器的ground truth**。全图RGB PSNR/SSIM、foreground（max RGB>8）PSNR及foreground包围盒+4 px边缘的7×7 local-window SSIM同时报告；不是global-mean伪SSIM。下表使用foreground PSNR/crop SSIM，以防大片黑背景抬高分数。

### 5.1 391万点 PLY，1080p/view2，最终版本

source3,914,609，visible3,904,214；foreground占5.73%。单位MiB为2²⁰ B；就绪是本机未限速、warm client、含截图的p50。

| profile | frame MiB | FG PSNR dB | crop SSIM | CPU decode ms | GPU upload / draw ms（各p50） | ready p50 / p95 ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| f32 + Zstd | 127.454 | 59.85 | 0.99994 | 379.81 | 24.150 / 11.334 | 1965.63 / 2163.77 |
| f16 + Zstd | 64.407 | 21.01 | 0.94081 | 270.67 | 23.807 / 7.581 | 1337.97 / 1473.92 |
| q20 + Zstd | 62.456 | 41.71 | 0.99918 | 308.03 | 23.667 / 8.760 | 1554.61 / 1662.46 |
| q16 + Zstd | 51.398 | 41.70 | 0.99918 | 269.48 | 23.679 / 8.125 | 1279.23 / 1429.55 |
| rgba + Zstd | 0.397 | ∞（一致） | 1.00000 | 3.46 | 0.666 / 0.001 | 82.64 / 89.18 |
| jpeg95 | 0.159 | 38.51 | 0.99648 | 6.47 | 0.666 / 0.001 | 73.48 / 75.60 |
| jpeg85 | 0.111 | 32.58 | 0.98851 | 6.83 | 0.667 / 0.001 | 72.32 / 75.53 |
| jpeg70 | 0.091 | 28.99 | 0.90793 | 5.97 | 0.671 / 0.001 | 73.77 / 75.35 |
| jpeg50 | 0.081 | 26.67 | 0.96472 | 6.54 | 0.674 / 0.001 | 72.77 / 74.77 |

Projected四档客户端均展开成Float40，帧resource allocation合计348.06 MiB；Image各档24.38 MiB。这个合计含upload/readback heaps，不是纯VRAM。q16 peak working set597.30 MiB，jpeg95约104.15 MiB。**量化减少wire bytes，但当前并不减少高斯端GPU buffer尺寸。**Image draw时间近零不代表零GPU负载：还需约0.67 ms纹理copy及截图。

同模型的CUDA阶段在Image与Projected运行间观察到不同时间；两模式使用同一投影kernel和排序规则，shared WSL/D3D12设备的驻留、时钟及分配影响未独立隔离，不能归因为Image秘密降低了点数/SH。

### 5.2 267万点 SPZ，1080p/view2，最终版本

source2,670,017，visible2,670,015；foreground占约3.91%。

| profile | frame MiB | FG PSNR dB | crop SSIM | ready p50 / p95 ms |
| --- | ---: | ---: | ---: | ---: |
| f32 + Zstd | 91.086 | 62.14 | 0.99986 | 1221.74 / 1343.43 |
| f16 + Zstd | 45.457 | 30.09 | 0.98364 | 867.46 / 932.75 |
| q20 + Zstd | 45.898 | 48.51 | 0.99955 | 1007.73 / 1112.82 |
| q16 + Zstd | 36.313 | 48.35 | 0.99951 | 858.70 / 920.74 |
| rgba + Zstd | 0.267 | ∞（一致） | 1.00000 | 62.56 / 66.31 |
| jpeg95 | 0.106 | 40.98 | 0.99532 | 65.81 / 71.58 |
| jpeg85 | 0.082 | 35.30 | 0.98659 | 66.17 / 72.72 |
| jpeg70 | 0.072 | 31.82 | 0.92803 | 66.19 / 69.53 |
| jpeg50 | 0.067 | 29.82 | 0.96308 | 65.72 / 68.37 |

JPEG70的SSIM并不优于JPEG50，原值保留，不能假定quality数字对每个指标单调。FP16绝对像素坐标在1080p下可有0.5–1 px步长，对高频细节不友好；此样本集中q16/q20比f16更适合高质量primitive streaming，但不对所有资产做无条件保证。

### 5.3 限速实测（不同于理论值）

小模型170,799点、720p/view2、额外40 ms delay、5 Mbps应用层paced proxy；30次暖机后采样。

| profile | packet bytes | bytes/5Mbps理论 ms | 实测network p50 ms | ready p50 / p95 ms |
| --- | ---: | ---: | ---: | ---: |
| q16 | 2,384,713 | 3815.54 | 3911.64 | 3946.21 / 3956.66 |
| rgba | 310,923 | 497.48 | 602.98 | 612.03 / 622.90 |
| jpeg95 | 69,556 | 111.29 | 205.89 | 216.33 / 233.19 |
| jpeg85 | 48,269 | 77.23 | 157.79 | 166.85 / 189.73 |

proxy完整收取origin后再限速chunk，延迟在响应前额外注入；没有真实WAN、丢包、拥塞/慢启动或TLS。不能把40 ms写成实测公网RTT。network_ms含server等待，不要再相加server_ms。

391万点1080p q16在5Mbps的**理论packet序列化**约86.23 s；无损rgba约666.84 ms，jpeg95约266.36 ms。该组大型资产没有跑相同5Mbps proxy，不能冒称86 s或266 ms为真实WAN测量。理论值含64 B frame header，未含HTTP/TLS、重传和排队。

## 6. 单帧延迟优化证据

同170,799点、1080p/view2、各30次，用slicing-by-eight IEEE CRC与little-endian Float40 bulk copy替代逐byte CRC/逐字段封包；**五个A/B packet逐字节一致**。

| profile | before encode p50 ms | after encode p50 ms | before / after ready p50 ms |
| --- | ---: | ---: | ---: |
| f32 + Zstd | 39.457 | 25.086 | 134.291 / 92.604 |
| f32 raw | 29.896 | 13.346 | 123.825 / 81.744 |
| rgba + Zstd | 18.548 | 17.370 | 49.896 / 49.269 |
| rgba raw | 26.985 | 14.822 | 128.903 / 96.575 |
| jpeg85 | 7.383 | 7.308 | 38.630 / 37.363 |

这是同机顺序A/B观察，不声称屏蔽全部时钟/系统噪声或有统计因果置信区间。JPEG主要瓶颈不在CRC，结果也没有伪造显著提升。还有每帧临时cudaMalloc/free、同步readback、CPU量化/压缩的开销；尚未实现GPU pack、pinned-memory overlap和persistent scratch pool。服务器原始模型常驻，不能把它称作所有临时buffer也常驻。

客户端init通常数百ms；CSV额外记录client_init_ms及首次init+ready。暖机后的ready不含init，更不能把CUDA模型冷加载（秒至几十秒）隐去，宣称从启动到看到大模型只需70 ms。

## 7. 2248万点大模型

完整SH3、全部22,480,361点准入，view2/720p：visible22,265,867，tile references26,485,068。JPEG85 frame60,016 B；GPU resident5,305,365,196 B，tracked peak7,637,602,687 B（约7.11 GiB），Linux最大RSS5,306,336 KiB。没有抽点或降低SH。

三次CLI server_ms为366.685/367.833/360.469 ms；kernel project约14 ms，tile draw约117–119 ms。load/upload35,986.6 ms，另一次加载约30,138 ms。Windows从文件解码/上传/截图成功，后两次ready约11.57/11.13 ms；**不是network或大模型正式p95统计**。图像质量数值及截图路径见evidence/largest-scene.json与out/server-largest-client.png。

同模型q16请求返回非零退出码1及`Projected payload count exceeds protocol budget`，不把2200万点静默压成800万点。该模型说明Image出口可绕开“输出高斯数”的protocol上限，不保证所有22M场景均不超64M tile引用或GPU budget。

## 8. 结论与验收边界

1. 当前密集真实资产下，Projected卸载了SH/投影/排序，但数十至上百MiB wire、hundreds-MiB端资源和overdraw仍存在。低带宽/弱端目标优先选Image，不单凭“已经原生移动开发”解决资源问题。
2. 高质量primitive对照优先q20/q16，f32用于基准；不能只看16-bit名称选f16。服务器encode/客户端decode仍是成本，量化不是免费。
3. 若门槛取foreground PSNR≥40 dB且crop SSIM≥0.99：SPZ该视角jpeg95可通过；391万点该视角jpeg95的38.51 dB不过门槛，需rgba无损。这个门槛是条件示例，不是用户已经批准的统一产品指标。
4. 这些图像黑背景较多、camera由bounds自动拟合，不能代表贴近细节或满屏复杂资产；仅测共享RTX3080不能保证低端Windows/移动GPU体验。
5. 原生Ubuntu22.04/24.04、其他NVIDIA架构、实机多GPU、真实弱客户端、WAN/TLS/鉴权、故障恢复、真实Win32 Present/物理显示以及Vulkan实现仍未验收。保持这张清单，不用“充分测试”替代尚未发生的部署证据。

数字归档：evidence/server-benchmark-final.csv、paced CSV、optimization.csv、source-sha256.json、环境与测试日志。完整JSONL、PNG/PPM、packet和误差图在out/server-benchmark-*；final两个场景各有quality-comparison.png。资产原件仍在Desktop/zhishan。
