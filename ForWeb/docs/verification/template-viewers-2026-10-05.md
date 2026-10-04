# React / Vue SDK 模板验收

2026-10-05。基于已提交并打包的 `@native3dgs/web@0.2.2-preview.2`，两个独立模板均通过安装包消费、strict类型检查、生产构建、7项宿主unit、固定Prettier格式检查和真实Edge154/WebGPU浏览器验收。SDK运行代码在模板工作阶段不再修改，模板vendored tarball与SDK发布包完全一致。

## 结构与公开边界

React19.3 / Vue3.5.43、Vite8.3.2，分别使用公开useNative3DGS hook/composable。React用useSyncExternalStore观察宿主会话，Vue用shallowRef观察原始engine/session；适配器拥有Canvas、引擎和异步dispose。ViewerSession只管理候选名、请求代际、消息、取消和PNG下载，不实现解码或渲染。相机诊断每100ms读取公开getPose，仅值变化时更新，dispose停止计时器；不使用节流的snapshot.stats作为实时相机坐标来源。

每个模板包含源码、严格配置、锁文件、verified assets脚本和vendor SDK。SDK不是指向未发布公共包的虚构依赖，也不导入ForWeb/src。两个框架内的viewer/style/tests保持字节一致。README涵盖资源部署、客户端边界、子路径、CORS、固定版本升级及Windows功能对照。

## 功能矩阵与结果

|验收|React production/development|Vue production/development|
|---|---|---|
|实际PLY/SPZ文件加载|production 170,799 / 804,758全点SH3|production同规模全点SH3|
|独立非对称fixture|均成功加载3点|均成功加载3点|
|Y显示反射|红色特征x保持562.755，y512.786→207.214，视口高721|一致|
|翻转前后旋转/右拖平移|相同原始鼠标增量得相同canonical相机|一致|
|fit/reset/zoom与持久翻转|通过|通过|
|自由Pointer Lock / WASD / Esc|松开移动键仍锁定，Escape退出|通过|
|截图|下载PNG可解码、尺寸及红色特征位置与画布一致|通过|
|迟到取消|不同1点响应完成后仍保留3点旧场景与像素|通过|
|URL错误回退|404显示诊断，旧场景保留|通过|
|设备恢复|注入uncaptured validation error并销毁设备，恢复后点数/镜像像素正确|通过|
|卸载|development Canvas及GPU device计数归零|通过|
|320/768/1024/1440布局|无横向溢出|通过|

设备故障使用浏览器GPU API注入，不是物理硬件拔出。SDK历史38模型完整加载、稳定性和性能结果见并行调优报告；模板测试使用代表性模型，不把小模型模板验收重述为38模型重测或性能基准。

## UI 审阅与完善

界面以查看器任务为中心，大视口、紧凑工具栏、可隐藏信息面板；系统深浅色，中性背景与单一绿色强调。手机面板移到视口下方，44px控制尺寸、原生按钮/输入、可见focus、aria-pressed/状态/alert与reduced motion支持。design-taste预检12文件，0硬性违规、0警告。

|初版观察|完善后|原因|
|---|---|---|
|相机位置跟随节流统计而滞后|独立读公开相机且仅变化通知|结束拖动后仍给出准确诊断|
|旧宿主错误可能遮挡设备故障|诊断函数优先当前Faulted，并清除已成功操作错误|恢复操作有正确上下文|
|SDK资源复制可能残留旧文件|先校验源资源，再确认生成目录边界并替换|构建只携带当前资产|
|下载/恢复验收只看状态|核验实际PNG与恢复画面特征|避免空白输出伪成功|

## 审查、构建与发布

OCR初审19文件（含工具），修复错误优先级、包清单、环境文件排除、Python脚本、异步测试cleanup和像素验收；后续修复路由等待、Vite创建失败清理、独立run目录、外部模型环境预检。全部真实GPU测试串行，原有用户5173服务未停止。最终审查文本和模板build/test JSON为独立证据。

记录完整inputs/outputs哈希、源码allowlist和实际SDK tarball SHA。打包前要求已提交且无变更，archive MANIFEST标明模板sourceCommit、sdkSourceCommit与旧v0.2.2 tag。两源码包均需解压到新目录、frozen安装、unit和build成功，六个正式资产（3 ZIP与各自sha256）上传原release；原Windows/Android/Cloud资产保留。WebGPU/内存/存储资源约束及私密OPFS偶发问题仍见SDK报告，不保证资源不足的电脑能加载任意模型。


### 最后工具审查处置

后续OCR指出证据记录未强制fresh build、Python优化可能禁用assert、shutdown无界、发布ZIP会直接覆盖、浏览器hash采于结束及仅验证单色特征。现均修复：记录器自身执行frozen install/unit/format/build并核对前后输入；Python使用-E；shutdown含两次有界等待；临时ZIP验证后rename发布；浏览器启动前匹配强制build证据并核对结束时一致性；capture/recovery/cancel进行全RGB图像差异检查（容差2灰阶，截图时隐藏宿主文字叠层）。OCR低优先级静态版本命名建议不影响此固定v0.2.2/preview.2发布，当前版本和SDK完全匹配。没有未处置的high/medium发现。
