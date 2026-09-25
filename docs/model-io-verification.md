# model-io 验证记录

2026-09-26，Windows 11 x64，VS 2026 / MSVC 19.50，CMake 4.3.2，clang-format 20.1.8。

## 构建与测试

```powershell
git submodule update --init --recursive
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' .\Native3DGSViewer.sln /restore /m /p:Configuration=Release /p:Platform=x64
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe' .\out\Release\Native3DGSViewer.Tests.dll /Platform:x64
```

VS 原生测试 DLL 启动同目录的 GoogleTest 可执行文件。直接运行 `out/Release/Native3DGSViewer.Tests.exe` 可查看逐项结果。固定依赖提交见 `third_party/README.md`。

本机验证结果：Release x64 构建成功；GoogleTest 19/19 通过、无跳过；VSTest 桥接测试 1/1 通过。`/restore` 因工程没有 NuGet 包而给出“无可还原项目”警告，不影响构建。

测试包括 PLY 属性重排、SH 0-3、RDF/RUB、CRLF、中文路径、截断与非法数值；SPZ v1-v4、量化 alpha 边界和不支持特性；内容探测、进度、取消、资源上限、错误恢复、100 次重复加载与短时随机头部/共享布局变异。独立故障 helper 覆盖崩溃、错误 IPC、共享布局损坏、日志洪泛、超时、活动取消、Job 配置失败和 Job 内存限额。工作区真实 PLY 和大小 SPZ 亦参与可选语料测试；它们不随仓库分发，缺席时语料用例会跳过。

ASan 使用 VS 2026 x64 Debug、`/fsanitize=address /EHsc` 构建，命令如下。全套 19/19 通过；另连续运行 `ModelIoCorpus.*` 五轮，每轮 2/2 通过，未报告 AddressSanitizer 错误。ASan 不代替泄漏检测或长时 fuzz。

```powershell
cmake -S . -B out/asan -G 'Visual Studio 18 2026' -A x64 -DCMAKE_C_FLAGS='/fsanitize=address /Zi' -DCMAKE_CXX_FLAGS='/fsanitize=address /Zi /EHsc' -DCMAKE_C_FLAGS_DEBUG='/Od /Zi' -DCMAKE_CXX_FLAGS_DEBUG='/Od /Zi' -DCMAKE_EXE_LINKER_FLAGS='/INCREMENTAL:NO' -DCMAKE_SHARED_LINKER_FLAGS='/INCREMENTAL:NO'
cmake --build out/asan --config Debug --parallel 8
$env:Path = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.50.35717\bin\Hostx64\x64;' + $env:Path
$env:ASAN_OPTIONS = 'halt_on_error=1'
.\out\Debug\Native3DGSViewer.Tests.exe --gtest_brief=1
for ($i = 1; $i -le 5; $i++) { .\out\Debug\Native3DGSViewer.Tests.exe --gtest_brief=1 --gtest_filter=ModelIoCorpus.*; if ($LASTEXITCODE -ne 0) { break } }
```

运行 `.\bench\measure-model-io.ps1` 对三份真实样本加载轮询采样（20 ms）：测试宿主峰值工作集 1003.4 MiB、最高采样私有内存 4 MiB、最高采样句柄 91；helper 峰值工作集 1864.2 MiB、最高采样私有内存 1128.7 MiB、最高采样句柄 55。工作集来自进程生命周期峰值属性；私有内存和句柄是轮询采样高点，短暂峰值可能漏测。宿主共享映射不计入私有内存。100 次重复加载测试另检查宿主句柄数回到初始值加 4 以内。

| 工作区样本 | SHA-256 | 用途 |
|---|---|---|
| `../1.ply` | `A0917F8B7D15D9D07B802BA3E937365E8557EDF292519706F70064CDE3EDB73A` | 1,179,648 点标准 PLY |
| `../Viewer_android/public/scene/jidaoshan.spz` | `1E16BB8F5C9CE1D41A5BAAED84058720A8327B2FE029AAF4EBD78519F765C367` | 509,812 点 SPZ，量化 alpha 边界 |
| `../Viewer_android/public/scene/zhihuizhimen.spz` | `FFFB4553592824703B02A551BAF5BDE60F19452C65B754F920F17CE5DB27827C` | 3,914,609 点大 SPZ、活动取消 |

固定姿态的图像与 Spark 对照须在 render-core 可运行后完成；本阶段只验证 model-io 数值与结构契约。外部语料的来源授权仍需项目所有者确认，仓库未分发这些文件。
