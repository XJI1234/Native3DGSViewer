# 桌面查看器验证记录

日期：2026-09-26。环境：Windows 11 x64、Visual Studio 2026、Windows SDK 10.0.26100.0、Windows App SDK 1.8.260921001。

## 已验证

- Release x64 解决方案构建成功，生成 `out/Release/Native3DGSViewer.GUI.exe`。
- `ctest --test-dir out/cmake -C Release --output-on-failure`：ModelIo、RenderCore、ModelRenderIntegration、DesktopViewer 四组通过。
- `packaging/build-installer.ps1 -SkipBuild` 生成自包含 Inno Setup 安装包；包内含模型辅助进程、9 个 shader、Windows App SDK 文件、应用本地 VC 运行库与第三方许可。
- 安装包静默安装到独立目录后，使用 `hornedlizard.spz` 启动程序；向窗口发送正常关闭消息后进程退出；静默卸载成功。
- 用户使用自己的模型确认现有查看效果良好。Z 镜像的相机空间关系由非默认角度测试覆盖。

本次安装包 SHA-256：`8830F84A49A7A84E2905AE43BA0B9111F2C69695AA4CD85B21753BD91ECB24BB`。产物位于 `out/installer/Native3DGSViewer-Setup-x64.exe`，属于本机构建产物，不纳入 Git。

## 待验收

尚未在干净的第二台 Windows 11 机器验证安装，尚未完成跨 DPI/多显示器、设备移除、无障碍、与 Spark 的画质截图对照或等画质性能基准。安装包未签名，正式分发需另行处理代码签名。
