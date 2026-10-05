# Vue 3.5 接入指南

适用Vue3.5、TypeScript及`@native3dgs/web@0.2.2-preview.2`。先按[SDK指南](SDK-guide.md)安装本地tgz，将完整dist/assets复制到public/gs-assets。Vue为optional peer，不依赖Three.js。当前真实验证Windows/Edge154/RTX3080；移动/Safari另行验收。

0.2.2-preview.2增加`maxFramesInFlight: 1 | 2 | 3`，可通过`useNative3DGS(hostRef, { assets, maxFramesInFlight: 2 })`初始化；默认2，1保留单帧门控。现有实例不会响应options对象字段的修改；需要变更时重新创建实例。结果及边界见[连续交互调优记录](verification/parallel-optimization-2026-10-05.md)，双帧上限不代表两个GPU队列，也不保证物理显示帧率。

## 1. 模块与生命周期

引擎类型从`@native3dgs/web`导入，composable从`@native3dgs/web/vue`导入。`useNative3DGS(hostRef,options)`必须在setup中调用，内部onMounted安装post-flush host watcher；容器出现、替换或v-if移除时重新绑定/清理，onBeforeUnmount清理事件、订阅、Canvas并异步dispose。

返回shallowReadonly的shallowRef：engine、snapshot、initializationError。初始化前engine和snapshot为null；错误显示initializationError。实例用markRaw，不把GPU对象纳入Vue深响应式代理。不要再把engine放进reactive/Pinia持久化、不要JSON.stringify引擎；仅持久化自己的模型URL/相机Pose等普通数据。

## 2. 可直接使用的单文件组件

下面的Vue SFC给出URL/File输入、取消、视角、错误和进度。Vite/Vue项目需宿主自己的Vue插件；本SDK的库包不捆绑组件编译器。

本轮示例编译组合为Vite8.3.2、@vitejs/plugin-vue6.0.9、vue-tsc3.3.12、TypeScript6.0.3。引擎自身使用TypeScript7.0.2；当前vue-tsc依赖传统`typescript/lib/tsc`，在7.0.2上实际报ERR_PACKAGE_PATH_NOT_EXPORTED。Vue宿主先使用此兼容的6.0.3组合，待Vue工具链支持后再升级，不把版本号更新当作兼容验证。

```vue
<script setup lang="ts">
import { computed, shallowRef, watch } from 'vue';
import type { EngineError, Source } from '@native3dgs/web';
import { useNative3DGS } from '@native3dgs/web/vue';

const props = withDefaults(defineProps<{ modelUrl?: string; height?: number }>(), { height: 600 });
const host = shallowRef<HTMLElement | null>(null);
const file = shallowRef<File | null>(null);
const loadError = shallowRef<EngineError | null>(null);
const cancel = shallowRef<(() => void) | null>(null);
const base = new URL(`${import.meta.env.BASE_URL}gs-assets/`, location.origin);
const { engine, snapshot, initializationError } = useNative3DGS(host, {
  assets: { baseUrl: base, workerUrl: new URL('decoder.worker.js', base) },
  pixelRatio: Math.min(window.devicePixelRatio, 2),
  // 小模型嵌入示例主动限制预算；完整38模型验收使用默认预算。
  limits: { inputBytes: 256 * 2 ** 20, gpuBytes: 512 * 2 ** 20 },
});
const source = computed<Source | null>(() => {
  if (file.value) return { kind: 'blob', blob: file.value, name: file.value.name };
  return props.modelUrl
    ? { kind: 'url', url: new URL(props.modelUrl, location.href).href }
    : null;
});

watch([() => engine.value, source], ([instance, input], _old, onCleanup) => {
  if (!instance || !input) return;
  let alive = true;
  loadError.value = null;
  const operation = instance.open(input);
  cancel.value = operation.cancel;
  void operation.result.then(result => {
    if (alive && !result.ok && result.error.code !== 'Cancelled') loadError.value = result.error;
  });
  onCleanup(() => {
    alive = false;
    operation.cancel();
    if (cancel.value === operation.cancel) cancel.value = null;
  });
}, { immediate: true });

const failure = computed(() => initializationError.value ?? loadError.value ?? snapshot.value?.error);
function selectFile(event: Event) {
  file.value = (event.target as HTMLInputElement).files?.[0] ?? null;
}
function reset() { engine.value?.camera.reset(); engine.value?.requestFrame(); }
function close() { cancel.value?.(); void engine.value?.closeScene(); }
</script>

<template>
  <section aria-label="3D Gaussian Splatting 查看器">
    <div role="group" aria-label="模型与视角控制">
      <label>打开PLY/SPZ <input type="file" accept=".ply,.spz" @change="selectFile" /></label>
      <button type="button" @click="cancel?.()">取消加载</button>
      <button type="button" :disabled="!engine" @click="engine?.fitScene()">适配视角</button>
      <button type="button" :disabled="!engine" @click="reset">重置</button>
      <button type="button" :disabled="!engine" @click="close">关闭场景</button>
    </div>
    <div ref="host" :style="{ height: `${props.height}px`, width: '100%', minHeight: '1px' }" />
    <p role="status" aria-live="polite">
      {{ snapshot?.phase ?? '初始化中' }} · {{ snapshot?.sceneCount.toLocaleString() ?? 0 }}点
      <span v-if="snapshot?.progress"> · {{ snapshot.progress.stage }}: {{ snapshot.progress.done }}</span>
    </p>
    <p v-if="failure" role="alert">{{ failure.code }}: {{ failure.diagnostic }}</p>
  </section>
</template>
```

资产base由Vite BASE_URL确定；子路径网站应配置正确base。上例只在客户端setup运行。如果使用Nuxt/SSR，将查看器放入ClientOnly或`.client.vue`组件；实际创建仅onMounted，setup中的window/location读取也必须放在客户端边界，或者使用静态绝对资产URL。ESM入口已验证可在Node导入，但这不表示以上含window的SFC能直接SSR。

父组件使用：

```vue
<script setup lang="ts">
import Native3DGSViewer from './Native3DGSViewer.vue';
</script>
<template>
  <Native3DGSViewer model-url="/models/site.spz" :height="640" />
</template>
```

URL/File变化由watch处理；computed保证无关统计更新不会重新创建来源。每次新open自动取消旧事务；onCleanup还显式cancel并阻止过期回调。不要在watch中await之后才注册onCleanup。Vue3.5的onWatcherCleanup也需要同步注册；本例使用回调参数兼容现有组合习惯。

## 3. options与响应式边界

options在mount时确定；props中动态改变assets/limits/pixelRatio不重建设备。要变这些配置，父组件改变key卸载重建。模型URL变化无需重建。容器需明确高度，CSS只改布局，ResizeObserver转换为物理像素；零尺寸时不提交帧，加载等待deadline。

不要用watchEffect每次读取snapshot后open模型，这会造成持续加载循环。不要手工给engine.value赋值，composable返回readonly ref。snapshot为冻结快照，订阅时整体替换，deep watch没有价值。错误diagnostic不参与控制逻辑，使用error.code。

多个视口独占device/Worker/显存，需宿主总预算。默认GPU/input/scene预算各8GiB、引擎CPU预算512MiB；预算不等于实际可用资源。大PLY/legacy SPZ使用OPFS backing和有界WASM批次，需要HTTPS/localhost及存储配额；SPZ v4保留有界内存解码。异步dispose会等待取消与磁盘清理。DPR提高会增加填充和截屏读回开销，详见SDK-guide资源预算章节。

## 4. 截图、恢复与自定义控制

```ts
async function screenshot() {
  const instance = engine.value;
  if (!instance) return;
  const result = await instance.capture();
  if (!result.ok) { loadError.value = result.error; return; }
  const { width, height, rgba } = result.value;
  const exportCanvas = document.createElement('canvas');
  exportCanvas.width = width; exportCanvas.height = height;
  exportCanvas.getContext('2d')!.putImageData(
    new ImageData(Uint8ClampedArray.from(rgba), width, height), 0, 0,
  );
  // 宿主按业务需求调用toBlob；不要在WebGPU canvas创建2d context
}
async function recoverDevice() {
  const result = await engine.value?.recover();
  if (result && !result.ok) loadError.value = result.error;
}
```

capture仅稳定非零视口可用；加载中、恢复中或超读回预算返回错误。设备丢失自动有界恢复，主动dispose不会触发恢复；Faulted可以显式recover。恢复期间新open返回DeviceLost；closeScene不会让恢复重新激活已关闭的模型。

相机同步setPose/orbit/pan/dolly/look/fly/reset对非法数值抛Error，调用时try/catch；异步引擎接口返回Result。通过camera.setPose支持业务方向/roll，轨道操作会重置roll。默认适配器仅鼠标/单指轨道、右键平移、滚轮，无完整pinch或键盘飞行绑定；业务自行监听时在卸载清理自己的监听器。

## 5. 路由与KeepAlive

普通路由卸载由composable释放资源；不可将已Stopped的实例保存后再复用。若Vue KeepAlive使组件只deactivated而未unmount，资源仍由该组件持有；业务应onDeactivated pause并取消当前加载，onActivated resume/requestFrame。若希望隐藏时释放显存，应取消KeepAlive缓存或显式销毁/重新挂载，pause本身不释放模型GPU资源。

对于自己追加的异步网络/导出工作，应使用组件alive标记或自己的AbortController。引擎cancel只取消模型事务，不取消宿主请求。watch cleanup与composable cleanup各管理自己的任务，不能省略其中一个。

## 6. 部署问题与验收

| 症状 | 核验 |
| --- | --- |
| 初始化失败 | HTTPS/localhost、实际adapter、策略限制、initializationError |
| Worker错误/空诊断 | 是否把SPA fallback HTML作为worker返回；workerUrl、CSP、同源 |
| 资源解码失败 | baseUrl、WASM MIME、模块/CORS/CSP、Worker/mjs/wasm版本 |
| 长期等待/Timeout | 容器实际宽高、隐藏标签页、deadline、网络 |
| ResourceLimit | 预算/旧场景峰值/WASM峰值，不要仅扩大gpuBytes |
| Vue不断重载 | source身份和watch依赖，是否把snapshot作为加载依赖 |
| KeepAlive占用显存 | deactivated不是unmount；按业务pause或真正销毁 |
| 偶发过期UI更新 | alive/token和onCleanup是否同步注册 |

独立tgz消费测试已验证Vue与React共存、真实WASM加载、卸载、Stopped和Canvas数量；仍应在宿主测试真实CSP、路由快速切换、零尺寸/恢复、失败/取消和浏览器硬件矩阵。详见[验收报告](verification/implementation-report.md)。


## 引擎类型与条件容器

engine.value 保持 WebEngine 类型，可传入接收 WebEngine 的公共工具函数。只读限制作用于 ref.value 的替换，实例方法仍然可调用。host 为 null 时不创建 GPU 实例；watcher 在 DOM 更新后处理新元素，每个绑定独立清理订阅、事件、Canvas 和迟到创建。options 为初始配置，运行期更改配置应重新挂载组件。


## 查看器导航与 Y 轴翻转（preview.2）

```ts
engine.camera.setFlipY(true); // 显示模型Y镜像，canonical pose不变
engine.camera.setMode('fly'); // 默认是orbit，框架适配器自动切换输入
engine.requestFrame();
```

SDK直接构造反射投影视图，不要再给Canvas添加CSS scaleX/scaleY。翻转状态跨fit/reset/open/recover保留，鼠标增量来自未变换client坐标。固定模式左拖旋转、右拖平移、滚轮缩放；自由模式点击视口进入Pointer Lock，鼠标转向、WASD/QE移动、Shift加速，Escape/失焦退出；拒绝Pointer Lock时支持左拖转向。方向键与加减键可在聚焦Canvas时操作。`getPose()`/`setPose()`始终使用未反射的canonical坐标。模板仅使用公开SDK、hook/composable、snapshot与Result，不读取renderer或active字段。
