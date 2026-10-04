# Web SDK OCR审查与闭环

2026-10-04，发布版本0.2.2-preview.1。按用户指定的open-code-review技能使用OCR CLI v1.12.11完成六轮审查，涵盖分支和随后工作区修订；每轮均传入WebGPU全点/全SH、WASM/OPFS、事务取消恢复和SDK发布的业务背景。没有把AI评论自动当作事实，也没有以修复前测试证明修复后行为。

## 审查范围与结果

| 记录ID | 已审查文件数 | critical | high | medium | low |
| --- | ---: | ---: | ---: | ---: | ---: |
| ocr-workspace | 17 | 0 | 0 | 6 | 1 |
| ocr-branch | 54 | 0 | 5 | 29 | 0 |
| ocr-final-workspace | 38 | 0 | 1 | 8 | 2 |
| ocr-final-recheck | 41 | 0 | 2 | 11 | 0 |
| ocr-release | 42 | 0 | 3 | 6 | 1 |
| ocr-closure | 43 | 0 | 0 | 3 | 4 |

82条观察包含跨轮重复与低优先级建议，不等于82个独立缺陷。脱敏后的文件覆盖、定位、评论、工具失败与逐项处置保存在[ocr-review-dispositions.json](evidence/ocr-review-dispositions.json)。没有保存provider认证配置或凭据。

最后一轮原始结果没有critical/high；其3项medium均已修复：dist完整inventory不再遗漏MANIFEST.json，性能图标题读取真实分辨率/SH，以及device.lost从每次呈现追加Promise continuation改为renderer单一监听和可移除订阅。后者覆盖100次重复等待、取消与设备丢失，并在最终100次生命周期和5分钟稳定性中验证。4项low为未使用导入，已清理。这里报告的是审查后修复闭环，没有声称最后一次OCR原始输出为零medium，也没有无限追加全量AI复审。

分支轮有一次git-show无法把vendor gitlink读取为普通文件的工具失败；54个选定文件完成审查，其他五轮toolFailures为空。未将vendor失败视为依赖源码已审查。依赖采用固定gitlink，构建/打包检查clean状态与完整许可。首次C++ include评论推断“无法构建”并不准确：已有include搜索路径掩盖了相对路径问题，已修正路径并实际重建WASM。

## 主要修复与实测依据

| 风险 | 最终机制 | 验证 |
| --- | --- | --- |
| 迟到创建、恢复或上传覆盖较新请求 | ownership/epoch、总deadline、可取消GPU/backing等待 | CPU事务回归、真实timeout→retry→capture |
| 候选失败影响活动模型或Canvas | 离屏验证后实际Canvas呈现验证，成功才替换，失败/取消回滚 | 暂停/替换/validation回归、真实GPU联合 |
| 长期设备Promise保留呈现闭包 | 单一lost监听、可移除短期订阅、finally取消 | 重复等待及取消单测，最终生命周期/soak |
| 存储清理挂起或被忽略 | 5秒上限，Stopped快照保留StorageCleanup错误 | 有界清理回归、38模型正常清理 |
| GPU暂存/读回或超大输入绕过预算 | 验证纹理共享预算，capture buffer limit，实际下载字节准入 | CPU资源回归、浏览器gzip/HTTP/宽PLY |
| 最大PLY批次局部bounds舍入误拒绝 | world中心和原bounds通过完整共享validator | WASM数学回归、最大PLY及38/38 |
| 适配器重绑定或初始化失败泄漏 | React callback host、Vue post-flush host watch、owned cleanup | 独立tgz、StrictMode、条件Vue、卸载资源归零 |
| 性能结果缺失却成功退出 | watchdog、有限值/样本数/本帧归属、失败与清理记录 | 完整6组Spark对照、布局消融、最大模型 |
| 包含陈旧或未经记录的产物 | source/output SHA及完整inventory、fresh staging、clean依赖 | 发布打包验证、tgz真实消费 |

发布源码通过36个CPU测试、18个契约测试（含WASM与基准守护）、GPU/图像、联合、streaming、独立消费和38个真实模型；100次生命周期及5分钟soak完成，dispose后所有owned GPU/device/context/Worker资源归零。详细性能、环境、模型哈希和剩余验收限制见[大型模型报告](large-model-optimization-report.md)。AI审查和单机实测不能替代跨平台、真实硬件reset或物理呈现验收。

## 复现方式

从仓库根目录执行，使用本机已配置provider，勿将认证配置放入仓库：

```powershell
ocr review --audience agent --background 'Review complete float32/SH WebGPU scenes, bounded WASM/OPFS decoding, cancellation/recovery ownership, React/Vue integration and release provenance' --from origin/main --to codex/webgpu-engine --format json --output ForWeb/.local/ocr-branch-reproduction.json
```

工作区复审省略from/to，保持background和output。原始本地审查输出不发布；可提交证据只包含脱敏后的选定文件与逐项处置。
