# 远端环境只读核验

状态：初始环境发现证据，不是部署、编译、渲染或性能验收。后续真实实施见verification-image-v2.md；远端报告的时钟仅作为设备快照，不用来判定本任务当前日期。性能计时使用monotonic/steady clock。

时间：远端 `nvidia-smi` 报告 `Sat Oct 3 00:32:38 2026`。以下来自本次通过 SSH 执行的只读命令，不沿用之前 WSL/RTX 3080 的推断。

## 连接与安全

- 已连接用户提供的 SSH 主机 `10.129.4.104`、端口 `8045`、用户 `root`。
- 用户文字中的 `C:\Users\21544.ssh\id_rsa` 不存在；实际使用已存在的 `C:\Users\21544\.ssh\id_rsa`。
- 使用 `BatchMode=yes`、`StrictHostKeyChecking=yes`、`ConnectTimeout=15`；现有 `known_hosts` 中已有对应主机和端口的 ed25519 记录，未关闭主机身份校验。
- 未读取/输出私钥内容，未把私钥上传或写入仓库。

## 实际环境

| 项目 | SSH 实测 |
| --- | --- |
| 容器发行版 | Ubuntu 22.04.5 LTS |
| 宿主共享内核 | 5.15.0-139-generic，x86_64 |
| 容器 PID 1 | `/bin/bash /run.sh`，不是 systemd |
| NVIDIA driver | 580.82.09 |
| GPU | index 0、1、2、3 均报告 NVIDIA GeForce RTX 4090 |
| 每卡显存 | 49,140 MiB；这是该远端设备的实际报告，不假定它是标准 24 GB 配置 |
| GPU 初始状态 | 四卡 GPU-util 0%，每卡约 27 MiB 已用；`nvidia-smi` 未列出计算进程 |
| 默认 `nvcc` | `/usr/local/cuda-11.8/bin/nvcc`，11.8.89 |
| `/usr/local/cuda/bin/nvcc` | 12.4.131 |
| CUDA 13.0 Toolkit | 检查的 `/usr/local/cuda-13.0/bin/nvcc` 不存在；已列举的 CUDA 目录仅 11.8 和 12.4，`dpkg-query` 未发现 `cuda-toolkit-13-0` |
| 默认 gcc/g++ | 11.4.0 |
| 已安装 gcc-12/g++-12 | 12.3.0 |
| CMake | `/opt/conda/bin/cmake`，3.30.5 |
| 默认 `python3` | 3.13.11；部署不应依赖修改 conda base |
| 已有构建依赖 | Boost system 1.74、libjpeg-dev、zlib1g-dev、Ninja 1.10.1 |
| 系统内存 | 251 GiB 总量，采样时 243 GiB available |
| 根文件系统剩余 | 约 982 GB，86% 已用 |
| 当时 TCP listener | 22 和 SSH X11 转发 6010；未发现 8888 listener |

`nvidia-smi` 中的 `CUDA Version: 13.0` 与默认 `nvcc` 11.8 并不矛盾：驱动支持的 CUDA 版本不能作为 CUDA 13 编译工具链已经安装的证据。后续构建必须记录实际选中的 nvcc、host compiler、CMake cache 和设备架构，不能静默退回旧工具链。

## 拓扑与多卡试验约束

`nvidia-smi topo -m` 报告：GPU 0/1 属于 NUMA 0，GPU 2/3 属于 NUMA 1；同组连接标为 NODE，跨组标为 SYS，未列出 NVLink。

拓扑报告不等于实际 CUDA peer-access 测试，本阶段未测试 `cudaDeviceCanAccessPeer`。建议先验证独立请求的多 worker 并发，再决定是否有理由增加单帧跨卡排序与 alpha 合成成本。不得以四卡数量推导 4 倍提速。

单入口 `8888 → 8046` 来自用户给出的映射关系，尚未用真实服务验证应用端口穿透；“8888 空闲”也只是该次快照，启动时仍需重新检查端口所有权。

## 模型与工作区

只读扫描了 `/workspace/3DGS` 和 `/workspace/display` 的最多三层目录，发现 `/workspace/display/ply_output/` 下有约 29.9 MB 的 PLY 文件。未修改、加载或拿这些其他工程的文件替代用户指定模型。

本次尚未上传 `C:\Users\21544\Desktop\zhishan` 中的样本，未写入远端安装目录或更改其他项目。

## 本地工程核验

- 既有原型默认 `Profile::F32`，仍暴露 ProjectedSplats；这与本次图像帧要求不符，需在新规格批准后迁移，而不是只改 UI 默认值。
- 既有 worker 只绑定回环、仅 `/health` 和 `/frame`，固定六个视角；尚无单入口调度、模型目录/状态和连续轨道接口。
- 既有 Windows 测试客户端是 CLI/Win32，不是 WinUI 3。
- 原 `GUI/` 是 WinUI 3 C++/WinRT，白色顶栏、浅色画布、底部状态栏；固定模式的相机边界在 `docs/SPEC-engine-sdk.md` 中已有契约，可作为本次交互参考。
- 旧 `worker_pool.py` 是多进程启动工具，不是已验证的四卡负载均衡服务。

## 官方资料核对

- NVIDIA CUDA 13.0 Linux Installation Guide：Ubuntu 22.04 列在支持矩阵中；支持 C++20，建议显式选择工具链并检查 host compiler。
- NVIDIA CUDA Compatibility：CUDA 13.x 的最低驱动系列为 580。本机远端驱动版本符合这一系列要求，但仍需编译/运行实际验证。
- Microsoft Windows App SDK self-contained deployment guide：C++ 项目可用 `WindowsAppSDKSelfContained=true` 随应用部署框架；新客户端可沿用原 GUI 的自包含模式。

来源地址（记录版本化文档，不把“最新版”作为构建 pin）：

```text
https://docs.nvidia.com/cuda/archive/13.0.0/cuda-installation-guide-linux/index.html
https://docs.nvidia.com/deploy/cuda-compatibility/minor-version-compatibility.html
https://docs.nvidia.com/deploy/nvidia-smi/index.html
https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/self-contained-deploy/deploy-self-contained-apps
```

## 未执行

CUDA Toolkit 安装、代码构建、GPU kernel/self-test、模型加载、单/多卡压测、网络应用服务、WinUI 构建和 UI 验收均未执行。不要把此文档当作这些验收项目已通过的证据。
