# 引擎与 SDK 验证记录

2026-09-26，Windows 11 x64，VS 2026/MSVC 19.50，CMake 4.3.2，clang-format 20.1.8。硬件为 RTX 3080、驱动 32.0.16.1656、wave32。规格见 [SPEC-engine-sdk](SPEC-engine-sdk.md)，消费步骤见 [SDK-guide](SDK-guide.md)。

## 实现范围

首期所有非 UI 组件：PLY/SPZ 隔离解码、共享不可变场景、D3D12 渲染核心、double 相机控制、异步加载/上传事务、渲染线程、宿主 surface 生命周期、快照/有界事件、异步关闭及 SDK 包。桌面窗口、Picker/拖放和键鼠事件适配由后续宿主实现。F1-F5 格式扩展、LoD、多模型、编辑和动画仍属于后续计划。
修改投影 shader 后已目视核查三份真实样本的 512×512 BMP 均非空，内容结构可辨；固定相机与 Spark 对照仍待执行，目视核查不替代 SSIM。

核心回归包括：稳定 Gram 行列式避免细轴特征值消减误差；attach 保持提前提交的 viewport；相同 camera/viewport 复用排序；上传页/copy fence 封装；明确 GpuTimeout 分类；错误码即时捕获；CPU/HLSL 布局断言；生产库与测试 hook 编译隔离。

## 模块与联合测试

Debug ASan 最终全量 **62/62** 通过（147.191 s），无跳过或 ASan 报告。模块分布：model-io 19、render-core 28、model/render 联合 1、相机 6、引擎运行层 8。真实 PLY/SPZ 点数及哈希沿用 [render-core-verification](render-core-verification.md) 的三份固定语料。

相机测试验证保守 fit、窄高视口、轨道/平移/dolly/reset、飞行 dt 上限和对角线速度、模式保持姿态、非法输入回滚、远原点与可重复输入回放。引擎测试使用实际二点 PLY、硬件 GPU 和 composition swapchain，验证真实 decode→upload→Present、请求替换/取消、失败保留旧模型/相机、关闭、有界事件、并发命令及 shutdown。实际 RemoveDevice 验证旧代确认拒绝、宿主释放确认后重建与重绑；不确认在十秒后进入持续错误。
最终 Fit/Reset 回归确认：缩为窄视口后显式 Fit 改变距离，Reset 仍精确恢复首次激活视角；Fit 保留当前 Fly 模式。修复后 Release 引擎测试 13/13 通过（12.382 s）。
Fit/Reset 回归的 ASan 引擎测试 13/13 通过（13.876 s）；随后增加恢复期 Close 测试，最终全量 62/62 结果见上。
OCR 定向复审发现恢复期间 Close 可能收到 ticket 0 的 SceneCleared 而旧活动 ticket 仍非零，导致 Closing 永久挂起；已补真实 RemoveDevice/重建但未重绑 surface 的回归，确认 Close 清空状态。引擎测试现为 14 项。最后一轮 CTest 5/5 通过（74.37 s）；负向 SDK 消费指定不存在文件，确认返回错误并执行 finally 清理，没有遗留临时目录。

ASan 使用 `NATIVE3DGS_ENABLE_ASAN=ON`，覆盖真实解码 helper、所有引擎/渲染实现及测试。专门模拟内存耗尽的 fault helper 不加 ASan：该 helper 故意触发 128 MiB Job 限额，全局插桩会干扰 sanitizer 本身的运行，首次全局插桩运行在此测试超时。移至目标级插桩后，限额测试单独 24 ms 通过，全量通过；真实 decoder 的插桩保留。

```powershell
cmake -S . -B out/asan -DNATIVE3DGS_ENABLE_ASAN=ON '-DCMAKE_CXX_FLAGS=/Zi /EHsc' '-DCMAKE_C_FLAGS=/Zi'
cmake --build out/asan --config Debug --parallel 8
$env:Path='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.50.35717\bin\Hostx64\x64;' + $env:Path
$env:ASAN_OPTIONS='halt_on_error=1'
./out/Debug/Native3DGSViewer.Tests.exe --gtest_brief=1
```

最终 Release/VSTest 桥接 **1/1** 通过（56.1040 s），运行全部 62 项 GoogleTest，无失败。最终 CTest **5/5** 通过，无跳过，74.37 s：model-io 5.86 s、render-core 29.95 s、联合 9.08 s、engine 13.16 s、SDK 16.32 s。独立 SDK 在系统 temp 目录重定位、配置、构建并加载真实 1.ply；消费工程仅访问 SDK。消费输出 `SDK OK frames=40 active=1 splats=1179648`，断言要求 GPU 候选点计数大于零。生产 render-core.lib 经 dumpbin 检查不含 testing_build/RendererTestControl/create_renderer_for_testing；导出 CMake 文件不含仓库绝对路径、测试或第三方源码目录。

固定 MSBuild Release x64 命令已成功，0 error；现有 `/restore` 无 NuGet 项目两项警告以及上游 zstd 兼容性提示保留。OCR 对初始 render-core 提交完整审查 21 文件、25 条（中等 7、低 18），有效问题已修复或回归覆盖。SDK 工作区第一轮审查 24 文件、15 条，因 token 预算有 8 文件未完成；第二轮审查 26 文件、15 条（中等 1、低 14），4 个 camera/engine 文件因单组超时。定向复审完成剩余 4 文件，5 条（中等 1、低 4）。中等级发现为 SDK 消费失败时未清理目录和恢复期间 Close 的旧 ticket 状态错误，均已修复并有回归。有效低等级建议包括 Fly/Pan 模式一致性、请求取消进度清理、公共头直接依赖和初始化 resize 错误检查，亦已修复。未发现高/严重问题。OCR 的工具调用期间有错误读取不存在路径的噪声，审查证据仍按实际覆盖文件和最终结果记录。

## SDK

Release `/MD`、Windows x64、匹配 MSVC 19.50 的四个静态库、五个公共头、helper、九个 shader、CMake Config/Version/Targets、console 宿主、使用说明与五项运行依赖的许可证。没有测试 hook、GoogleTest、源码绝对路径或第三方编译需求。SDK 不承诺跨编译器 STL ABI；VC runtime 由部署宿主安装。ZIP 通过 CPack SDK component 生成，产物位于 `out/packages/Native3DGS-SDK-0.1.0-windows-x64-SDK.zip`。
ZIP 独立解包、重定位、消费构建与真实 PLY 运行已通过，输出 `SDK OK frames=35 active=1 splats=1179648`。VS 在系统 temp 中构建给出 MSB8029（增量构建目录警告）；消费验证是单次全新构建，无错误。测试成功后移除临时目录。

## GPU 阶段基准

统一 1920×1080、Radial、实际 SH 0/3、SH cap=3、stddev=3、alpha=0、blur=0、radius=1024、BGRA8 UNORM 预乘混合。每 case 预热 60 帧，采样 300 帧；固定相机 cached/force 渲染相同场景和画质。orbit 每帧相同一像素轨道增量，每帧重新投影/排序。全部使用硬件 GPU timestamp、无 CPU 排序回退；独立进程串行运行。

| 样本 | 每帧重排 GPU 中位 ms | 固定相机复用 ms | orbit ms |
|---|---:|---:|---:|
| jidaoshan.spz，509,812 / SH3 | 1.09 | 0.38 | 1.09 |
| 1.ply，1,179,648 / SH0 | 1.56 | 0.87 | 1.57 |
| zhihuizhimen.spz，3,914,609 / SH3 | 8.81 | 2.87 | 8.81 |

原始九份 CSV 和当前 shader SHA-256 在 [evidence/engine](evidence/engine)。这些数据验证固定视角避免投影/排序工作的收益；重排缓存在原核心中已有，本次去重改进保证重复相同相机命令也不失效。数据不代表本次变更相对上一版本所有场景都提速，也不证明 Viewer 的 20% 门槛。排序/投影与绘制分阶段数据可用于后续 PIX 定位。

## 外部验收边界

尚未完成：Spark 固定相机 SSIM、实际可见 WinUI SwapChainPanel/DPI、PresentMon 同画质 30 秒暖机/60 秒采样/三次重复、AMD/Intel/其他 NVIDIA 实机、干净机器 VC runtime 部署、长期泄漏和 GPU-based validation。这些门槛保留为未验收。当前测试 surface 未绑定桌面控件，阶段基准是离屏诊断，不报告用户可见帧率胜出。
