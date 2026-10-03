# instances-cache-v3：部署与验收记录

本轮实现均位于ForServer，原GUI/SDK不变。Go + SQLite + 单模型native实例已安装到已授权SSH测试机，WinUI客户端通过真实8046入口验证。验收覆盖单节点可信私网场景，未把历史四卡吞吐、公网部署或全部模型兼容性作为当前结论。

## 环境与运行版本

2026-10-03执行。Ubuntu22.04.5容器、PID1非systemd；4个设备实际报告RTX4090/CC8.9、驱动580.82.09、49140MiB/卡。本轮只配置GPU0，multi_gpu=false。CUDA13.0.88隔离工具链、G++12.3、Go1.27.1隔离官方archive/SHA256、系统SQLite3.37.2。没有替换宿主驱动、CUDA alternatives或系统Go。

SSH10.129.4.104:8045，应用8046→容器8888。运行prefix /opt/native3dgs，current指向releases/528cd9f342d7f117；源代码快照source-image-v3。token0600且不进入证据。运行标识/二进制SHA见[runtime.log](evidence/instances-v3/runtime.log)。最后新增的Go测试和文档不改变已安装native/Go二进制；源码后续同步不会声称release目录名是整个新文档快照的hash。

默认实例idle300秒、lease30秒/客户端10秒心跳、图片TTL86400秒、维护10秒、最多4实例、10GiB/100000帧。模型目录是启动快照，增加/替换需重启。已启用模型为changjin_v1.ply和spz/juyuan_v2.spz；后者为2670017点、52605719B，未遍历全部38个PLY/SPZ。

## 测试矩阵

| 检查 | 实际结果与证据 |
| --- | --- |
| Linux Release | CTest2/2组，11个frame/request +6个CUDA测试；[deploy.log](evidence/instances-v3/deploy.log) |
| Go并发与存储 | 8个实质测试+1个子进程helper，go test -race -count=1 -v、go vet通过；[go-tests.log](evidence/instances-v3/go-tests.log) |
| CUDA内存 | Compute Sanitizer12.4检查CUDA13构建的6个测试，0 errors；[gpu-memcheck.log](evidence/instances-v3/gpu-memcheck.log)；与最终版本相同GPU源码 |
| Windows Release | CTest2/2组，11个共享契约+3个Windows测试；[windows-build.log](evidence/instances-v3/windows-build.log) |
| 真实GPU生命周期 | 八用户一次渲染、租约/翻转、预测命中、idle/cache-hit无GPU、预测懒加载、TTL删除/再装载共6组；[lifecycle.json](evidence/instances-v3/lifecycle.json) |
| 外部生产入口 | Go API v3、GPU0、8046真实帧/会话检查；[production-integration.json](evidence/instances-v3/production-integration.json)，以及最终WinUI/latency |
| WinUI真实应用 | 10帧、4画质、2旧帧丢弃、Y翻转/极点、19次累计预取、1次local hit；[winui-smoke.json](evidence/instances-v3/winui-smoke.json)、[截图](evidence/instances-v3/winui-viewer.png) |
| WinUI拒绝路径 | 401错误token、未同意私网HTTP、不可达主机；[bad-token.json](evidence/instances-v3/bad-token.json)、[http-consent.json](evidence/instances-v3/http-consent.json)、[unreachable.json](evidence/instances-v3/unreachable.json) |
| 运行管理 | 真实install/staging/native/Go门禁、健康/重复start、脚本bash语法，当前available1/零闲置实例；runtime/deploy日志 |
| 源码与静态检查 | 32个C++/CUDA文件格式通过，Go全部gofmt、Python编译、源码空白/本地Markdown链接/JSON/凭据扫描通过；[static-checks.log](evidence/instances-v3/static-checks.log)；87个源码/文档文件本地远端SHA一致及多卡/无效GPU拒绝：[source-sync.log](evidence/instances-v3/source-sync.log) |

Go tests覆盖20并发共享render、磁盘重开/损坏/TTL续期及过期/LRU/orphan、租约保护及预测不续租、关闭租约拒绝、实例容量、前台优先、GPU等待取消活动数释放、shutdown新producer拒绝、未知schema拒绝。helper是独立真实socket进程，但fake model；真实GPU六组另列，不能互换。

Linux生命周期测试启动独立8891入口、21000起始worker端口及临时state，idle1秒/cache10秒/lease2秒/sweep100ms。以加速时钟验证删除机制，生产仍24小时TTL，未声称已等待24小时。八请求返回一个miss、七shared，只有一个PID/GPU0和一次native render。退出后缓存返回且实例为空；新live租约可为缺失邻域重启实例。Y反转相同a011-t38-d24码、不同图片SHA。

## 缓存延迟与持久化选择

消除可重建图片逐帧fsync及SQLite FULL同步，改write/close/atomic rename + WAL NORMAL。正常进程重启保留缓存；宿主突然掉电可能失去近期缓存，以CRC/SHA/文件存在检查重建。这不是凭据或权威业务数据的持久化策略。

2670017点模型、1920×1080 RGBA，20个不同离散相机各一次缺失后紧接一次命中，先预热。Windows同相机yaw182..220°、pitch14°、distance1；优化前后通过实验管理脚本仅使该模型1080pRGBA缓存过期，以保证20miss/20hit。wall为urllib读完全部响应，含网络/HTTP/编码/缓存处理，不含WinUI解码和屏幕呈现。

| 链路/版本 | miss p50/p95 ms | hit p50/p95 ms |
| --- | --- | --- |
| Windows→8046，原逐帧同步 | 220.656 /304.929 | 106.261 /135.139 |
| Windows→8046，NORMAL，等相机 | 74.182 /84.527 | 32.849 /46.699 |
| 服务器→127.0.0.1:8888，NORMAL | 34.251 /40.810 | 2.440 /2.808 |
| 服务器→自身映射8046，NORMAL | 39.593 /42.718 | 3.260 /3.694 |

原始数据：[before-windows.json](evidence/instances-v3/before-windows.json)、[equal-windows.json](evidence/instances-v3/equal-windows.json)、[loopback.json](evidence/instances-v3/loopback.json)、[mapped.json](evidence/instances-v3/mapped.json)。采样顺序会受网络/系统负载影响，20样本是短基准，非长期SLA。不可把loopback数字写成Windows端到端加速。

## 客户端预测、质量和负载

最后Release真实WinUI，viewport1920×854。预测默认四方向各4档；smoke等待当前预测循环完成，再相邻移动，cache=local、network_ms=0、decode+BGRA6.916ms。累计19包跨多个窗口，不等于最终窗口有19张。压缩cache1258852B，进程峰值工作集132530176B（126.39MiB），不等于GPU显存或长期最大内存。相同a015-t40-d41位置翻转后只改变flip_y；90°极点为a016-t00-d41，显示有效。

同一初始小模型相机/1920×854：RGBA272185B，JPEG95 71905B，JPEG90 62282B，JPEG85 58309B；解码分别约5.74/13.04/8.73/8.68ms。大模型当前翻转JPEG85邻居56019B、极点76931B。图像内容影响压缩；预取16张也会增加带宽，不能只报单帧变小就声称整体零开销。64MiB/256项缓存、单后台下载与前台优先给出资源上限，但没有测持续随机交互命中率或移动弱设备。

RGBA/JPEG像素算法与v2相同。等质量PSNR/SSIM及CLI/D3D12对照见[历史v2编码验收](verification-image-v2.md#4-编码带宽质量与-windows-端侧负载)；这些是旧固定相机数据，本轮未重复整套质量统计，也不将CLI呈现当WinUI compositor GPU负载。截图已人工检查工具栏、状态、居中图像和极点显示。

## 复现

~~~bash
cd /opt/native3dgs/source-image-v3/ForServer/gateway
CGO_ENABLED=1 GOTOOLCHAIN=local /opt/native3dgs/go-toolchain/go/bin/go test -race -count=1 -v ./...
/opt/native3dgs/go-toolchain/go/bin/go vet ./...
cd /opt/native3dgs/source-image-v3
python3 ForServer/tools/cloud_v3_test.py --gateway /opt/native3dgs/current/bin/gs-gateway --server /opt/native3dgs/current/bin/gs-server --models /opt/native3dgs/models --token-file /opt/native3dgs/token --output /opt/native3dgs/bootstrap/v3-lifecycle-repeat.json
~~~

该命令使用独立临时cache/state，不修改生产期限；端口8891/21000需空闲。外部重复共享冷缺失测试需新视图或临时状态，既有缓存会使预期render增量不再为1。latency脚本默认yaw180；重跑既有视角均可能hit，只有明确受控测试才用--expire-database操作可重建缓存索引，不用于业务数据库。

~~~powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ForServer/WindowsClient/build.ps1
ctest --test-dir out/server-windows -C Release --output-on-failure
python ForServer/tools/cloud_v3_latency.py --url http://10.129.4.104:8046 --token-file out/cloud-token.txt --output out/v3-latency-repeat.json
.\out\cloud-client\Release\Native3DGSCloud.exe --smoke-test C:\path\smoke.json
~~~

smoke配置含url/private_http/token_file/output的绝对路径，token只在受限文件，不能写进JSON/命令行。生产部署完整说明在[README](../README.md)。

## 尚未验收

当前管理bearer和查看lease不等于账户/租户RBAC；8046仍是明确批准的可信私网明文调试。公网TLS/证书轮换、安全审计、长期大并发、多节点数据库/分布式锁不是已验收能力。systemd主机、外部容器entrypoint/restart/完整回退故障注入未实测；未改变现有容器编排。日志/DB/release不受图片预算限制，运维需轮转与磁盘配额。

完整38模型/2200万点、Ubuntu24.04、弱设备/WAN、实体输入/多DPI/可访问性/resize完整矩阵、真实网关重启的WinUI410恢复、display-to-photon与compositor显存须另验。多卡能力保留但本轮关闭，历史1/2/4卡吞吐不是当前新实例架构的横向扩展验收。
