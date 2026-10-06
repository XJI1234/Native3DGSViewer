<script setup lang="ts">
import { computed, onBeforeUnmount, ref, shallowRef, watch } from "vue";
import { useNative3DGS } from "@native3dgs/web/vue";
import { ViewerSession, number, status, diagnostic, time } from "./viewer";
import "./style.css";
const host = ref<HTMLElement | null>(null),
  file = ref<HTMLInputElement | null>(null);
const options = {
  decoder: { mode: "auto" as const, threads: 4 },
  sorting: { mode: "adaptive" as const },
  assets: {
    baseUrl: new URL(`${import.meta.env.BASE_URL}gs-assets/`, location.href),
    workerUrl: new URL(
      `${import.meta.env.BASE_URL}gs-assets/decoder.worker.js`,
      location.href,
    ),
  },
};
const { engine, snapshot, initializationError } = useNative3DGS(host, options);
const session = shallowRef<ViewerSession | null>(null);
const ui = shallowRef({
  name: "",
  candidate: "",
  message: "",
  flipY: false,
  mode: "orbit" as "orbit" | "fly",
  capturing: false,
  closing: false,
  position: "未提供",
});
const url = ref(""),
  panel = ref(true);
const stop = watch(
  engine,
  (instance, _old, onCleanup) => {
    session.value = null;
    if (!instance) return;
    const viewer = new ViewerSession(instance);
    session.value = viewer;
    ui.value = viewer.getSnapshot();
    const unsubscribe = viewer.subscribe(() => {
      ui.value = viewer.getSnapshot();
    });
    onCleanup(() => {
      unsubscribe();
      viewer.dispose();
    });
  },
  { immediate: true },
);
onBeforeUnmount(stop);
const busy = computed(() =>
  ["Loading", "Uploading", "Recovering"].includes(
    snapshot.value?.phase ?? "Idle",
  ),
);
const scene = computed(() => (snapshot.value?.sceneCount ?? 0) > 0);
const ready = computed(
  () =>
    !!session.value &&
    !ui.value.closing &&
    !["Faulted", "Recovering", "Stopping", "Stopped"].includes(
      snapshot.value?.phase ?? "Idle",
    ),
);
const message = computed(() =>
  diagnostic(
    initializationError.value,
    snapshot.value?.error ?? null,
    snapshot.value?.phase ?? "Idle",
    ui.value.message,
  ),
);
const progress = computed(() => snapshot.value?.progress);
const pick = () => file.value?.click();
function selectFile(event: Event) {
  const input = event.target as HTMLInputElement;
  const model = input.files?.[0];
  if (model) session.value?.openFile(model);
  input.value = "";
}
function drop(event: DragEvent) {
  const model = event.dataTransfer?.files[0];
  if (model && ready.value) session.value?.openFile(model);
}
</script>
<template>
  <div class="shell">
    <header class="header">
      <div class="brand">
        <h1>Native3DGS 查看器</h1>
        <span class="framework">Vue 模板</span>
      </div>
      <button
        @click="panel = !panel"
        :aria-expanded="panel"
        aria-controls="inspector"
      >
        {{ panel ? "隐藏信息" : "显示信息" }}
      </button>
    </header>
    <nav class="toolbar" aria-label="查看器工具">
      <button class="primary" :disabled="!ready" @click="pick">打开模型</button>
      <input
        ref="file"
        class="sr-only"
        aria-label="选择模型文件"
        type="file"
        accept=".ply,.spz"
        tabindex="-1"
        @change="selectFile"
      />
      <button
        :disabled="!busy || snapshot?.phase === 'Recovering'"
        @click="session?.cancel()"
      >
        取消加载</button
      ><button :disabled="!scene || ui.closing" @click="session?.close()">
        关闭模型
      </button>
      <span class="separator" />
      <div class="mode" role="group" aria-label="导航模式">
        <button
          :disabled="!ready"
          :aria-pressed="ui.mode === 'orbit'"
          @click="session?.setMode('orbit')"
        >
          固定</button
        ><button
          :disabled="!ready"
          :aria-pressed="ui.mode === 'fly'"
          @click="session?.setMode('fly')"
        >
          自由
        </button>
      </div>
      <button :disabled="!ready || !scene" @click="session?.fit()">适配</button
      ><button :disabled="!ready || !scene" @click="session?.reset()">
        重置
      </button>
      <button
        :disabled="!ready"
        :aria-pressed="ui.flipY"
        @click="session?.flip()"
      >
        翻转 Y
      </button>
      <button
        :disabled="!ready || !scene"
        aria-label="放大模型"
        @click="session?.zoom(-1)"
      >
        放大</button
      ><button
        :disabled="!ready || !scene"
        aria-label="缩小模型"
        @click="session?.zoom(1)"
      >
        缩小
      </button>
      <button
        :disabled="!ready || !scene || busy || ui.capturing"
        @click="session?.capture()"
      >
        {{ ui.capturing ? "正在截图" : "保存截图" }}
      </button>
    </nav>
    <main :class="['workspace', { compact: !panel }]">
      <div class="viewport" @dragover.prevent @drop.prevent="drop">
        <div ref="host" class="host" />
        <div v-if="!scene" class="empty">
          <h2>{{ busy ? "正在准备模型" : "打开一个三维场景" }}</h2>
          <p>
            {{
              busy
                ? "加载进度显示在信息面板。"
                : "选择本地 PLY 或 SPZ 文件，也可以将文件拖入视口。"
            }}
          </p>
          <button v-if="!busy" :disabled="!ready" @click="pick">
            选择模型文件
          </button>
        </div>
        <p v-if="scene" class="scene-caption">
          {{ ui.name || snapshot?.source }} ·
          {{ ui.flipY ? "Y 轴已翻转" : "原始方向" }}
        </p>
      </div>
      <aside class="panel" id="inspector" :hidden="!panel">
        <section>
          <h2>场景信息</h2>
          <dl class="metrics">
            <dt>状态</dt>
            <dd role="status">
              {{
                status(
                  snapshot?.phase ?? "Idle",
                  !!engine,
                  !!initializationError,
                )
              }}
            </dd>
            <dt>高斯点数</dt>
            <dd data-testid="count">{{ number(snapshot?.sceneCount ?? 0) }}</dd>
            <dt>球谐阶数</dt>
            <dd>SH {{ snapshot?.degree ?? 0 }}</dd>
            <dt>显示方向</dt>
            <dd>{{ ui.flipY ? "Y 轴镜像" : "未翻转" }}</dd>
          </dl>
          <div v-if="progress" class="load-status">
            <p>{{ ui.candidate || "候选模型" }} · {{ progress.stage }}</p>
            <progress
              aria-label="模型加载进度"
              :max="progress.total ?? 1"
              :value="progress.total ? progress.done : undefined"
            />
            <p>
              {{
                progress.total
                  ? `${Math.min(100, (100 * progress.done) / progress.total).toFixed(0)}%`
                  : "正在处理"
              }}
            </p>
          </div>
        </section>
        <section>
          <h3>渲染诊断</h3>
          <dl class="metrics">
            <dt>CPU 提交</dt>
            <dd>{{ time(snapshot?.stats?.cpuMs) }}</dd>
            <dt>GPU 总帧</dt>
            <dd>{{ time(snapshot?.stats?.gpuMs) }}</dd>
            <dt>投影 / 排序</dt>
            <dd>
              {{ time(snapshot?.stats?.projectionMs) }} /
              {{ time(snapshot?.stats?.sortMs) }}
            </dd>
            <dt>绘制</dt>
            <dd>{{ time(snapshot?.stats?.drawMs) }}</dd>
            <dt>解码后端</dt>
            <dd data-testid="decoder">
              {{
                snapshot?.decoder
                  ? `${snapshot.decoder.backend} · ${snapshot.decoder.threads}`
                  : "未加载"
              }}
            </dd>
            <dt>排序策略</dt>
            <dd data-testid="sorting">
              adaptive · {{ snapshot?.stats?.sortReason ?? "尚未提交" }}
            </dd>
            <dt>排序年龄</dt>
            <dd>{{ time(snapshot?.stats?.sortAgeMs) }}</dd>
            <dt>设备代际</dt>
            <dd>{{ snapshot?.deviceGeneration ?? 0 }}</dd>
          </dl>
          <p class="load-status">相机位置（未镜像坐标）</p>
          <output aria-label="相机位置">{{ ui.position }}</output>
        </section>
        <section>
          <form @submit.prevent="session?.openUrl(url)">
            <label for="model-url">远程模型地址</label
            ><input
              id="model-url"
              type="url"
              v-model="url"
              placeholder="https://example.com/model.spz"
              required
            /><button :disabled="!ready">加载地址</button>
          </form>
        </section>
        <section>
          <h3>操作方式</h3>
          <p>
            {{
              ui.mode === "orbit"
                ? "左键旋转，右键平移，滚轮缩放。"
                : "点击视口后鼠标转向，WASD 移动，Q/E 升降，Shift 加速，Esc 退出。未锁定时可左键拖动转向。"
            }}
          </p>
          <p class="load-status">
            方向键旋转，加减键缩放。Y 翻转后操作方向保持一致。
          </p>
        </section>
        <section v-if="snapshot?.phase === 'Faulted'">
          <button @click="session?.recover()">恢复设备</button>
        </section>
      </aside>
    </main>
    <div v-if="message" class="alert" role="alert">{{ message }}</div>
    <footer class="footer">
      <span class="model-name">{{ ui.name || "尚未打开模型" }}</span
      ><span>本地模型在浏览器中处理 · WebGPU</span>
    </footer>
  </div>
</template>
