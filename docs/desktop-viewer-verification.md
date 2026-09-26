# 桌面查看器验证记录

日期：2026-09-26。环境：Windows 11 x64、Visual Studio 2026、Windows SDK 10.0.26100.0、Windows App SDK 1.8.260921001。

## 已验证

- Release x64 解决方案构建成功，生成 `out/Release/Native3DGSViewer.GUI.exe`。
- `ctest --test-dir out/cmake -C Release --output-on-failure`：ModelIo、RenderCore、ModelRenderIntegration、Engine、InstalledSDK 五组通过。Engine 组覆盖 X/Y/Z 各轴组合的几何关系与加载期间翻转状态保持。
- `packaging/build-installer.ps1 -SkipBuild` 生成自包含 Inno Setup 安装包；包内含模型辅助进程、9 个 shader、Windows App SDK 文件、应用本地 VC 运行库与第三方许可。
- 安装包静默安装到独立目录后，使用父目录的 `1.ply` 启动程序；窗口正常关闭且退出码为 0；静默卸载成功。
- 用户使用自己的模型确认先前查看效果良好。当前版本的 SDK 接入、三轴翻转和固定模式拖拽方向尚待用户以该模型重新人工确认。

本次安装包 SHA-256：`ABB4E50C315C9119383190C64309C314C4341C2670EFAC99E490B3F78DA3B6C3`。产物位于 `out/installer/Native3DGSViewer-Setup-x64.exe`，属于本机构建产物，不纳入 Git。

## 待验收

Intel Arc 的第二台 Windows 11 机器已验证基本安装、SPZ/PLY 加载和浏览；跨 DPI/多显示器、设备移除、无障碍、与 Spark 的画质截图对照及等画质性能基准尚未完成。安装包未签名，正式分发需另行处理代码签名。

## 2026-09-27 Intel Arc 复测

用户提供的 Windows 11 23H2 build 22631 日志显示约 32 GB 物理内存、Intel Arc UMA、wave8-32。固定 wave32 排序自检通过，交换链正常绑定，SPZ 请求 1 和 PLY 请求 2 都进入 `scene_ready`，切换后正常关闭，日志无错误。用户确认多个模型浏览流畅；这证明本机型的基本加载和交互路径可用，不代表完整画质或性能验收。Y 镜像时的鼠标方向回归由随后版本修复，仍需在该机器人工确认。

诊断经验：旧版把 UMA 设备上可能无效的 non-local 预算作为上传条件，导致 `OutOfVideoMemory` 且 HRESULT 为 `S_OK`；新路径以 D3D12 UMA 能力决定是否需要该段，并在拒绝时记录 local/non-local 的预算、用量和估算需求。窗口指针增量必须在未应用视口镜像的根布局坐标中计算。日志只在启动、场景状态变化和错误时写入，不采集逐帧数据。

0.1.1 正式版在本机 Release 构建与五组 CTest 通过；隔离目录静默安装后从安装目录打开 `1.ply`，日志记录版本 0.1.1、自检通过及 `scene_ready` 的 1,179,648 点和 SH0。安装目录中的日志按启动时间命名，正式版安装包不包含构建机日志。翻转 Y 后固定模式的拖动方向仍待目标设备人工复核。
