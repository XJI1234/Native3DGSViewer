# Cloud Windows 0.2.2 修订验收

2026-10-03，在 Windows 本机和已授权 Ubuntu22.04.5/RTX4090 远端完成。本报告覆盖当前修订；旧 instances-v3 / image-v2 证据保留为历史记录，不代表当前默认值。实现范围为 ForServer、根 README 和 0.2.2 发布说明。

## 交互与会话

屏幕水平和垂直拖动、方向键及按钮复用相同导航函数；正常、倒置及穿极点有契约回归测试，反转 Y 不修改球坐标基准。默认 JPEG85，预渲染低/中/高每方向5/10/15档，默认低。81个距离档及2°球面网格保持原契约。

租约120秒、10秒心跳，控制请求使用短超时及有限重试，连续3轮失败且30秒没有成功才提示。真实410自动重建会话、清图片并刷新当前视角。帧传输改为异步WinHTTP，取消由请求所属线程处理，等关闭回调后释放状态和缓冲区；用户退出后清压缩缓存并执行有界最终租约释放。无持久下载目录。

## 验证

| 项目 | 结果与证据 |
| --- | --- |
| Windows Release / D3D12 debug | CTest2/2组；15个共享契约、6个Windows测试，含响应头卡住时3秒内取消、分配失败恢复及提交后资源保留；[windows-tests.log](evidence/cloud-022/windows-tests.log) |
| Linux Release / GPU0 | CTest2/2组；16个共享契约及6个CUDA测试；Go race/vet；11个Python网关/部署测试及bash语法；[deploy.log](evidence/cloud-022/deploy.log) |
| 并发与生命周期 | 真实GPU六组：八用户共享一次渲染、租约/朝向、预测命中、空闲卸载及磁盘命中无GPU、预测懒加载、TTL删除后再装载；隔离端口/临时状态未改生产TTL；[lifecycle.json](evidence/cloud-022/lifecycle.json) |
| 安装后静置 | 65秒静置、7次成功心跳、11帧、4画质和倒置/极点、2次本地命中，进程退出0、关闭缓存0；[idle.json](evidence/cloud-022/idle.json) |
| 恢复故障注入 | 真实模型传输，经回环代理注入一次503及关闭本次客户端自己的租约后返回真实410；45秒静置、4次成功心跳、1次恢复、12帧、退出0、关闭缓存0；[recovery.json](evidence/cloud-022/recovery.json)、[proxy.json](evidence/cloud-022/proxy.json) |
| 启动/失败/卸载 | 安装目录和仓库目录启动均成功；不可达入口退出1、0帧；所有关闭缓存0；silent install与uninstall退出0；[installed-checks.json](evidence/cloud-022/installed-checks.json) |
| 缓存吞吐工具 | Go网关全hit的4帧测试可正常报告，不假定native stats存在；报告明确是固定视角缓存HTTP吞吐；[cache-benchmark.log](evidence/cloud-022/cache-benchmark.log) |
| OCR | 全模块55项、42条；修复后复审33项5条；最终复审7项1条测试watchdog问题，已修复并重跑CTest。除已验证的Go版本误报，其余全部处理；[处置记录](evidence/cloud-022/ocr-dispositions.json) |

截图：[安装后WinUI](evidence/cloud-022/winui-viewer.png)。原始日志及各场景关闭缓存记录在同目录；安装包hash/尺寸见[installer.json](evidence/cloud-022/installer.json)。smoke报告和截图写失败也返回1；不能只靠进程成功判断质量。

## 审查处置

有效问题包括独立64个共享producer预算、预测失败后的前台重试、实例GPU均衡放置、worker实际路径/digest固定、部署事务回退及有效配置GPU选择、绑定地址健康探测、端口/根目录验证、只解包已校验CUDA文件、NuGet/VC运行库发现、缓存查询避免复制与坏包驱逐、异步取消、最终租约释放、真实失败退出码、RGBA/JPEG语义校验及写失败、D3D12异常资源生命周期、测量及历史工具的输入/缓存统计边界。

OCR声称2026年10月Go1.27.1未发布，与本机保存的官方go.dev metadata、archive实际SHA及远端go version冲突，因此该条为误报；[Go证据](evidence/cloud-022/go-toolchain.json)。没有把最后复审的测试问题描述成OCR零问题：已补失败清理和CTest30秒进程watchdog，Windows回归通过。

## 部署与发布

远端4卡设备只启用GPU0，多卡保留而关闭。入口8046映射容器8888；C++20/CUDA13.0.88/CUB、Go1.27.1、SQLite WAL。安装日志包含当前release路径，实际二进制SHA见[runtime.log](evidence/cloud-022/runtime.log)。测试后补充的验收文档/证据不改运行二进制；release目录名标识安装时快照，不宣称覆盖以后新增文档。

独立产品 Native3DGSCloudViewer-0.2.2-Windows-x64-Setup.exe，每用户安装，携带自包含WinUI、VC runtime和许可，不覆盖原Windows Viewer。补充到既有v0.2.2 Release；保留原tag及四个Windows/Android资产，云端源码使用Release中列出的后续合并提交。

此安装测试在开发机完成，尚未做干净Windows镜像、多DPI/实体输入完整矩阵、弱设备和真实WAN长稳测试。安装包未签名，当前为可信私网单节点预览；公网TLS/RBAC、外部容器重启编排、长期负载和日志轮转仍需独立验收。异步HTTP关闭回调及D3D12 CLI异常后安全排空依赖操作系统/驱动完成；本次覆盖可控故障，不宣称验证所有GPU设备移除情形。
