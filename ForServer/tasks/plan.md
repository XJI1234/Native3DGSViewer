# 实施计划：instances-cache-v3

用户授权继续实现/SSH部署，沿用CAPABILITY-MAP稳定模块。主规格docs/SPEC-instances-cache-v3.md。

1. image-frame/render-worker：request3球坐标规范化、2°/81档、极点/朝向、预取位置列表；先契约测试再native固定模型实例。
2. service-gateway：Go线程安全lease/实例/卡级门控/producer合并；SQLite版本索引和磁盘完整性/TTL/LRU；race/真实socket、优先级/取消/容量/shutdown。
3. windows-client：transport会话头/响应位置、压缩LRU、完整球面输入/Y翻转、心跳、移动预取窗口；build/CTest及真实应用smoke。
4. linux-deployment：隔离Go、SQLite依赖、默认GPU0、TTL参数、native/Go质量门禁、supervisor；真实SSH生产安装与8046闭环。
5. 验收：八用户同帧、实例保留/卸载/cache-hit无GPU/预测懒加载、加速TTL实测；1080p同相机cache latency；WinUI预测local hit/质量/翻转/极点和错误。
6. 同步README/provider规格/架构/证据与未验收门槛。保留v2报告的历史1/2/4卡和JPEG质量数据，不把它们当当前多卡启用或新延迟。

风险与处理：模型/renderer版本进入cache key；预测不续前台TTL；极点roll独立于位置；shared producer不由单用户断开取消；配置/令牌不进入证据；SQLite缓存NORMAL可重建。维护源码范围ForServer，原GUI/SDK不变，不提交/建分支。
