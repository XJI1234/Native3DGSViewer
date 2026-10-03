# Spec: service-gateway（API v3）

## 目标与结构

Go>=1.23 net/http，cgo系统SQLite，C++/CUDA只处理GPU热路径。生产入口gateway/gs-gateway，旧tools/gateway.py仅供历史/回退。空间与生命周期以[SPEC-instances-cache-v3.md](SPEC-instances-cache-v3.md)为准。

models目录启动时快照：PLY/SPZ相对路径的SHA256产生稳定id，内容SHA256为revision；符号链接不允许逃逸，最多10000模型，路径禁止制表/换行。内容被替换后新加载拒绝旧快照，需重启目录；缓存命中属于启动快照，不承诺文件热更新。

## API契约

| 请求 | 认证与行为 |
| --- | --- |
| GET /health | 公开；protocol2/api_version3/status ready；available为配置GPU数，workers为已装载实例数 |
| GET /models | Bearer；id/name/bytes/revision，不泄露绝对路径 |
| GET /status | Bearer；device、multi_gpu、实例model/gpu/pid/activity、loads/unloads/renders、缓存数量/大小/计数 |
| POST /sessions | Bearer；纯文本model-id；返回48hex session_id、expires_in_ms，最多256租约 |
| POST /sessions/heartbeat | Bearer；纯文本session-id；120秒续租；过期410 |
| POST /sessions/close | Bearer；纯文本session-id；释放租约 |
| POST /frame | Bearer；NGSREQ3或旧NGSREQ2；可选X-GS-Session，后台必须有效租约及X-GS-Prefetch:1 |

成功/frame为完整NGSFRM02，X-GS-Request-Id/X-GS-Model-Id逐请求改写，X-GS-View-Code与规范球坐标一致，X-GS-Cache为hit/miss/shared。X-GS-Stats在native/shared响应含原生阶段数据；纯缓存命中为{}，不能拿原始渲染耗时代表当前请求。服务日志另外记完整前台处理ms。cache_hits/misses为内部lookup计数，非严格用户请求数。

错误JSON：400输入/请求格式，401认证，404模型/路由，405方法，410租约，500元数据/缓存读取错误，503并发预算/实例容量/原生渲染或缓存写错误。禁止chunked、重复header；body1024、header8192、128连接、64handler、最大16配置实例（默认4）。单用户没有独立登录，256为共享bearer的全局租约预算。

## 调度、持久化与安全

同模型一实例、单GPU固定；相同帧producer合并。卡级前台优先，预测繁忙可失败。前台等待预测producer失败时，以前台策略重试一次。foreground断开不取消共享producer；总超时180秒。独立64个producer预算保持到producer结束，已命中缓存和合并等待不占新producer；超限503。实例idle300秒，lease120秒、维护10秒。cache TTL86400秒，10GiB/100000项；仅前台命中续期，预测不续。SQLite WAL/NORMAL、schema版本1、文件原子rename和CRC/SHA完整性校验。进程状态不从DB恢复。初始化状态目录flock、端口PID确认、SIGTERM/SIGINT退出、清理自有children。启动时固定worker实际路径和digest；shutdown关闭新增producer入口。保留多GPU模式按实例占用最少的卡放置，默认仍只启用GPU0。

非回环需TLS或显式可信私网HTTP；token0600、恒定时间比较、不日志。共享bearer管理员认证，不是多租户身份/RBAC。启动设备用native devices验证；多个device需要--enable-multi-gpu，默认0。

## 命令与验收

~~~bash
cd ForServer/gateway
CGO_ENABLED=1 GOTOOLCHAIN=local go test -race -count=1 ./...
go vet ./...
go build -trimpath -o gs-gateway .
./gs-gateway --server /path/gs-server --models /data/models --state /data/state --token-file /data/token --devices 0 --bind 0.0.0.0 --port 8888 --private-http
~~~

测试涵盖20个并发用户一次render、租约/prefetch、idle/cache-hit/reload、TTL/corrupt/LRU/persistence、优先级/取消后的活动数、容量拒绝、shutdown/schema拒绝。真实GPU及WinUI证据见verification-instances-v3.md。默认health表示网关可接收请求与启动时设备验证成功，不等于实时GPU压力测试；还需/status和渲染验证。
