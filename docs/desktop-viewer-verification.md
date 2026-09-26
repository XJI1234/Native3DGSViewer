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

尚未在干净的第二台 Windows 11 机器验证安装，尚未完成跨 DPI/多显示器、设备移除、无障碍、与 Spark 的画质截图对照或等画质性能基准。安装包未签名，正式分发需另行处理代码签名。
