# render-core 验证记录

2026-09-26，Windows 11 x64，VS 2026 / MSVC 19.50，CMake 4.3.2，clang-format 20.1.8。
本机硬件为 NVIDIA GeForce RTX 3080，驱动 32.0.16.1656，FL12.0、SM6.0、WaveOps，wave lane 32。
模块契约见 [SPEC-render-core](SPEC-render-core.md)，源码入口为 `include/render-core/renderer.h`。
本记录保留初版 f328bd7 的证据；SDK 完善后的数值回归、相机/引擎测试、当前 shader 哈希与基准见 [engine-sdk-verification](engine-sdk-verification.md)。

## 构建与模块测试

```powershell
git submodule update --init --recursive
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' .\Native3DGSViewer.sln /restore /m /p:Configuration=Release /p:Platform=x64
ctest --test-dir out/cmake -C Release --output-on-failure
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe' .\out\Release\Native3DGSViewer.Tests.dll /Platform:x64
```

Release x64 构建成功。`/restore` 的无 NuGet 项目警告和上游 zstd CMake 兼容性提示不影响构建。
CTest 三组全部通过，无跳过；详细输出在 `out/cmake/Testing/Temporary/LastTest.log`。

| 测试组 | GoogleTest 数量 | CTest 时间 | 结果 |
|---|---:|---:|---|
| ModelIo | 19 | 6.63 s | 通过 |
| RenderCore 独立模块 | 23 | 26.84 s | 通过 |
| ModelRenderIntegration 联合模块 | 1 | 8.71 s | 通过 |

独立运行命令为 `out/Release/Native3DGSViewer.Tests.exe --gtest_filter=Render*`；联合运行使用 `--gtest_filter=ModelRenderIntegration.*`。
VS 原生测试 DLL 启动同目录的完整 GoogleTest 可执行文件，桥接测试数量为 1，不代表只有 1 项逻辑测试。
ASan Debug 全量 43/43 通过（117.920 s），无跳过，未报告 AddressSanitizer 错误。最终 VSTest 桥接测试 1/1 通过，运行完整 43 项 GoogleTest，总时间 41.3781 s。

## 验证范围

- CPU 契约：相机/FOV/近远面/单位四元数、有限数、质量边界、SH 长度、场景数量上限、double 原点先减后转 float、增量预算 80% 余量与 UINT64_MAX 边界。
- GPU 排序：与 `std::stable_sort` 逐索引比对；0/1、127/128/129、511/512/513、4097 同键、100 万与 800 万键；有效点原始索引顺序稳定。原版 FidelityFX 八次 4-bit pass，无 CPU 排序回退。
- 图像：128x128 Gaussian 每像素解析对照（8-bit 误差 <=1.5）、远近透明叠加、SH 0-3、SH cap/clamp、double 远原点像素相同、旋转各向异性协方差/blur 主轴、近面/画面边缘/溢出、Radial/ViewDepth 顺序。异常投影计数与普通裁剪区分。
- 生命周期：copy fence 完成字节进度、首次成功 Present 后激活、失败/取消保留旧活动场景、零视口暂停、过期 revision/generation、100 次替换/取消/clear、detach、跨线程 camera/resize/stats 命令。
- 恢复：实际 `ID3D12Device5::RemoveDevice`、同 LUID 重建、保留 CPU 场景重传、永不完成 direct fence 的 5 秒界限、取消后 copy fence 超时、OOM/预算拒绝、重绑 10 秒超时终止、恢复重传失败终止。DeviceLost 后宿主释放旧 COM 引用，再继续调用 render_frame。
- GPU 测试开启 D3D12 Debug Layer/DRED 并检查错误消息；性能进程关闭诊断。未启用 GPU-based validation，也未做长期泄漏检测；ASan 不替代这两项。

生产帧图仅异步读回 timestamp 和绘制/拒绝计数，不读回排序数组；完整排序与图像 readback 只在测试/基准辅助代码执行。
Present 后另发 direct fence，覆盖 DXGI 在 Present 中追加的队列工作，才允许释放 back buffer。

## 联合语料与截图

测试直接将 model-io 返回的不可变 SceneHandle 交给 renderer；验证实际上传总字节、SceneReady、composition swapchain Present 与独立离屏图像非空。
语料位于父工作区，未纳入仓库；缺失时联合语料测试会跳过，本次全部存在。

| 样本 | 点数 / SH | 候选点数 | SHA-256 |
|---|---|---:|---|
| `../1.ply` | 1,179,648 / 0 | 1,179,648 | `A0917F8B7D15D9D07B802BA3E937365E8557EDF292519706F70064CDE3EDB73A` |
| `../Viewer_android/public/scene/jidaoshan.spz` | 509,812 / 3 | 509,340 | `1E16BB8F5C9CE1D41A5BAAED84058720A8327B2FE029AAF4EBD78519F765C367` |
| `../Viewer_android/public/scene/zhihuizhimen.spz` | 3,914,609 / 3 | 3,904,214 | `FFFB4553592824703B02A551BAF5BDE60F19452C65B754F920F17CE5DB27827C` |

离屏图像为 BGRA8 UNORM、透明黑底；Gaussian 和 overlap 为 128x128，真实语料为 512x512。
统一质量：Radial、SH cap=3、stddev=3、alpha cutoff=0、blur variance=0、max radius=1024、预乘 alpha。
相机 identity xyzw=(0,0,0,1)，FOV=π/3，near=0.01；真实语料按 bounds 中心和 `radius=max(bounds尺寸)/2+maxScale*3` 定位，Z 增量为 `radius/sin(FOV/2)+radius`，far=max(10000,radius*10)。这与测试源码一起固定可重放视角。

直接从项目根运行测试后，BMP 输出为 `out/render-tests/gaussian.bmp`、`overlap.bmp`、`sample-0.bmp`、`sample-1.bmp`、`sample-2.bmp`。CTest 工作目录为构建目录，其输出对应 `out/cmake/out/render-tests/`。
三份真实样本的本地图像已目视检查，均非空；保守 fit 会留下较大边距。未与 Spark 同相机截图比较。

## GPU 排序微基准

```powershell
.\out\Release\Native3DGSViewer.SortBench.exe 8000000 3
```

RTX 3080 / wave32，Release，诊断关闭，800 万 uint32 key/value，GPU timestamp 测量全部八次 radix pass，结果为 4.73907、4.12877、4.74317 ms。
原始数值见 [CSV](evidence/render-core/gpu-sort-rtx3080.csv)。首轮随机键（mt19937 seed=42），后两轮重用已排序数组；没有 Viewer 暖机/采样流程。这是排序微基准，不能据此判定整帧门槛通过。

## 固定依赖与 Shader

FidelityFX Parallel Sort MIT，提交 `0c539948c8d196ae338d91efbc8ca495f1ea0d1d`，许可在 submodule 的 LICENSE.txt。
DXC 使用 Windows SDK 10.0.26100.0，dxcompiler/dxil 1.8.2502.11；dxc.exe SHA-256 为 `7C6918A0E2D4E437629FA8549F5CE800970494780F363BBBE1E3D3034F435AEE`。固定目标 cs_6_0/vs_6_0/ps_6_0，选项 `-O3 -WX -Ges`。
布局 FrameConstants=160 字节，Ellipse=48 字节，间接参数前 16 字节为 D3D12_DRAW_ARGUMENTS、后 4 字节为投影拒绝计数。

| 编译产物 | SHA-256 |
|---|---|
| count.cso | `FFF1C78B23A11350EB101C1B7C0998D830CAF5ECC9BC53EFD7CD0967066C48DD` |
| pixel.cso | `840234B9959CF49E8D573F997BFF529DBE0FCBED761579ED0D2774274D7771D2` |
| project.cso | `994C6A549E161CA1D85AA7C50416170467DC9D9CF414C70CDB099C4D2E8D8691` |
| reduce.cso | `10684FB17FCECC3D42C925B7544798C54B9BDCBC315B8ADD5210B707B9AF8E2C` |
| reset_args.cso | `ECC671C9410BCAD6A9996A3D03D6F28193E5C7698F0A796A0BD1386D434E4DEA` |
| scan_add.cso | `01848D5453239DA5047CC2B36042E685F15155CB8D6C08ADB3EA64A747884332` |
| scan.cso | `407F8644285BDA1820B29EFCFCFB0000BACA35646F941941263614AAEAC82C4C` |
| scatter.cso | `2939C935585CD7A68A760E28B21D6BC3C90E7020222E4995B158C719178F4500` |
| vertex.cso | `FDE81D35D64C4A9911B7B718B401A358E88F618C49E40D0C77A6683FCD9E4371` |

## 尚未完成的验收

模块测试通过不等于首期产品整体验收通过。以下门槛保留：

- Spark 同相机、同质量、同颜色空间截图 SSIM >=0.95 与人工比较。
- RTX 3080 小/中/大矩阵、1920x1080、同相机路径，30 秒暖机、60 秒采样、至少 3 轮 PresentMon/Viewer 基线；中位帧时间 <=0.8 基线且 1% low FPS 不下降。
- WinUI SwapChainPanel 可见合成、DPI/窗口输入与 UI dispatcher 恢复；当前只测试未绑定控件的 composition swapchain。
- AMD/Intel/其他 NVIDIA 的硬件与 wave size 矩阵；本机只验证 RTX 3080 wave32。
- 完整渲染/恢复的峰值显存、长期泄漏检测、GPU-based validation 与 PIX 帧图检查。

## ASan 复现

```powershell
cmake -S . -B out/asan -G 'Visual Studio 18 2026' -A x64 -DCMAKE_C_FLAGS='/fsanitize=address /Zi' -DCMAKE_CXX_FLAGS='/fsanitize=address /Zi /EHsc' -DCMAKE_C_FLAGS_DEBUG='/Od /Zi' -DCMAKE_CXX_FLAGS_DEBUG='/Od /Zi' -DCMAKE_EXE_LINKER_FLAGS='/INCREMENTAL:NO' -DCMAKE_SHARED_LINKER_FLAGS='/INCREMENTAL:NO'
cmake --build out/asan --config Debug --parallel 8
$env:Path = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.50.35717\bin\Hostx64\x64;' + $env:Path
$env:ASAN_OPTIONS = 'halt_on_error=1'
.\out\Debug\Native3DGSViewer.Tests.exe --gtest_brief=1
```
