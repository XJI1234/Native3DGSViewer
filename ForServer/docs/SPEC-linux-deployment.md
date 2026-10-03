# Spec: linux-deployment（Go/SQLite与单GPU默认）

## 目标与边界

一键诊断/依赖检查与可选安装、Release/native CTest/Go race/vet/build、staging安装/启动健康验证。部署脚本位于deploy/，生产Go网关gateway/，Python仅负责监管/部署/测试和旧版回退。不安装宿主驱动、不改CUDA alternatives/conda/其他工程。失败非零、保留日志、只清理本安装自有进程。配置/token/state独立于release。

## 命令

~~~bash
bash ForServer/deploy/install-go.sh /opt/native3dgs/go-toolchain
bash ForServer/deploy/deploy.sh install --prefix /opt/native3dgs --models /data/models --devices 0 --port 8888 --cuda /usr/local/cuda-13.0 --go /opt/native3dgs/go-toolchain/go/bin/go --install-deps --instance-idle 300 --cache-ttl 86400 --cache-bytes 10737418240 --max-instances 4 --private-http
~~~

支持doctor/install/start/stop/status/logs/run，已有配置默认保留；修改需stop后--reconfigure。默认device0，多设备需要--enable-multi-gpu。上述--private-http只用于已授权可信网络，正式非可信网络使用--cert/--key。健康检查使用配置bind，通配地址映射到IPv4/IPv6回环；TLS证书需覆盖实际探测地址并有对应信任。

Go>=1.23/cgo/libsqlite3-dev。install-go.sh固定官方Go1.27.1 archive/SHA256、独立prefix，支持预置downloads离线archive，不改PATH/alternatives。--install-deps仍需apt软件源或管理员预置包。CUDA独立install-cuda13.sh只校验解包toolkit，不运行驱动/maintainer脚本。GPU架构由devices实际CC推导。GOTOOLCHAIN=local阻止静默下载不同Go工具链。

## 生命周期和持久化

构建测试后current切换，失败恢复current/previous、配置和supervisor；启动尝试失败或中断，先停新supervisor再恢复，无法停服则保留恢复资料。安装拒绝仍存活的自有supervisor，不以HTTP探测失败判断已停服。保留配置时，CUDA架构来自配置中的GPU；仅解包已验证toolkit文件。prefix规范化后拒绝根目录，worker端口校验覆盖整个实例范围。token不输出，配置不含token本体。supervisor锁、PID命令归属、进程组清理、有界重启；原生实例Pdeathsig辅助回收。run为容器前台入口，start为同一监管机制的交互后台方式。启动成功不保证跨容器重启持续存在，应接现有编排entrypoint；systemd模板为可选原生主机路线。

状态SQLite WAL、frames目录、实例日志与release分离；图片10GiB预算不包含数据库/日志/release。备份用SQLite在线backup或停服连同WAL复制。专用frames目录启动清临时/无索引文件，勿放业务数据。模型增删/替换需停服重启快照，SHA变更自动分缓存命名空间。

## 验收

bash -n、doctor、真实install、Go race/vet、native CTest、repeat start/status、GPU配置拒绝、远端8046及实际WinUI。SSH8045不能作服务端口。最终证据见verification-instances-v3.md，区分历史部署失败路径和本轮实测；未在该容器验证systemd/外部编排重启/公网TLS。保留license与源码依赖pins。
