# 2026-10-17 Windows D3D12 起点实测

状态：E00 **阶段诊断已完成**；CUDA 路径尚未实现/测试，正式可见呈现对照尚未运行。此报告不宣称 CUDA 提速。

## 环境与来源

RTX 3080 10GB，WDDM，驱动617.42；Windows x64，CMake 4.3.2、VS 2026生成器。精确工具输出、二进制与shader SHA-256见 [environment.json](environment.json)。起点提交 `75a47714e4002f10666fe2b9265dfdc5e3e8439c`，分支 `codex/windows-cuda-operators`。本轮在此提交的源码上重建现有 SceneBench、SortBench 和测试目标，未修改渲染算法。

模型根目录：`C:\Users\21544\Desktop\zhishan`。[models.json](models.json) 递归枚举40个PLY/SPZ；主测试的5个PLY和2个SPZ已计算SHA-256，其余尚未逐个加载验证。

可用nvcc为11.8.89；v13.3目录缺少nvcc和开发头，驱动显示的CUDA UMD 13.4不代表安装了完整13.x Toolkit。CUDA11.8的host_config拒绝`_MSC_VER >= 1940`；CUDA/VS2026组合尚未构建验证。

## 协议与命令

使用现有 `bench/scene.cpp`：1920×1080物理像素、SH3、stride1、默认径向排序/stddev3/alpha0/blur0/radius1024。核对每份日志的质量与渲染点数，所有PLY保持完整源点数。每次独立进程先预热60帧、记录120帧；5个PLY × force/cached/orbit × 3轮，共45次运行、5400个记录帧，全部退出码0。第2轮反转模型/模式顺序，所有GPU负载串行。

- force：fit相机不动，但每帧重新投影/排序。
- cached：fit相机不动，复用投影/排序。
- orbit：每帧旋转1度并重新投影/排序。这是按帧路径，不是正式验收的按时间路径。

示例（从仓库根）：

```powershell
cmake --build out/cmake --config Release --target Native3DGSViewer.SceneBench Native3DGSViewer.SortBench Native3DGSViewer.Tests --parallel
& .\out\Release\Native3DGSViewer.SceneBench.exe 'C:\Users\21544\Desktop\zhishan\zhihuizhimen.ply' force 120 3 1
& .\out\Release\Native3DGSViewer.SceneBench.exe 'C:\Users\21544\Desktop\zhishan\jiulonghu_v1.ply' orbit 120 3 1
& .\out\Release\Native3DGSViewer.SortBench.exe 8000000 30
ctest --test-dir out/cmake -C Release --output-on-failure
```

每次命令、退出码、运行墙钟与每轮中位数见 [stage-runs.json](stage-runs.json)，完整标准输出/错误见 `raw/`。汇总由日志中的逐帧记录计算，不删除异常帧；阶段值和整帧分别取中位数，其和不必等于整帧中位数。

## 实测结果

下表为三轮各自中位数的中位值，单位ms；不是显示帧时间或端到端交互延迟。

| PLY | 完整点数 | force GPU总帧 | cached GPU总帧 | orbit GPU总帧 | force三轮中位区间 |
|---|---:|---:|---:|---:|---:|
| changjin_v1 | 170799 | 0.4045 | 0.1782 | 0.4009 | 0.4045–0.4403 |
| shengyi_v1 | 804758 | 1.2769 | 0.5571 | 1.2723 | 1.2718–1.3599 |
| tumu_v1 | 1248730 | 1.9118 | 0.8591 | 1.9128 | 1.9098–1.9139 |
| zhihuizhimen | 3914609 | 5.9628 | 2.6747 | 5.9658 | 5.9617–6.0396 |
| jiulonghu_v1 | 22480361 | 34.7177 | 15.8003 | 34.7945 | 34.6947–34.8365 |

| force阶段 | 3914609点 | 22480361点 |
|---|---:|---:|
| 投影/SH | 1.7685 | 10.5006 |
| 排序 | 1.2687 | 8.1045 |
| 绘制 | 2.6767 | 15.9145 |

完整15组摘要见 [summary.csv](summary.csv)。所有45次运行均核对了120条帧记录、源点数以及SH3/stride1/1920×1080日志。

另对 `spz/shengyi_v1.spz`、`spz/tumu_v1.spz` 各做一次force/120帧烟雾，退出码均为0；这两次不是三轮性能对照，也没有证明PLY/SPZ归一化数组逐值相等。原始日志以 `-spz-force-1.log` 命名。

800万键现有SortBench每轮30次、三轮中位数再取中位为 **2.6122ms**；见 [sort-summary.json](sort-summary.json)。注意其初始键为随机，但后续迭代继续排序已排序的buffer；不能将该数字描述为30次独立随机数组排序。E02正式对照须明确并统一输入分布及重置成本。

## 可据此判断的内容与限制

大场景的投影、排序、绘制均有显著成本，排序只占部分帧时间；因此E02需统计互操作后的整帧收益，后续投影/tile实验也值得独立验证。小模型force结果存在约6%–9%的轮间变化，不能用一次小幅胜出来保留复杂实现。

当前基准离屏、逐帧CPU等待GPU，没有Present，也没有CUDA。其固定帧预热与按帧orbit不满足规格的30秒预热/60秒采样/按时间相机路径。未测帧管线并行、GPU时钟全程轨迹、进程峰值RAM/VRAM、图像误差、两小时稳定性或故障注入。

现有CTest的ModelIo、RenderCore、ModelRenderIntegration、Engine、InstalledSDK **5/5通过**，总计88.18秒；见 [ctest.log](ctest.log)。这是当前D3D12起点的回归证据，不代表新增CUDA的稳定性验收。

## 下一项

规格审阅后进入E01：确认受支持的CUDA宿主组合，验证LUID、共享资源、fence和空交接代价。当前无任何CUDA加速数字。
