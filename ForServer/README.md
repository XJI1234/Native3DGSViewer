# 原生 GPU 云渲染：模型实例、离散视角缓存与 WinUI 3

当前入口为 Go 网关 + SQLite + C++/CUDA worker，API v3，图像包仍为 NGSFRM02。原生编码默认 RGBA + Zstd，客户端默认 JPEG85，支持 JPEG85–95（UI 提供95/90/85）。ProjectedSplats 已退役；新代码均在 ForServer，原 GUI/SDK 不变。

- [架构](docs/architecture.md)、[模块索引](CAPABILITY-MAP.md)、[实例/缓存规格](docs/SPEC-instances-cache-v3.md)
- [本轮验收与复现](docs/verification-instances-v3.md)、[任务状态](tasks/todo.md)
- [Cloud 0.2.2 修订规格](docs/SPEC-client-refinements-022.md)、[安装与静置恢复验收](docs/verification-cloud-022.md)
- [v2 四卡证据](docs/verification-image-v2.md)属于上一轮吞吐方案；本轮多卡保留但默认关闭。

## 1. 当前服务和客户端

SSH：root@10.129.4.104:8045，实际私钥 C:\Users\21544\.ssh\id_rsa。应用端口是 **8046**，映射容器8888；8045不能用于客户端。当前生产运行目录为 /opt/native3dgs，只启用GPU0，采用已经授权的可信私网HTTP模式。访问令牌文件 /opt/native3dgs/token 为0600；不要输出到日志、放入命令行参数、复制到源码或提交Git。

~~~powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ForServer/WindowsClient/build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File ForServer/WindowsClient/packaging/build-installer.ps1 -SkipBuild
.\out\cloud-client\Release\Native3DGSCloud.exe
~~~

携带整个自包含输出目录，不仅exe。连接页输入10.129.4.104、8046和令牌；当前调试环境需取消HTTPS并确认可信私网HTTP。健康检查protocol2/api_version3、认证后的模型目录通过后才能进入查看页。

选择模型、打开；方向键/按钮每次2°，拖动吸附到2°网格，滚轮每次一个距离档。支持完整绕球观察和穿越极点；“反转Y”将相机上方向翻转180°，不修改模型坐标、原点、球坐标基准或位置编码。适配/重置恢复居中视图。画质支持RGBA/JPEG95/90/85。

“预渲染”可关闭；每方向低/中/高（5/10/15档），默认低（5档）。当前画面显示后后台下载20张相邻图片，移动后更新范围并重用重叠项。客户端仅缓存压缩包，64MiB/256项上限，优先保护当前帧和四个最近相邻帧；显示时才解码。状态栏显示球坐标编码、local/hit/miss缓存状态及预取数量。网络、解码和预取不占UI线程；最多一个前台和一个后台下载，过期帧不会覆盖当前画面。

客户端每10秒更新120秒租约；控制请求采用短超时和最多三次重试，连续三轮且30秒未成功才提示持续连接故障。恢复后清除提示。关闭/切换模型/断开释放租约。网关重启使旧租约失效，客户端按HTTP410重新创建后清空本地图片缓存、重新请求当前画面，避免模型或渲染器更新后显示旧缓存。完全退出时清理内存压缩包；普通查看不创建持久下载缓存。

安装包为Native3DGSCloudViewer-0.2.2-Windows-x64-Setup.exe，位于out/cloud-installer。Windows11 x64、每用户独立安装，包含自包含WinUI与VC runtime、许可，与本地查看器不同AppId及快捷方式。当前未签名。补充发布在既有v0.2.2 Release，附加源码commit由Release说明注明，不移动旧tag。

## 2. 实例和缓存规则

一个模型版本对应一个native进程/实例，生命周期内固定一张GPU；多个用户共享该实例，同一视图的并发缺失共用一次渲染。默认仅device0，不复制同模型到多卡。其他模型可以在GPU0装载独立实例，默认最多4个；GPU渲染串行门控，前台优先、后台繁忙时让出。达到实例容量时返回503，不默默卸载正在使用的模型。

默认实例空闲宽限300秒，未过期查看租约会保留实例。退出客户端未成功发送关闭也有120秒租约期限，之后空闲清理。已有实例在宽限期内再次访问无需重复装载；进程退出释放模型映射和GPU分配。磁盘缓存命中不需要装载实例；活跃会话随后可以为缺失的预测邻域启动后台实例。

球坐标命名为 aNNN-tNN-dNN：方位角180档、极角91档，均2°；距离81档，0.1..10倍适配半径、对数间隔、d40为适配距离。球心沿用原引擎固定居中原点（包围盒中心的归一化坐标），不因反转Y而变动。穿越极点时用规范化方位/极角加独立画面朝向表达；极点保留方位以确定相机方向。

磁盘路径位于 state/frames/model-id/model-SHA256/renderer-SHA256/aNNN-tNN-dNN-f0-WxH-rgba.ngsf。f0/f1、分辨率和编码是画面变体，不能混用。模型内容、渲染器二进制或网格版本变化不会命中旧画面。只由实际前台服务端访问续期；预取和心跳不续期图片。默认24小时未命中删除，10GiB和100000项上限，超限按LRU清理，扫描间隔10秒。

SQLite metadata.sqlite 使用WAL、预编译绑定参数和受限单进程写入，存储缓存校验/大小/最后命中、租约及schema版本；GPU状态为运行时状态，不从旧数据库冒认实例。文件写完关闭后原子rename再建索引；CRC/SHA256、尺寸和编码验证失败则删除并重新渲染。NORMAL WAL避免每个可重建缓存命中都fsync；正常进程重启保留缓存，突然掉电可能丢近期缓存，自动失效重建。该策略不用于凭据或业务账务记录。

## 3. Linux 一键部署

要求Ubuntu22.04/24.04、NVIDIA兼容驱动、CUDA12.6+、CMake3.22+、GCC/G++12、Ninja、Boost、libjpeg-turbo、zlib、Go>=1.23、libsqlite3-dev、Python3.10+（仅部署/监管/测试）。Go服务无第三方module依赖，cgo链接系统SQLite。此次实测Ubuntu22.04.5、CUDA13.0.88、Go1.27.1。

~~~bash
git submodule update --init --recursive
bash ForServer/deploy/install-go.sh /opt/native3dgs/go-toolchain
bash ForServer/deploy/deploy.sh doctor --cuda /usr/local/cuda-13.0 --go /opt/native3dgs/go-toolchain/go/bin/go
bash ForServer/deploy/deploy.sh install \
  --prefix /opt/native3dgs --models /data/models --devices 0 --port 8888 \
  --cuda /usr/local/cuda-13.0 --go /opt/native3dgs/go-toolchain/go/bin/go \
  --install-deps --instance-idle 300 --cache-ttl 86400 \
  --cache-bytes 10737418240 --max-instances 4 \
  --cert /data/tls/server.crt --key /data/tls/server.key
~~~

生产非可信网络必须使用有效TLS；不要关闭客户端证书校验。监管健康检查访问127.0.0.1，证书需覆盖它并配置相应信任。可信私网调试明确用 --private-http 替代cert/key，图像和令牌确实不加密。

install进行Release构建、native CTest、Go race/vet/build、监管脚本安装和健康检查；staging通过后原子切换current，启动失败恢复previous并保留日志。配置/token/数据库/图片与release分离。已有配置默认保留；升级前stop，修改配置使用 --reconfigure。本轮从旧四卡配置迁移时必须显式 --devices 0 --reconfigure。多卡候选参数保留，但 --devices 0,1 没有 --enable-multi-gpu 会拒绝启动；当前不启用。

### 当前SSH机复现命令

~~~bash
cd /opt/native3dgs/source-image-v3
bash ForServer/deploy/deploy.sh stop --prefix /opt/native3dgs
bash ForServer/deploy/deploy.sh install \
  --prefix /opt/native3dgs --models /opt/native3dgs/models --devices 0 --port 8888 \
  --cuda /opt/native3dgs/toolchains/usr/local/cuda-13.0 \
  --go /opt/native3dgs/go-toolchain/go/bin/go --private-http --reconfigure
~~~

CUDA13通过官方apt包校验后隔离解包，原11.8/12.4和宿主驱动不变，见deploy/install-cuda13.sh。Go bootstrap固定官方archive及SHA256，未改系统PATH/alternatives。无外网时提前把archive放入 go-toolchain/downloads；apt依赖仍需要可达软件源或管理员离线包，不承诺零前置的任意离线安装。

### 生命周期和日志

~~~bash
bash ForServer/deploy/deploy.sh start  --prefix /opt/native3dgs
bash ForServer/deploy/deploy.sh status --prefix /opt/native3dgs
bash ForServer/deploy/deploy.sh logs   --prefix /opt/native3dgs
bash ForServer/deploy/deploy.sh stop   --prefix /opt/native3dgs
bash ForServer/deploy/deploy.sh run    --prefix /opt/native3dgs
~~~

run作为容器entrypoint前台运行；start后台使用同一supervisor，具有锁、PID/命令所有权校验、进程组清理与有界退避重启。不能假设后台进程跨容器重启存活。此次不改容器所属平台的entrypoint；管理员需把run接入现有编排。systemd模板为原生主机可选方案，本机容器非systemd，未实机验收该路线。

日志：logs/gateway.log、supervisor.log，state/instance-model-id.log。认证后的/status含实例pid/gpu/活动数、装载/卸载/渲染计数和缓存统计。models为启动时PLY/SPZ快照、相对路径稳定ID；增删或替换模型后停服重启，内容SHA变化自动隔离旧缓存。不要手工往专用frames目录放业务文件；启动会回收无索引图片和临时文件。

SQLite备份需用SQLite在线backup或停服复制数据库及WAL，不能运行中只复制metadata.sqlite。缓存和过期租约可重建；恢复后实例重新懒加载，启动会清除旧租约。10GiB是图片预算，不是整个prefix磁盘上限；release、日志和数据库需由运维另行保留/轮转。

## 4. 测试与API

~~~bash
cd ForServer/gateway
CGO_ENABLED=1 GOTOOLCHAIN=local go test -race -count=1 ./...
go vet ./...
go build -trimpath -o gs-gateway .
~~~

~~~powershell
ctest --test-dir out/server-windows -C Release --output-on-failure
python ForServer/tools/cloud_v3_latency.py --url http://10.129.4.104:8046 --token-file out/cloud-token.txt --output out/v3-latency.json
~~~

租约API：POST /sessions，body为model-id；POST /sessions/heartbeat或/close，body为session-id。/frame带 X-GS-Session；后台额外带 X-GS-Prefetch:1，必须有有效租约。响应提供 X-GS-View-Code、X-GS-Cache、request/model身份，帧本体不绑定某个用户。错误为JSON状态码400/401/404/410/503；健康GET /health不需要token。共享bearer是管理访问凭据，租约区分查看会话，不是租户RBAC系统。

真实生命周期测试使用单独回环端口与临时状态目录，不改生产TTL，具体完整命令和结果见verification-instances-v3.md。旧tools/gateway.py及旧四卡benchmark保留追溯，不是当前生产HTTP入口。
