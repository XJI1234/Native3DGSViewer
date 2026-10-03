# API v3：模型实例与离散视角图像缓存

主规格：[SPEC-instances-cache-v3.md](SPEC-instances-cache-v3.md)。图片包继续使用NGSFRM02 v2，请求新增NGSREQ3。当前验收：[verification-instances-v3.md](verification-instances-v3.md)；历史四卡方案见verification-image-v2.md，ProjectedSplats原型见architecture-v1.md。

~~~mermaid
flowchart LR
    UI[WinUI3 离散相机] --> Local[64MiB 压缩包LRU]
    Local --> HTTP[Go网关 :8888]
    HTTP --> DB[(SQLite WAL 租约/索引)]
    HTTP --> Disk[24h 磁盘帧缓存]
    HTTP --> Flights[同视图请求合并]
    Flights --> GPU[GPU0 渲染门控]
    GPU --> Native[C++ 每模型实例]
    Native --> Codec[RGBA Zstd / JPEG]
    Codec --> Disk
    Disk --> UI
~~~

## 原生渲染与实例

CUDA Runtime/CUB实现headless计算渲染，不依赖窗口或图形桌面。保留full SH0–3、协方差/投影、稳定径向排序、tile/rank排序和CUDA透明合成。随后RGBA8 readback、Zstd level1（不可压缩时raw fallback）或libjpeg-turbo 4:4:4。客户端不加载模型，不计算SH/投影/排序。

一个模型内容版本一个进程，启动带--instance-model及--device，生命周期内不迁移、不接其他模型。多个模型可在同一张卡有独立实例，默认最多4个；卡级门控串行渲染，前台等待数阻止新预测抢占，已经开始的CUDA帧不强制中断。容量不足返回503；不默默驱逐活跃模型。GPU故障/进程失败清理后可以重新懒加载；健康端口验证worker PID，避免误认其他进程。

只有GPU0默认启用。设备列表和显式--enable-multi-gpu保留扩展能力；当前不使用四卡副本或单帧跨卡合成。GPU0分配记录与nvidia-smi系统总占用须分别报告。

查看会话是120秒租约、客户端10秒心跳。关闭立即删除租约；失联自动过期。保留租约或实际前台活动保护实例，默认空闲宽限300秒，扫描每10秒。宽限期重访复用同进程；终止实例释放CPU mapping/GPU内存。图片命中可以在零实例状态响应；有效租约允许随后的缺失邻域预测懒加载实例。

## 相机与文件编码

沿用原生引擎包围盒中心归一化原点，Y向上、yaw0在+Z、正yaw向+X，FOV60°、保守fit包含3σ支撑。2°格点：a00..179为方位角，t00..90为极角；81个距离档d00..80，fit倍率10^((d-40)/40)。位置名aNNN-tNN-dNN。极点保留方位，用于确定相机方向；穿越极点折叠位置并翻转相机roll，分析基向量避免look-at叉积奇异。

Y反转使相机up/right旋转180°，不翻模型、不重算球心或改变基准面。图片变体额外编码f0/f1、分辨率和质量。完整路径：frames/model-id/model-SHA256/renderer-SHA256/position-f0-WxH-profile.ngsf；网格版本也是索引key的一部分。相同位置与变体合并，不因用户/request-id复制缓存；响应身份头逐请求生成。

## 缓存与数据库

SQLite是单节点元数据存储，cgo链接系统libsqlite3，无外部Go模块。WAL、busy timeout、绑定参数、schema版本检查，存缓存digest/bytes/last_hit及租约；运行GPU状态不持久恢复，启动清旧租约。它不是账户或计费数据库。

文件write/close/atomic rename成功后建索引；每次读取检查header/CRC/尺寸/profile/SHA256，缺失/损坏删除重算。启动清临时文件/无索引图片。默认24小时无前台服务端命中删除，预测/心跳不延长期限；客户端本地命中没有服务器图片请求，所以不续磁盘图片TTL。总图片10GiB/100000项LRU。缓存维护与实例卸载是独立生命周期。

缓存可重建：SQLite同步NORMAL、图片不逐帧fsync，消除高延迟持久化开销。正常服务重启保留缓存；宿主突然掉电可能丢最近缓存，校验后重算。凭据/配置及权威业务数据不沿用此策略。缓存写失败显式返回错误；日志/数据库/release不包含在图片预算内。

## 并发与端侧预测

Go net/http限制128连接、64并发handler、1KiB body、8KiB请求头、64MiB+header帧。单帧producer180秒，HTTP写200秒；shutdown停止新增producer并等待清理。相同模型/变体缺失single-flight；用户断开不会取消其他用户共享的producer。预测需要有效模型租约，繁忙时可跳过；未开始的旧窗口停止请求。

Windows前台single-flight/latest-wins，与一个独立后台预取协程并行。当前画面显示后按四方向各5档预测，可选10/15档。重叠窗口只下载缺失压缩包；64MiB/256项LRU保护当前帧及最近四邻域，显示时才解码。模型/画质/尺寸/朝向都是本地key一部分。客户端保持租约，即使一直本地命中；服务重启导致410时重建租约、清本地帧并刷新当前画面。

## 部署和可运维边界

supervisor执行Go网关，所有C++实例只监听回环。部署staging通过native CTest/Go race/vet后切换current，配置/token/DB/缓存独立。前台run适合作为容器entrypoint；后台start用于当前已授权测试机。容器PID1非systemd，未修改外部编排配置。共享bearer是管理凭据，租约不是租户权限；当前8046为可信私网HTTP。公网TLS/账户授权、长压测、日志轮转与容器重启集成见验收剩余项。
