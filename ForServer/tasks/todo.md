# instances-cache-v3 任务状态

- [x] 规格更新：五个provider模块及空间/生命周期补充规格，保持已授权实施边界。
- [x] request3/2°完整球面/81距离档/Y反转/预测位置；11个共享契约测试与6个CUDA测试。
- [x] native固定模型实例、单卡绑定、多个用户共享；多卡保留而默认关闭。
- [x] Go HTTP与SQLite：leases/idle、同帧producer合并、GPU门控/优先级、TTL/LRU/CRC/SHA、启动清理与shutdown。
- [x] Go八个实质测试及worker helper通过race/vet；覆盖20并发、容量、取消、shutdown、schema。
- [x] Windows transport/压缩LRU/保护近邻、会话心跳、完整球面/Y翻转、四方向3–5档预测及移动复用。
- [x] Windows构建/CTest2组通过；真实WinUI10帧、19次累计预取、相邻local命中、极点与错误路径。
- [x] Go隔离安装、Linux部署脚本/README、GPU0配置，SSH实际Release安装/服务8046。
- [x] 真实GPU八用户一次渲染、实例保留/卸载、缓存无GPU响应、预测懒加载、加速TTL删除与再加载六组通过。
- [x] 1080p20相机miss/hit、同相机Windows前后数据、loopback与mapped分链路记录；CUDA memcheck0错误。
- [x] 架构/provider/verification及脱敏证据同步。

## 后续验收边界

- [ ] 公网TLS/证书轮换、独立用户/RBAC、长期并发及故障注入安全审计。
- [ ] 原生systemd、容器外部entrypoint/重启集成、完整回退故障注入、日志轮转策略。
- [ ] 全38模型/约2200万点、Ubuntu24.04、弱Windows设备与真实WAN。
- [ ] 多DPI/可访问性/实体输入/resize全人工矩阵及实际服务重启的WinUI会话恢复E2E。
- [ ] display-to-photon、WinUI compositor GPU占用、持续交互prefetch命中率与带宽。

## Cloud 0.2.2 修订

- [x] 修正屏幕拖动方向，倒置和穿极点保持交互一致。
- [x] 预渲染低/中/高5/10/15档，默认低及JPEG85，完全退出清本地压缩缓存。
- [x] 120秒租约、短控制超时、有限重试、失效恢复及最终退出释放。
- [x] OCR全模块审查及修复复审；有界producer、部署事务回退、异步WinHTTP取消和故障回归。
- [x] 独立Cloud安装包及安装后静置、恢复、失败退出和卸载验收，见verification-cloud-022.md。

上述是未验收扩展门槛，当前默认单GPU闭环已完成。前节3–5档及30秒租约等记录属于历史v3验收；当前修订以Cloud0.2.2规格为准。历史v2任务/数据见docs/verification-image-v2.md。单帧跨卡分片与实时视频不在范围。
