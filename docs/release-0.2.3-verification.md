# Windows 0.2.3 发布验证（2026-10-28）

本轮更新 Windows 本地查看器与 C++ SDK；Android、Cloud 和 Web 直接复用 v0.2.2 Release 中现有文件，保留原命名，不重新构建、不修改内容。最终资产命名与范围见[发行说明](release-0.2.3.md)。没有单独的 SHA256 附件。

## 构建与测试

环境：Windows x64、VS 2026 / MSVC 19.50、Windows SDK 10.0.26100.0、CUDA 13.3、RTX 3080 10GB。产品版本 CMake、安装器和 GUI 启动日志均更新到 0.2.3。CUDA DLL 静态链接 cudart，附 cudart / CCCL 许可证；宿主不链接 CUDA。

```powershell
pwsh -NoProfile -File CudaOperators/tools/build.ps1 -Targets gs-cuda-backend,Native3DGSViewer.CudaTests,Native3DGSViewer.Tests
& 'C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe' Native3DGSViewer.sln /restore /m /p:Configuration=Release /p:Platform=x64 /v:minimal
ctest --test-dir out/cmake-cuda -C Release --output-on-failure -j 1
pwsh -NoProfile -File packaging/build-installer.ps1 -SkipBuild -EnableCuda
```

上述 CUDA 发行必需目标、完整解决方案和安装器均构建成功。CTest **14/14** 组通过，149.24 秒，含隔离的 AdaptiveSortProduction。该组五项正常配置测试全部执行；全量 RenderCore 进程已经开启全局 Debug Layer 时，CUDA 专属测试按策略跳过，隔离组提供实际覆盖。

初次 Ninja `all` 在原生 VS 测试桥链接处遇到带空格路径解析失败；没有将其记为成功。正式流程改为显式构建 CUDA/产品测试目标，并由上面的标准 MSBuild 解决方案成功构建 VS 测试桥。未修改此旧 Ninja 辅助路径。构建仍有既存 CUDA DLL 接口及第三方 CMake 警告；独立消费工程的系统临时目录会触发 MSB8029 提示。

## SDK 实际消费

本轮打包时发现并修正两个问题：将 CUDA toolkit 许可证路径规范为 CMake 正斜杠，避免安装脚本的 `\U` 转义错误；让 `native3dgs_deploy_runtime` 同时部署可选 DLL 和许可证。新增部署断言先在旧函数上确认缺 DLL 失败，再验证修正后的实际压缩包。

向同一全新 prefix 安装 `SDK` 和 `CUDA` 组件，压缩根目录内容。独立测试解压、移到新目录、配置并编译消费工程，在没有 toolkit 链接依赖的情况下运行；核对 DLL/许可证已自动部署，并测试校准导入和 GPU 测距：

```powershell
pwsh -NoProfile -File tests/sdk/installed-sdk.ps1 -BuildDirectory out/cmake-cuda -PackageArchive out/release-0.2.3/sdk-candidate.zip -Scene C:/Users/21544/Desktop/zhishan/changjin_v1.ply -ExpectedCudaActive false
pwsh -NoProfile -File tests/sdk/installed-sdk.ps1 -BuildDirectory out/cmake-cuda -PackageArchive out/release-0.2.3/sdk-candidate.zip -Scene C:/Users/21544/Desktop/zhishan/zhihuizhimen.ply -ExpectedCudaActive true
```

两次通过：170,799 点场景 `cuda_available=true cuda_sort_active=false`；大场景 `cuda_available=true cuda_sort_active=true`。两次均输出 `MEASUREMENT OK` 并正常关闭。最终发行 ZIP 在候选包基础上只补齐本验证文档；所有原有非文档条目逐字节核对，未修改被测库、DLL、头文件、shader、CMake 配置或样例代码。

## 安装包

`Native3DGSViewer-0.2.3-Windows-x64-Setup.exe` 使用 Inno Setup 6 构建，包含 CUDA DLL、两份通知和 WinUI/VC 运行库。静默安装到工作区专用 `out/release-0.2.3/installed-viewer`，核对 EXE、辅助程序、DLL、许可证和 shader 与 staging 相同。实际安装的查看器加载 170,799 点和 3,914,609 点，分别记录 CUDA inactive/active、`app_start version=0.2.3`、正常 `app_exit code=0`。未做提示像素检查。

首次烟雾脚本按 DisplayName 精确等于 `3DGS Viewer` 查询失败；Inno 实际名称含 `version 0.2.3 (Current user)`。更正为固定 AppId 注册表路径后重跑，确认 DisplayVersion=0.2.3；静默卸载通过，测试注册表项已移除。此为脚本断言问题，未改动安装包，也未替换已有用户安装。

## 资产与限制

六个沿用资产从 v0.2.2 直接下载，并与 GitHub 提供的内容摘要核对一致；没有使用旧 Windows 本地 SDK 或本地查看器安装包。保留 Android 0.2.2、Cloud 0.2.2 和 Web preview.4 原版本名称，避免把旧二进制标成新构建。Release 共八个资产，使用显式清单上传，排除 `.sha256`。

本轮构建/CTest/SDK/安装与卸载日志在本机 `out/release-0.2.3/`；可重现的 CUDA 性能原始数据、图像和正常/故障集成证据见[优化报告](windows-cuda-adaptive-sort-2026-10-28.md)及其链接。本轮未重跑沿用平台测试；其他 GPU/驱动、两小时长稳和干净系统安装仍未验证。旧 v0.2.2 资产及 tag 保留，可以回退；C++ SDK 回退应成套替换头文件和静态库并重新编译。
