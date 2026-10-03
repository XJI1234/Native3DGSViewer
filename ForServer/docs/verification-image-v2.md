# ImageFrame v2：四卡服务器与 WinUI 3 验收

本报告对应本轮获批的五个模块规格与实际执行结果，不沿用历史 `verification.md` 中的 WSL/RTX 3080 数字。结论：图像帧协议、原生 CUDA 后端、单入口多卡网关、容器部署和 Windows WinUI 3 客户端已完成并通过下述检查；这不是公网生产环境或全部模型兼容性的验收。

## 1. 实测环境与部署标识

| 项目 | 实际运行环境 |
| --- | --- |
| 服务器 | Ubuntu 22.04.5 LTS 容器，PID 1 为 bash/run.sh，非 systemd |
| GPU | device 0–3，均报告 NVIDIA GeForce RTX 4090、compute capability 8.9 |
| 驱动/显存 | 580.82.09；每卡报告 49,140 MiB，按设备实际报告记录，不推定标准卡配置 |
| 编译工具 | 隔离 CUDA 13.0.88、G++ 12.3.0、系统 Ninja、CMake 3.30.5 |
| 工具链目录 | `/opt/native3dgs/toolchains/usr/local/cuda-13.0`；原有 CUDA 11.8/12.4 未被替换 |
| 入口 | SSH `10.129.4.104:8045`；应用 `10.129.4.104:8046` 映射容器 8888 |
| 当前安装 | `/opt/native3dgs/releases/c29f2e75c38de658`，由 `/opt/native3dgs/current` 指向 |
| 服务方式 | 单入口网关，四个原生 worker，worker 只监听回环 19000–19003 |
| Windows | 本机 x64；VS 2026、SDK 26100、WinUI 3 C++/WinRT 自包含应用 |

远端时钟的具体读数保留在 [初始环境审计](remote-environment-audit.md)，只作为设备快照，不用它推定当前日期。性能使用 monotonic/steady clock。CUDA toolkit 的离线部署按官方 apt 元数据 SHA256 校验 deb 后解包，没有安装宿主驱动或执行 alternatives 脚本。

最终原生服务器二进制及两个模型 SHA256 见 [final-hashes.txt](evidence/cloud-4090/final-hashes.txt)。远端 Linux 源快照为 `/opt/native3dgs/source-image-v2`；Windows 最后的工具栏/头文件调整以本地源码与实际构建为准，不声称远端快照与整个本地目录逐字节一致。

## 2. 测试矩阵

| 范围 | 结果与证据 |
| --- | --- |
| Linux Release CTest | 2/2 组通过：[final-ctest.log](evidence/cloud-4090/final-ctest.log)；10 个帧/请求契约测试、5 个 CUDA 正确性测试 |
| 四卡逐卡检查 | device 0/1/2/3 原生 `self-test` 均通过：[final-self-test.log](evidence/cloud-4090/final-self-test.log)；该命令检查 GPU 稳定排序，不等同四卡各跑完整图像测试组 |
| CUDA 内存检查 | Compute Sanitizer 12.4 对 CUDA 13 构建的全部 5 个 CUDA 测试执行 memcheck，0 errors：[final-memcheck.log](evidence/cloud-4090/final-memcheck.log) |
| Windows Release CTest | 2/2 组通过：[windows-tests.txt](evidence/cloud-4090/windows-tests.txt)；共享 10 个契约测试、2 个图像复制/传输策略测试 |
| 网关测试 | 7 个单测/真实 socket 测试通过，Windows 与 Linux 均执行；Linux 日志：[gateway-tests.log](evidence/cloud-4090/gateway-tests.log) |
| 真实 HTTP 集成 | 外部 8046 与最终安装的回环 8888 各 18 项通过：[integration.json](evidence/cloud-4090/integration.json)、[final-integration.json](evidence/cloud-4090/final-integration.json) |
| worker 启动失败清理 | 无效 GPU 99 清理先启动的 GPU 0；健康响应 PID 不符时拒绝启动、清理自己的进程但不终止端口原占有者：[lifecycle.json](evidence/cloud-4090/lifecycle.json) |
| 部署检查 | bash 语法、doctor、停止/启动/重复启动、1/2/4 卡重配置验证；缺失 CUDA 失败、运行时安装被拒绝：[invalid-cuda.log](evidence/cloud-4090/invalid-cuda.log)、[running-install.log](evidence/cloud-4090/running-install.log)、[final-install.log](evidence/cloud-4090/final-install.log) |
| 原仓库回归 | 原 CTest 5/5 组通过，82.97 s：[root-regression.txt](evidence/cloud-4090/root-regression.txt)；没有修改原 GUI/SDK |
| WinUI | Release 构建、真实应用联网/显示成功、失败路径与截图见第 6 节 |
| 静态检查 | 31 个 C++/CUDA 文件按现有 clang-format 配置通过 dry-run/Werror；`git diff --check` 通过，并额外检查未跟踪的源码/文档，修正部署脚本一处行尾空格 |

契约测试包括全部 header 位变异、CRC32 标准向量、旧协议/旧 profile 拒绝、长度/资源限制、JPEG 损坏/尺寸不符、严格请求解析及透明顺序不可交换。CUDA 测试包括稳定排序、CPU 参考对照、解析投影/SH、全剔除恢复、orbit/zoom/非法参数。HTTP 检查覆盖认证、模型访问、非法请求、四种画质、重复 header、慢速不完整 header 总期限和测试后健康状态；并未模拟所有可能的公网恶意流量。

## 3. 单卡/多卡公平对照

模型为 `spz/juyuan_v2.spz`，2,670,017 个 Gaussian，文件 52,605,719 B。固定 1920×1080、yaw 0、pitch 14.0362434679、zoom 1、完整 SH、RGBA + Zstd。每个配置先预热 `concurrency × 4` 次，再测 120 帧；9 个配置共 1,080 个测量请求。请求由服务器回环 HTTP 发出，延迟包含网关排队、渲染/编码与读取完整响应，不是 Windows 展示延迟。冷加载约 2.6–3.4 s，单独记录于 JSON，不纳入暖态统计。

| GPU 数 | 并发请求数 | 吞吐 frame/s | 请求 p50 ms | 请求 p95 ms |
| --- | --- | --- | --- | --- |
| 1 | 1 | 26.6908 | 36.6330 | 40.2076 |
| 2 | 1 | 28.0838 | 34.9272 | 38.6122 |
| 4 | 1 | 28.2380 | 35.1267 | 38.3049 |
| 1 | 4 | 28.5189 | 141.1354 | 146.7793 |
| 2 | 4 | 56.7045 | 70.0584 | 79.5317 |
| 4 | 4 | 109.5639 | 35.6011 | 40.1496 |
| 1 | 8 | 32.5795 | 244.2031 | 249.2540 |
| 2 | 8 | 63.1045 | 123.8386 | 152.4019 |
| 4 | 8 | 124.4307 | 61.7907 | 69.3702 |

九份原始摘要为 `evidence/cloud-4090/cards-{1,2,4}-c{1,4,8}.json`。每个配置内所有测量帧字节一致；本地保留的九份 `frame.ngsf` 也具有同一 SHA256：`9ac008e7c2b5e8dfc563ffc9ad9e6669cec543ab7289131b91f0e473069594d2`，各 143,978 B。每 worker 报告 GPU 模型驻留 630,124,012 B、管线峰值 908,150,783 B；这是渲染器记录的分配，不是整卡所有进程的 nvidia-smi 峰值。

**多卡扩展的是独立请求吞吐，不是同一帧分片加速。** 并发 4 时四卡约为单卡 3.84 倍吞吐，而单请求 p50 仍约 35–37 ms。单客户端 single-flight 通常只使用一张卡。上述 frame/s 是服务器暖态请求处理速率，不是端到端交互显示帧率。

## 4. 编码带宽、质量与 Windows 端侧负载

同一 SPZ、固定相机与 1080p，经实际 8046 入口用 Windows CLI 测量；每 profile 31 次，丢弃首帧后统计 30 次。RGBA 无损图作为共同参考，JPEG 采用 libjpeg-turbo 4:4:4，不改变模型点数、SH 或分辨率。

| Profile | 整帧包 B | 前景 PSNR dB | 前景包围裁剪 SSIM | 解码 p50 ms | ready 含抓取 p50 ms |
| --- | --- | --- | --- | --- | --- |
| RGBA + Zstd | 143,978 | ∞，逐像素一致 | 1.000000 | 1.9236 | 72.6945 |
| JPEG95 | 93,058 | 39.3828 | 0.992671 | 6.5842 | 85.1644 |
| JPEG90 | 81,845 | 35.6857 | 0.984945 | 6.6732 | 84.4533 |
| JPEG85 | 76,750 | 33.5178 | 0.976780 | 6.5560 | 88.9419 |

证据：[quality-client.json](evidence/cloud-4090/quality-client.json)。包大小包含 64 B 帧头，不含 HTTP/TCP 开销；不把它叫固定每秒带宽。实际码率还取决于请求频率、相机和场景内容。RGBA 包相对 8,294,400 B 原始像素明显缩小，但此画面前景仅 **1.963%**，大面积黑背景特别易压缩，不能据此承诺所有 1080p 场景都只需约 140 KiB。

前景由 RGBA RGB 最大通道 > 8 定义；SSIM 在其包围框外扩 4 像素的裁剪图上计算。质量指标只相对于本引擎完整 SH 的无损输出，不是照片真值或独立渲染器准确率；没有用黑背景占优的全画面指标掩盖前景损失。

CLI D3D12 图像路径报告：提交资源 25,559,040 B（约 24.38 MiB，含上传/读回等资源，不全是设备显存），各 profile 峰值 working set 中位数约 107–109 MB；GPU 上传 p50 约 0.66 ms，图片 draw p50 约 0.001 ms。`ready_with_capture` 包含抓取开销，但不含实际屏幕显示到光子的延迟。JPEG 的 WIC 解码比此样本 RGBA 解压慢，LAN 下 RGBA 总时间更低。上述 D3D12 时间不能直接视作 WinUI compositor 时间。

## 5. 限带宽链路实验

使用真实应用响应 pacing 代理，5 Mbit/s、额外 40 ms 延迟、4 KiB 分块，实际传输上述图像包；每 profile 10 次暖态测量。不是只计算 `bytes/bandwidth`，也不是 WAN 或 Linux netem：代理先取得完整 origin 响应，再分块发送，不能模拟丢包、重传、抖动或双向网络的全部行为。

| Profile | ready p50 ms | ready p95 ms |
| --- | --- | --- |
| RGBA + Zstd | 354.91 | 370.20 |
| JPEG95 | 270.45 | 293.76 |
| JPEG90 | 256.91 | 281.68 |
| JPEG85 | 248.45 | 275.96 |

证据：[paced-link.json](evidence/cloud-4090/paced-link.json)。同一实验中 JPEG85 相比 RGBA p50 减少约 30%，代价见前景质量指标。当前采取用户选择质量而不是未经验证的自动切换；限带宽下 JPEG 有利，LAN 下默认 RGBA 更合适。

## 6. WinUI 3 实际应用验收

输出为 `out/cloud-client/Release/Native3DGSCloud.exe`，须携带整个自包含输出目录。连接页要求地址/应用端口/token；先健康检查再认证访问模型目录，成功才显示查看页。界面保留原生 GUI 白色工具栏、浅色画布、44px 底部状态栏；为避免高 DPI 控件挤出，工具栏改为自适应两行，第二行模型控件支持横向滚动。

受控 smoke 运行真实应用的网络、解码、图像更新及控件事件，覆盖两模型、RGBA/JPEG95/90/85、相机旋转/缩放、最新状态优先及断开后关闭。最终成功展示 7 帧、丢弃 2 个过期响应，峰值 working set 120,758,272 B，见 [winui-success.json](evidence/cloud-4090/winui-success.json)。此视图分辨率随真实 viewport 调整，截图为 1920×854 请求，不与上一节 CLI 1080p 数字混用。

失败路径均未伪造图像：错误 token 返回 HTTP 401；未允许私网 HTTP 时在连接前拒绝；不可达服务器得到 WinHTTP 12029。见 [winui-bad-token.json](evidence/cloud-4090/winui-bad-token.json)、[winui-http-consent.json](evidence/cloud-4090/winui-http-consent.json)、[winui-unreachable.json](evidence/cloud-4090/winui-unreachable.json)。最终 [查看页截图](evidence/cloud-4090/winui-viewer.png) 已视觉检查，所需控件可见、模型居中。

smoke 调用与真实按钮共用的相机逻辑，并触发画质/模型 ComboBox 事件，不等同逐一模拟物理鼠标滚轮、拖动和键盘输入。鼠标/键盘/resize 的事件接线已实现并审阅，完整跨 DPI、可访问性及长时间人工交互验收仍需补充。保守 fit 包含离群点与最大 Gaussian scale 的 3σ 支撑，主体可能较小；没有为好看的截图静默裁点，可通过滚轮放大。

## 7. 复现入口

以下是运维/测试操作，不应在有人使用服务时自动重配置。安装、TLS及模型权限细节见 [README](../README.md)。

~~~bash
# 服务器：当前安装检查，不改变卡数
/usr/bin/python3 /opt/native3dgs/service.py status --prefix /opt/native3dgs
ctest --test-dir /opt/native3dgs/current/build --output-on-failure
for device in 0 1 2 3; do /opt/native3dgs/current/bin/gs-server self-test --device "$device"; done
/usr/bin/python3 -m unittest discover -s ForServer/tools -p gateway_test.py -v
/usr/bin/python3 ForServer/tools/cloud_integration_test.py --url http://127.0.0.1:8888 --token-file /opt/native3dgs/token --output integration.json
/usr/bin/python3 ForServer/tools/cloud_benchmark.py --url http://127.0.0.1:8888 --token-file /opt/native3dgs/token --model spz/juyuan_v2.spz --width 1920 --height 1080 --profile rgba --frames 120 --concurrency 4 --output benchmark
~~~

1/2/4 卡对照需先 `deploy.sh stop`，再以原有 prefix/model/cuda/port、所需 `--devices` 与 `--reconfigure` 重新安装启动，分别运行 concurrency 1/4/8；实验结束恢复 0,1,2,3。不能只改 benchmark 并发数就声称改变了卡数。

~~~powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ForServer/WindowsClient/build.ps1
ctest --test-dir out/server-windows -C Release --output-on-failure
python -m unittest discover -s ForServer/tools -p gateway_test.py -v
New-Item -ItemType Directory -Force out/cloud-quality | Out-Null
foreach ($profile in @('rgba', 'jpeg95', 'jpeg90', 'jpeg85')) {
    & .\out\server-windows\Release\gs-client.exe --url http://10.129.4.104:8046 --allow-private-http 1 --token-file out/cloud-token.txt --model m-49fe972e55f40fb0 --width 1920 --height 1080 --profile $profile --repeat 31 --out "out/cloud-quality/$profile.ppm" | Out-File -Encoding utf8 "out/cloud-quality/$profile.jsonl"
    if ($LASTEXITCODE -ne 0) { throw "Quality capture failed: $profile" }
}
python ForServer/tools/cloud_quality.py --input out/cloud-quality --output out/cloud-quality/quality.json
.\out\cloud-client\Release\Native3DGSCloud.exe --smoke-test out/cloud-smoke-config.json
~~~

质量实验须分别用 rgba/jpeg95/jpeg90/jpeg85 运行 CLI，输出同名 PPM 与 JSONL 到同一目录后再分析。smoke 配置例子如下，`token_file` 只引用管理员提供的秘密文件，不把 token 本体写进配置、日志或命令行：

~~~json
{
  "url": "http://10.129.4.104:8046",
  "private_http": true,
  "token_file": "C:/Users/21544/workspace/Native3DGSViewer/out/cloud-token.txt",
  "output": "C:/Users/21544/workspace/Native3DGSViewer/out/cloud-ui-smoke"
}
~~~

限带宽复现：Windows 启动 `python ForServer/tools/paced_link.py --origin http://10.129.4.104:8046 --port 8899 --mbps 5 --delay-ms 40`，CLI 改用 `http://127.0.0.1:8899`、相同 token/model/view/profile，独立测量后停止代理。NumPy/Pillow/scikit-image 仅为离线质量分析依赖，不是服务依赖。完整样本/PPM/帧包保留于忽略的 `out/cloud-remote`、`out/cloud-quality`；入库证据为摘要与日志，不含令牌、私钥或模型本体。

## 8. 明确未验收/后续门槛

- 公网 TLS 部署、证书轮换、生产安全/压力审计未执行；当前为获准的可信私网 HTTP，token 与画面确实未加密，不能作为公网生产方案。
- systemd unit 路线未实机运行；已测试的是当前非 systemd 容器的 supervisor。容器重启自启动取决于管理员 entrypoint 配置，不假定后台进程能跨重启存活。
- 远端只上传并验证两份授权样本；没有本轮全部 38 个模型、约 2,200 万点大场景、Ubuntu 24.04 或移动/弱设备实测。
- 本轮多卡为请求级调度；无单帧跨卡合成、无实时视频编码、无连续视角帧率承诺。
- 缺少真实 WAN/丢包抖动、display-to-photon、WinUI compositor GPU 占用以及长期稳定性测试；当前指标不代替这些验收。
- 部署 staging/回滚代码已实现并审阅，缺失工具链/运行中安装/启动失败清理已实测；没有对每一种磁盘满、构建中断、证书错误和回滚阶段逐项故障注入。
