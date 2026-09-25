# model-io 验证记录

2026-09-26，Windows 11 x64，VS 2026 / MSVC 19.50，CMake 4.3.2，clang-format 20.1.8。

## 构建与测试

```powershell
git submodule update --init --recursive
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' .\Native3DGSViewer.sln /restore /m /p:Configuration=Release /p:Platform=x64
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe' .\out\Release\Native3DGSViewer.Tests.dll /Platform:x64
```

VS 原生测试 DLL 启动同目录的 GoogleTest 可执行文件。直接运行 `out/Release/Native3DGSViewer.Tests.exe` 可查看逐项结果。固定依赖提交见 `third_party/README.md`。

本机验证结果：Release x64 构建成功；GoogleTest 16/16 通过、无跳过；VSTest 桥接测试 1/1 通过。`/restore` 因工程没有 NuGet 包而给出“无可还原项目”警告，不影响构建。

测试包括 PLY 属性重排、SH 0-3、RDF/RUB、CRLF、中文路径、截断与非法数值；SPZ v1-v4、量化 alpha 边界和不支持特性；内容探测、进度、取消、资源上限、错误恢复、100 次重复加载与短时随机头部/共享布局变异。工作区真实 PLY 和大小 SPZ 亦参与可选语料测试；它们不随仓库分发，缺席时语料用例会跳过。

| 工作区样本 | SHA-256 | 用途 |
|---|---|---|
| `../1.ply` | `A0917F8B7D15D9D07B802BA3E937365E8557EDF292519706F70064CDE3EDB73A` | 1,179,648 点标准 PLY |
| `../Viewer_android/public/scene/jidaoshan.spz` | `1E16BB8F5C9CE1D41A5BAAED84058720A8327B2FE029AAF4EBD78519F765C367` | 小 SPZ，量化 alpha 边界 |
| `../Viewer_android/public/scene/zhihuizhimen.spz` | `FFFB4553592824703B02A551BAF5BDE60F19452C65B754F920F17CE5DB27827C` | 3,914,609 点大 SPZ、活动取消 |

正式发布前仍须运行规格要求的持续 AddressSanitizer 语料、独立故障注入（Job 赋予失败、助手崩溃、IPC 损坏）和内存/句柄峰值采样；本记录不代表这些发布门槛已完成。
