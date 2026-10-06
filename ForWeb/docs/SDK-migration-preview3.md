# Web SDK preview.2 → preview.3 迁移

2026-10-06。目标包 `@native3dgs/web@0.2.2-preview.3`；从 [v0.2.2 Release](https://github.com/XJI1234/Native3DGSViewer/releases/tag/v0.2.2) 下载 preview.3 SDK 包；未发布 npm。验收状态与数据以 [并行解码报告](verification/pthreads-2026-10-06.md) 为准。

## 兼容性与变化

**默认行为变化：preview.3 自动启用 adaptive 排序与可回退的多线程 WASM（auto/4）。**

原有 `createEngine`、`WebEngine.create`、`open`、`Source`、`Result`、`ErrorCode`、相机、Y 翻转、截图、取消/关闭/恢复/释放、React hook 和 Vue composable 签名保持。未改变模型完整点数、SH0–3、float32 GPU 数据精度。preview.3 默认 adaptive 排序允许小位移过程中的暂时透明混合顺序变化，但保持当前相机投影和完整可见点。`maxFramesInFlight` 仍默认 2，WASM 线程数与 GPU 在途帧数独立。

新增可选 `EngineOptions.decoder`，及只读可选 `Snapshot.decoder`。新字段是增量契约：旧项目无需修改加载调用，旧代码构造 Snapshot 类型也不被要求补齐该字段。实际新引擎空场景/关闭/释放时字段为 `null`；加载成功后记录该活动场景的解码路径；候选失败/取消保留旧场景诊断；恢复保留已解码场景信息。

| 设置 | 行为 |
| --- | --- |
| 不设置或 `{ mode: 'auto' }` | 默认自动策略。非隔离页面不请求 pthread 资产；小模型、SPZ v4 或资源不足选择单线程；增强版初始化失败可回退并记录原因 |
| `{ mode: 'single' }` | 强制单线程，便于回归、受限宿主或对照测试 |
| `{ mode: 'auto', threads: 4 }` | 线程上限包含协调线程；实际数量还受 hardwareConcurrency 和策略限制 |
| `{ mode: 'parallel', threads: 4 }` | 显式验收/诊断；缺少隔离、增强版初始化失败或预算不够时返回错误，不掩盖增强路径失败；不将不支持并行的格式路径伪装为并行 |

`threads` 必须是 1–8 的整数；1 选择基础运行时。默认上限 4；自动并行需要分块路径、至少 262,144 点、至少两个可报告硬件线程及足够 CPU 预算。Small PLY/SPZ v4 的原有有界内存路径继续单线程；PLY 和解压后的 legacy SPZ 批次并行，gzip 流仍串行校验，重定位在当前版本保持串行。`mode: 'parallel'` 可对低于自动点数门槛的分块模型测试，但不会将小型内存路径强行切为另一个布局。

增强版使用 C++20/Emscripten pthreads，不要求 Rust、SIMD、WASM64 或主页面 Atomics.wait。增加有界预取、独立任务状态、无重叠 tile 输出和 bounds 归并；存储句柄仍由协调 Worker 独占。线程栈及额外批次内存计入 CPU admission。提高线程数不保证更快，也不能突破硬件 RAM/VRAM、WebGPU binding 或 OPFS 配额。

## 升级步骤

1. 将 SDK tarball 替换为新版本，更新项目依赖并重新生成/提交锁文件；不直接修改 `node_modules`。本地安装示例：`pnpm add C:/path/to/native3dgs-web-0.2.2-preview.3.tgz`。模板采用 vendor 文件依赖时，同步替换 vendor 文件及 package.json 引用。
2. 在宿主 dev/build 前递归复制新包的整个 `dist/assets/`，保持许可证和 manifest。两框架模板的 `scripts/copy-sdk-assets.mjs` 会按 manifest 检查哈希并递归复制，可继续使用。
3. 保持已有 `assets.baseUrl`、`assets.workerUrl` 和宿主 base 路径一致。清除旧 Worker/mjs/wasm、CDN 和 Service Worker 缓存，不能混用不同 SDK 版本的解码资产。
4. 原站点仅需单线程时，HTTPS 和现有 MIME 配置足够。希望启用增强版时，按下节配置隔离，并验证实际响应及诊断。
5. 运行宿主类型检查/构建，实际打开 PLY/SPZ，检查取消、模型替换、卸载、Y 翻转和设备恢复。非隔离页面再验收一次，确认仍可单线程运行。

新资产结构：

```text
gs-assets/
  decoder.worker.js
  decoder.mjs
  decoder.wasm
  threaded/
    decoder.mjs
    decoder.wasm
  manifest.json
  licenses/
```

Emscripten pthread 启动 Worker 复用 `threaded/decoder.mjs`；没有需要手工编写的第三个 pthread 脚本。manifest 的 `files[].name` 可以包含 `threaded/` 相对路径，自定义复制脚本必须保留目录，不能只取 basename。

## HTTPS、跨源隔离与 MIME

增强版需要 HTTPS（localhost 允许 HTTP）、COOP/COEP 和 SharedArrayBuffer。它们是服务器响应头，不是 HTML meta 标签。在现有 HTTPS Nginx `server` 块内配置：

```nginx
add_header Cross-Origin-Opener-Policy "same-origin" always;
add_header Cross-Origin-Embedder-Policy "require-corp" always;

location = /gs-assets/decoder.mjs {
    types { }
    default_type application/javascript;
    try_files $uri =404;
}
location = /gs-assets/threaded/decoder.mjs {
    types { }
    default_type application/javascript;
    try_files $uri =404;
}
```

沿用 server 的网站 `root`，若使用 alias 或子路径需调整目录与 location。确认 `.js` 返回 JavaScript MIME、两个 `.wasm` 返回 `application/wasm`。也可在全局 mime.types 的 JavaScript 行加入 mjs；详细排错见 [SDK 部署说明](SDK-guide.md#12-nginx-的解码资产-mime-配置)。`nginx -t` 成功后再重载；宝塔可在对应网站配置文件中修改并使用面板重载。

COEP 需覆盖文档及相关 Worker 响应。若子 location 自己声明 add_header，应按实际 Nginx 的继承规则确认这些头没有丢失。开启 require-corp 后，第三方脚本、CDN 解码资产、跨源模型、图片等也必须满足相应 CORS/CORP；SDK 不会代替网站修改其他资源的响应头。

浏览器控制台检查：

```js
window.isSecureContext
window.crossOriginIsolated
typeof SharedArrayBuffer !== 'undefined'
```

三项都应为 true，但仍需模型加载后检查 `snapshot.decoder?.backend === 'pthreads'`。支持 WebGPU 不等于启用跨源隔离。无法配置隔离的普通嵌入网站保留 auto 或 single，仍可加载模型。

本地 SDK 开发服务可显式设置环境变量（不改变模板默认部署要求）：

```powershell
$env:GS_CROSS_ORIGIN_ISOLATED = '1'
pnpm run dev
```

## DOM、React 与 Vue 使用

DOM 初始化增量配置，模型加载 API 保持：

```ts
import { createEngine } from '@native3dgs/web';
const base = new URL(`${import.meta.env.BASE_URL}gs-assets/`, location.origin);
const result = await createEngine({
  canvas,
  assets: { baseUrl: base, workerUrl: new URL('decoder.worker.js', base) },
  decoder: { mode: 'auto', threads: 4 },
});
if (result.ok) {
  const engine = result.value;
  const loaded = await engine.open({ kind: 'blob', blob: file, name: file.name }).result;
  if (loaded.ok) console.log(engine.getSnapshot().decoder);
  // 宿主销毁视口时 await engine.dispose()
}
```

React 和 Vue 都在原 options 中增加字段即可：

```ts
const options = {
  assets: { baseUrl: base, workerUrl: new URL('decoder.worker.js', base) },
  decoder: { mode: 'auto' as const, threads: 4 },
};
// React: useNative3DGS(hostRef, options)
// Vue: useNative3DGS(hostRef, options)
```

选项在创建时确定；运行中改 mode/threads 需要卸载并重建，不把它当成每帧响应式参数。实际诊断为 `{ backend: 'single' | 'pthreads', threads, fallbackReason: string | null }`。回退原因用于展示与排错，不解析其文本作为程序分支；根据 backend 分支。现有错误仍通过 Result/EngineError 处理，未新增必须识别的错误枚举。


## 默认排序自适应延续

新增 `EngineOptions.sorting`，DOM、React `useNative3DGS` 与 Vue
`useNative3DGS` 使用相同选项；省略时为 `adaptive`。原项目 API 调用不需要修改，但默认排序行为发生变化；要保留旧行为显式设置 strict。
此功能独立于 WASM 线程，不要求 SharedArrayBuffer 或 COOP/COEP。

```ts
const created = await createEngine({
    canvas,
    assets: { baseUrl: new URL('/gs-assets/', location.href) },
    decoder: { mode: 'auto', threads: 4 },
    sorting: { mode: 'adaptive', maxSortAgeMs: 100, targetFrameMs: 1000 / 60 },
});
```

`maxSortAgeMs` 默认 100，接受有限的 16–1000 ms；`targetFrameMs`
默认 1000/60，接受有限的 4–100 ms。它是调度目标，不是帧率承诺。
选项在初始化前校验、复制并冻结，修改原对象不影响引擎；设备恢复沿用。
当前没有运行时切换策略的 setter，更改策略应按原有卸载流程重建引擎。

每帧更新屏幕投影、SH 与裁剪，仅沿用最近提交的完整点排序。
沿用包含全部点的完整索引，逐帧清除不可见标志并在顶点阶段裁掉无效项，避免沿用旧可见数量遗漏新出现的点。
小位移排序更新间隔根据完成的 GPU 排序成本估计；交互停止后自动补齐
待更新排序。首次呈现、模型替换、恢复、截图、视口/质量/Y 反射变更和
相机突变强制刷新。暂停、后台、零尺寸或队列已满时沿用原暂停/限流机制，
年龄上限不代表能在这些情况下强制提交，也不是 GPU 完成 deadline。
近远平面的动态变化在当前投影中处理，不改变径向排序关系。

`snapshot.stats` 增加可选 `sortAgeMs`、`sortPositionErrorRatio`、
`sortReason`。前三项描述本次提交；GPU 阶段耗时仍描述
`gpuFrameId` 指向的已完成测量帧，不能直接认作当前 `frameId`。
`sorted` 表示本次编码了 radix；`sortAgeMs` 是复用排序的提交年龄，
不表示浏览器显示器扫描输出时间。相机位置未变时径向关系可长期复用，
因此静止/纯转向的年龄可能大于 maxSortAgeMs，而位移误差为零。

动态浏览默认使用 adaptive；需要每个移动帧精确透明度排序的
宿主显式使用 strict。capture 在 adaptive 下也会重新排序。性能结果与
有效范围见 [自适应排序报告](verification/adaptive-sorting-2026-10-06.md)。

没有 timestamp-query 的适配器使用 targetFrameMs 固定刷新间隔，不能据当前 RTX 3080 的实测推断其收益；仍保留 strict。


### 保留旧版本行为 / 单线程宿主

```ts
const result = await createEngine({
  canvas,
  assets: { baseUrl: new URL('/gs-assets/', location.href) },
  sorting: { mode: 'strict' },
  decoder: { mode: 'single' },
});
```

默认多线程是能力自适应的 auto/4，不是强制 parallel。非隔离、小模型、
SPZ v4、单核或预算/增强初始化受限时自动选择单线程并报告 fallbackReason；
排序仍可自适应。实际路径以 Snapshot.decoder 为准。已部署项目即使只升级
依赖也会启用 adaptive；需要严格逐帧顺序的项目按上例选择 strict。

汇总 JSON 与测试脚本保存在 GitHub 仓库的 ForWeb/docs/verification/evidence/；SDK 包仅携带验证报告 Markdown，不包含模型、基准 JSON 或 Spark。

逐帧采样与浏览器截图作为本地原始档案保留，Git 记录汇总、归属和输入/服务资产核验，不将其作为 SDK 运行资产。
