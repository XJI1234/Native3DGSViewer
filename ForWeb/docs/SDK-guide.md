# Web SDK API、部署与运行契约

适用 `@native3dgs/web@0.2.2-preview.2`。本文描述实际导出的 API；架构设计中的候选接口不是本版的承诺。React19/Vue3.5是可选peer，普通DOM网站无需安装它们。

## 1. 生成、安装和部署资产

```powershell
# 仓库 ForWeb
pnpm install --frozen-lockfile
pnpm run build:wasm
pnpm run build
pnpm pack --out ./native3dgs-web-0.2.2-preview.2.tgz
# 消费项目
pnpm add C:/path/to/native3dgs-web-0.2.2-preview.2.tgz
```

复制包内 `dist/assets/` 到宿主 `public/gs-assets/`，包含 Worker、mjs、wasm、manifest和licenses。可以增加 `scripts/copy-gs-assets.mjs`：

```js
import { cp, mkdir } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
const source = dirname(fileURLToPath(import.meta.resolve('@native3dgs/web/assets/decoder.mjs')));
await mkdir('public/gs-assets', { recursive: true });
await cp(source, resolve('public/gs-assets'), { recursive: true });
```

在宿主构建之前执行。不只复制 wasm。Vite站点初始化时：

```ts
const base = new URL(`${import.meta.env.BASE_URL}gs-assets/`, location.origin);
const assets = { baseUrl: base, workerUrl: new URL('decoder.worker.js', base) };
```

BASE_URL应以`/`结束，子路径网站配置Vite的base。其他框架提供等价的绝对地址。SDK被宿主重新打包时必须显式部署Worker，不依赖库内部Vite生成地址。每次升级同时更新Worker/mjs/wasm，清理旧缓存，按manifest验证哈希。

`.wasm` MIME为application/wasm，`.mjs/.js`必须合法JavaScript MIME，模型application/octet-stream。生产HTTPS；localhost/127.0.0.1可开发。Worker建议同源；模型和解码资产跨源时需CORS。本版不要求COOP/COEP、SharedArrayBuffer或pthreads。CSP应允许对应模块、Worker和WASM编译，例如按站点设置 `worker-src 'self'`、`script-src 'self' 'wasm-unsafe-eval'`、connect-src资产/模型源；需要在宿主真实策略下验证。

## 2. 初始化与释放

```ts
import { createEngine } from '@native3dgs/web';
const created = await createEngine({ canvas, assets });
if (!created.ok) {
  showError(created.error.code, created.error.diagnostic);
} else {
  const engine = created.value;
  engine.resize(1920, 1080); // 物理像素；CSS由宿主管理
  const operation = engine.open({ kind: 'url', url: modelUrl });
  const loaded = await operation.result;
  if (!loaded.ok) showError(loaded.error.code, loaded.error.diagnostic);
  await engine.dispose(); // 路由卸载/视口销毁时
}
```

Canvas的WebGPU context由单一引擎拥有，不能与WebGL/Three.js或另一个实例共享。创建请求adapter/device并执行排序自检。navigator.gpu存在不足以证明支持。没有能力返回UnsupportedCapability，由宿主决定替代界面。Stopped实例不能复用，应创建新Canvas/引擎。

EngineOptions为`{canvas,assets?,limits?,maxFramesInFlight?}`，limits可部分覆盖。createEngine/WebEngine.create返回Promise<Result<WebEngine>>。每个Result必须处理ok:false；错误diagnostic用于诊断，不作为程序分支条件。

`maxFramesInFlight`为初始化选项，只接受1、2、3；本版默认2。它限制自动绘制等待GPU完成的提交数量，允许CPU准备与GPU执行重叠；不是GPU并行队列，也不保证对应物理显示帧数。队列满时仅保留最新相机状态；不会积压每个鼠标事件。需要原单帧门控时设为1；本机3没有优于2的稳定收益。此值在创建时验证并固定，修改调用方options对象不会动态调整引擎。捕获、首帧验证和释放仍等待相关GPU工作完成。React/Vue的AdapterOptions继承此字段，在hook/composable初始化时传入；运行中切换应销毁并重新创建实例，不能当作响应式渲染参数。

## 3. 完整公共方法

| 方法/属性 | 契约 |
| --- | --- |
| open(source) | 立即返回{requestId,result,cancel}；result为Promise<Result<void>>；自动取消较旧请求 |
| resize(width,height) | Result<void>；非负整数物理像素，最大值受GPU limit约束；零尺寸暂停提交 |
| getSnapshot() | 稳定、冻结的只读快照；不修改返回值 |
| subscribe(callback) | 返回取消订阅函数；回调内重新读快照，异常隔离 |
| pause()/resume() | 停止/恢复按需绘制；不取消加载，不覆盖交易/恢复/错误阶段 |
| requestFrame() | 标记下一帧绘制；无场景或隐藏页面不提交 |
| fitScene() | Result<void>；按当前模型和宽高重新适配 |
| closeScene() | Promise<void>；取消加载、清空画布/活动/恢复场景，可以再次打开 |
| recover() | Promise<Result<void>>；并发恢复合并，最多2次重建，保留CPU场景和相机 |
| capture() | Promise<Result<{width,height,rgba}>>；RGBA8、上到下、连续行、黑色不透明背景，返回自己拥有的副本 |
| dispose() | Promise<void>；等待在途上传/GPU/恢复清理，最后Stopped；幂等 |
| camera | Camera实例，见第5节 |
| capabilities | adapter信息、pageBytes、execution:'main'；不是剩余显存查询 |
| maxViewportDimension | 当前设备允许的视口维度上限；适配器按比例限制物理像素尺寸 |

Snapshot字段：phase、requestId、sceneCount、degree、source、progress、error、stats、deviceGeneration、viewportRevision。phase为Idle/Loading/Uploading/Ready/Suspended/Recovering/Faulted/Stopping/Stopped。Loading/Uploading时可以仍显示旧模型；Ready表示首帧GPU完成并交给Canvas，不证明物理显示器已扫描。替换失败/取消保留旧模型及相机。恢复期间open返回DeviceLost。closeScene与恢复并发时设备可能继续重建，但关闭的模型不会复活。

progress包含stage/done/total，total可null。Downloading/Inflating/Rebasing/Uploading单位字节，Decoding/Packing单位点；不同阶段不能直接相加。加载总deadline也包括等待非零视口和上传。候选场景先离屏验证，再验证实际Canvas呈现；成功后才替换活动场景和相机，失败或取消恢复旧画面，包括暂停状态。capture仅用于稳定场景，不在加载/恢复中截取，GPU读回受预算及maxBufferSize限制。

stats最多约4Hz发布。cpuMs是提交CPU时间；gpuMs是可选异步timestamp结果；gpuFrameId指出该结果所属的旧帧。不能将gpuMs称为当前帧耗时或直接换算用户FPS。更换场景不复用旧场景计时。

## 4. 模型、格式与取消

```ts
const local = engine.open({ kind: 'blob', blob: file, name: file.name });
const remote = engine.open({ kind: 'url', url: new URL('/models/site.spz', location.href).href });
remote.cancel();
const result = await remote.result;
```

支持Graphdeco binary_little_endian PLY、基础SPZ1–4、完整SH0–3。PLY默认源RDF，规范化为RUB；已经RUB的PLY用plyCoordinates:'rub'。SPZ按规范转RUB，此选项不改变SPZ。ASCII/压缩PLY、SH4、SPZ antialiased/未知扩展不支持，不静默降低质量。部分模型源朝向与默认相机不一致，可设置正确相机，不能重复翻转已经规范化的数据。

URL必须绝对HTTP(S)，浏览器执行CORS。当前无自定义credentials/requestHeader、HTTP Range或边下载边显示。实际接收字节始终受inputBytes限制。只有可确认identity编码或可信同源未编码响应才比较Content-Length；HTTP压缩及跨源隐藏Content-Encoding时，编码长度不能作为浏览器解码后的长度。Blob分块处理不等于半模型可见。cancel终止独立解码Worker，下载随Worker终止。cancel不是throw，操作result返回Cancelled；超时返回Timeout。加载及恢复均有总deadline；GPU等待和backing读取会响应取消，底层已提交GPU或存储工作可能随后完成，但迟到结果不能替换新场景。

## 5. 相机与控制

Camera使用float64世界坐标，GPU frame使用相对模型origin的float32。Pose为`{position:Vec3,target:Vec3,up:Vec3}`；Vec3是readonly三元组。getPose返回副本，setPose支持roll；up不能平行视线。本版fov固定60度，没有可调quality/FOV插件契约。

| Camera API | 单位/语义 |
| --- | --- |
| orbit(dx,dy) | 物理像素位移，0.005rad/px，围绕target，重置roll为世界up |
| pan(dx,dy) | 物理像素，使用当前距离/视口，保持朝向 |
| dolly(delta) | 有限数、指数缩放距离；推荐滚轮方向±1 |
| look(dx,dy) | 保持位置改变朝向，重置roll |
| fly(right,up,forward,seconds) | 相机局部轴速度系数；位移=系数×当前相机距离×clamp(seconds,0,0.1)，forward正值向前 |
| getPose()/setPose(pose) | RUB世界坐标 |
| reset() | 最近fit所产生的初始视角 |
| revision | 只读修订号 |

相机方法是同步数学操作，非法输入抛Error，宿主应try/catch。引擎异步方法使用Result，两类错误方式不同。操作后requestFrame；引擎也检查相机revision。不要直接改内部字段。

Camera类声明还包含引擎低层使用的fit(bounds,width,height)、resize(width,height)、frame(bounds,width,height,count,degree,stride,pageCapacity)。普通集成使用engine.fitScene/resize和上表控制方法；frame输出128字节内部GPU uniform ABI，依赖renderer布局，不属于网站独立渲染接口。后续公共声明收敛需专门迁移，不建议宿主依赖此内部布局。

适配器提供鼠标/单指轨道、右键平移、滚轮缩放；没有完整pinch/多指或键盘飞行绑定。可由宿主将相机fly/look绑定自己的键盘/手柄，不把不存在的触控机制称为已完成。

## 6. 资源预算和错误

| Limits | 默认 | 说明 |
| --- | --- | --- |
| inputBytes | 8GiB | URL/Blob/File输入的字节上限 |
| sceneBytes | 8GiB | 规范化/packed场景字节预算；大场景不要求整份驻留WASM |
| cpuBytes | 512MiB | 引擎输入/解压/批次/pack/旧内存场景的保守估算，不是测量RSS |
| gpuBytes | 8GiB | 新旧模型/排序/投影/截屏预算，不是显卡剩余VRAM |
| timeoutMs | 600000 | 完整加载期限；毫秒 |

所有limit都是正safe整数，timeoutMs还必须≤2147483647，避免浏览器计时器溢出。大PLY和legacy gzip SPZ采用分块WASM及OPFS backing，输入页每页最多128MiB、页数不限；单页投影结果进入同一个全局稳定排序。紧凑SH3每点236字节；流式布局按64点属性分块，末尾补齐到64点但逻辑点数不变。投影40字节，排序双缓冲16字节，再加radix scratch。单投影buffer仍受设备binding上限约束，真实GPU分配可能因其他程序占用或驱动限制失败。默认8GiB是策略预算，不代表显卡可用空间。小PLY和SPZ v4保持有界内存路径，v4超过其WASM估计峰值时显式ResourceLimit。替换时新旧同时驻留，宿主可先await closeScene()以降低峰值，但会放弃保留旧模型的失败回退。

OPFS需要安全来源（HTTPS或localhost）、浏览器存储权限与足够配额。解码临时存储峰值约为输入文件＋解压SPZ属性＋packed backing；成功后只保留backing，closeScene/dispose等待取消与清理。浏览器私密模式可能限制大文件快照，写入长度与File快照长度会核验。配额/快照截断返回ResourceLimit；没有OPFS返回UnsupportedCapability。浏览器异常退出可能留下临时目录；正常API关闭会释放。生产宿主应避免在多标签页活跃时盲目删除其他实例目录。

cpuBytes限制引擎分配的保守估计；磁盘File backing的residentBytes为0，表示没有由引擎保留的整份ArrayBuffer。浏览器/操作系统页缓存、宿主自行创建的Blob内存不在该计数内，不能据此推断RSS为零。提高预算不能突破物理内存、GPU buffer上限或存储配额，不能保证资源不足的任意电脑完整渲染。

OPFS删除等待最多5秒；超时或删除失败不阻止GPU释放和进入Stopped，底层存储可能继续回收。宿主应在closeScene/dispose后检查getSnapshot().error：stage为StorageCleanup标记清理故障，不能因为Promise<void>完成就推断磁盘删除成功。错误使用现有ErrorCode，不新增名为StorageCleanup的公共枚举。异常退出残留及清理重试需由宿主管理。

Snapshot.stats的projectionMs、sortMs、drawMs是gpuFrameId对应已完成采样帧的GPU阶段时间；gpuMs包含阶段间开销。正在提交的frameId与gpuFrameId可能不同。没有timestamp-query时GPU项为null；未变更视角的采样帧投影/排序为0。自动渲染最多保持一个在途GPU帧，在完成时合并最新camera变化。

ErrorCode为UnsupportedCapability/InvalidInput/UnsupportedFormat/ResourceLimit/OutOfMemory/DecoderFailure/Cancelled/Timeout/NetworkFailure/DeviceLost/Stopped。ResourceLimit提示选择小模型或明确调整预算；OOM不无限重试。设备丢失有界恢复；Faulted后可以显式recover。主动dispose/destroy不触发自动恢复。uncapturederror进入Faulted。不要把桌面单卡结论推广到手机/Safari。

## 7. SSR、多视口和生命周期

三个ESM入口可在无window/document的Node中导入；实际创建必须在浏览器mount/useEffect之后。每个视口独占Canvas/device/Worker；多视口重复占用显存，宿主需设总预算。适配器自动管理Canvas、ResizeObserver、事件和卸载，但宿主自己的异步回调仍需mounted/token保护。

参数在mount时确定；改变模型用open，改变assets/limits/pixelRatio配置需要卸载重建。不要深响应式代理或序列化WebEngine。Vue适配器使用shallowRef/markRaw，React使用useSyncExternalStore。加载模型的effect/watch必须在清理时cancel，避免路由卸载后的工作。

## 8. 验证和故障定位

test:sdk独立安装tgz、文档TSX/SFC严格类型检查、NodeSSR导入、生产bundle、React开发StrictMode真实effect重放及Vue同页创建、真实WASM加载、卸载后0device/0Canvas/Stopped验证。GPU/真实模型/基准分别运行并保留证据。部署故障首先检查Network中Worker是否实际JS、WASM MIME、CSP/CORS、版本一致性、容器非零尺寸和error.code；不要通过宽松任意源CSP掩盖路径错误。


## 查看器导航与 Y 轴翻转（preview.2）

```ts
engine.camera.setFlipY(true); // 显示模型Y镜像，canonical pose不变
engine.camera.setMode('fly'); // 默认是orbit，框架适配器自动切换输入
engine.requestFrame();
```

SDK直接构造反射投影视图，不要再给Canvas添加CSS scaleX/scaleY。翻转状态跨fit/reset/open/recover保留，鼠标增量来自未变换client坐标。固定模式左拖旋转、右拖平移、滚轮缩放；自由模式点击视口进入Pointer Lock，鼠标转向、WASD/QE移动、Shift加速，Escape/失焦退出；拒绝Pointer Lock时支持左拖转向。方向键与加减键可在聚焦Canvas时操作。`getPose()`/`setPose()`始终使用未反射的canonical坐标。模板仅使用公开SDK、hook/composable、snapshot与Result，不读取renderer或active字段。
