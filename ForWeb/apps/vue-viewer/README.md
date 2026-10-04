# Native3DGS Vue 查看器模板

独立项目，版本 `0.2.2-preview.2`。使用安装后的 `@native3dgs/web` 公开接口及 `/vue` 适配器，不引用仓库内部源码。模板源码包自带 `vendor/native3dgs-web-0.2.2-preview.2.tgz`，不需要先发布到 npm。锁文件固定全部依赖。

## 快速开始

Node.js 24.14+、pnpm 11.4.0。解压后在本目录执行：

```powershell
pnpm install --frozen-lockfile --ignore-scripts
pnpm dev
```

访问终端给出的 localhost 地址。需要支持 WebGPU 的浏览器及可用硬件；部署必须 HTTPS。点击打开模型或将一个 PLY/SPZ 拖入画面。不会把本地文件上传到服务器。远程 HTTP(S) 地址需要服务端允许 CORS，HTTPS 页面不能加载 HTTP 混合内容。

```powershell
pnpm test
pnpm format:check
pnpm build
pnpm preview
```

源文件由固定Prettier版本统一格式化；构建包含类型检查，`pnpm assets` 从实际安装包读取资源清单并核验 WASM/JS SHA-256，再复制到 `public/gs-assets`（含第三方许可证）。无需 WASM 编译环境。分发 `dist/` 时保持资源路径与许可证完整；WASM 应返回 `application/wasm`，Worker 与 decoder.mjs 返回有效 JavaScript MIME，不能被 SPA fallback 改成 HTML。

## 项目结构

- `src/App.vue`：SDK生命周期适配器、状态展示、文件/地址输入及控件。
- `src/viewer.ts`：宿主操作会话，处理load identity、取消、消息、PNG下载，渲染/解码仍由SDK负责。
- `src/main.ts`：标准挂载入口，`mount(host)`返回卸载函数。
- `src/style.css`：同一套响应式、中性色和单一强调色；遵循系统深浅主题及reduced motion。
- `src/viewer.test.ts`：候选加载竞态、关闭/取消、非法来源、截图失败、卸载与翻转持久性。
- `scripts/copy-sdk-assets.mjs`、`vendor/`：安装包资源部署及固定SDK。

源码包不包含模型、node_modules、dist、机器路径或凭据。普通包安装仍需下载框架依赖；完全离线部署可预先准备pnpm store。

## Windows 功能对照

|桌面操作|模板入口|
|---|---|
|本地打开/拖放，候选就绪前保留旧模型|文件按钮、拖放、`engine.open`|
|加载进度/取消/关闭|snapshot.progress、LoadOperation.cancel、closeScene|
|固定旋转/右键平移/滚轮|SDK DOM适配器|
|自由浏览/WASD/QE/Shift/Esc|Camera.mode与SDK Pointer Lock/键盘适配器|
|适配/重置/Y翻转|fitScene、camera.reset、camera.setFlipY|
|诊断/设备恢复|公开snapshot及recover|
|截图、远程地址|capture、HTTP(S) Source|

Y翻转是关于场景中心Y的显示反射，不改变文件或canonical相机；旋转、平移、自由浏览方向翻转前后保持一致，fit/reset/open/recover保持翻转状态。**不要添加Canvas CSS镜像，也不要给鼠标增量再乘翻转符号。** 自由模式点击视口请求Pointer Lock；拒绝后左拖仍能转向，方向键与加减键可键盘浏览。Esc、失焦、切模式或卸载都会结束捕获/移动。

桌面CLI路径、WinUI交换链、系统安装器/日志及原生自动减SH/抽样策略不属于浏览器模板。Web SDK保留完整质量，超出WebGPU/内存/存储预算时返回结构化错误并保留旧场景；模板不伪造成功。GPU timestamp不可用时显示“未提供”，GPU时间可能来自早先完成帧，不能当成当前帧准确延迟。

## 集成到现有网站

保留SDK安装依赖，复制App、viewer和style，或只采用SDK hook/composable并替换UI。一个挂载视口对应一个Canvas/engine；卸载由适配器异步dispose，宿主会话取消未完成请求并忽略迟到结果。不要手动复用Canvas，不要读取私有renderer/active，也不要按响应式深层代理包裹WebEngine。

资源地址依据Vite `BASE_URL`，支持子路径发布。例如PowerShell：

```powershell
$env:VIEWER_BASE = '/viewer/'
pnpm build
```

将生成目录部署到同一`/viewer/`路径。默认不添加COOP/COEP，因为当前SDK单线程WASM不依赖SharedArrayBuffer。CSP应允许应用脚本、同源Worker、WASM执行及必要模型connect-src；按网站实际域名配置，不能让资源请求返回登录页或跨域重定向。

升级SDK时替换vendor中的tarball、更新package.json文件依赖并重新生成pnpm-lock.yaml；运行unit/typecheck/build及真实浏览器加载/翻转/卸载验收，再发布。两模板故意各自携带相同viewer/style/tests以保持解压即用；维护时同时更新，仓库工具校验一致性。SDK与模板均为preview，附于原生v0.2.2 release，实际来源commit/hash以MANIFEST.json为准。模型和SparkJS不随包分发；第三方许可在安装包资源licenses中，仓库自有代码遵循仓库所有者条款。
