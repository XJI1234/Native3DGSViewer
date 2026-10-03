# 能力划分：模型实例、球坐标缓存与云端查看

用户已授权继续实现、SSH部署及测试。稳定模块ID沿用v2；本轮以[实例与缓存规格](docs/SPEC-instances-cache-v3.md)扩展边界。

| 模块 id | 职责 | 依赖 |
| --- | --- | --- |
| image-frame | NGSFRM02、RGBA + Zstd、JPEG85–95、尺寸/完整性/资源校验 | — |
| render-worker | C++20/CUDA/CUB headless渲染；每模型实例固定一张卡；球面相机 | image-frame |
| service-gateway | Go并发、租约、实例管理、同帧合并、SQLite缓存索引与期限、单一对外入口 | image-frame, render-worker |
| linux-deployment | 工具链/依赖诊断、分阶段构建测试、安装/监管/回退、运行配置 | service-gateway |
| windows-client | WinUI3连接/模型/画质/完整球面/Y反转、离散输入、本地压缩缓存和预渲染 | image-frame, service-gateway |

实施顺序：空间契约 → 固定模型native实例 → Go/SQLite → Windows租约与预测 → 部署验收。各provider契约见docs/SPEC-{module-id}.md。部署和UI不反向耦合GPU算法。

## 本轮约束

所有新增实现位于ForServer，不改原GUI/SDK。ProjectedSplats已退役。服务器持有PLY/SPZ；端侧只接收图片。一模型版本一实例，多用户共享，同帧并发缺失只渲染一次。默认只启用GPU0；多卡开关保留但关闭，不继续采用旧版同模型跨卡复制驻留策略。

空间为方位角180档、极角91档（均2°）、81个对数距离档。完整球面、穿越极点，Y反转为独立相机朝向变体，世界基准不变。预测为四方向各5/10/15档，默认低（5档）；缺失才生成/下载。缓存按实际前台服务端命中续期24小时，并受磁盘预算约束。

Ubuntu22.04.5/驱动580/CUDA13服务器通过SSH8045维护，应用8046→容器8888。当前已授权可信私网HTTP；生产公网TLS、租户身份/RBAC和外部容器编排须单独验收。本轮实际证据见[verification-instances-v3.md](docs/verification-instances-v3.md)；[v2四卡报告](docs/verification-image-v2.md)仅作为历史性能参考。
