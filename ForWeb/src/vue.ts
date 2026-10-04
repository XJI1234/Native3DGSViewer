import type { Ref } from 'vue';
import { markRaw, onBeforeUnmount, onMounted, shallowReadonly, shallowRef, watch } from 'vue';
import type { EngineOptions, Snapshot, WebEngine } from './engine/engine';
import { createEngine } from './index';
import { type EngineError, error } from './splat-types/index';
import { bindCanvas } from './web-adapters/dom';
export interface AdapterOptions extends Omit<EngineOptions, 'canvas'> {
    pixelRatio?: number;
}
export function useNative3DGS(host: Ref<HTMLElement | null>, options: AdapterOptions = {}) {
    const engine = shallowRef<WebEngine | null>(null),
        snapshot = shallowRef<Snapshot | null>(null),
        initializationError = shallowRef<EngineError | null>(null);
    let stop: (() => void) | undefined;
    onMounted(() => {
        stop = watch(
            host,
            (parent, _previous, onCleanup) => {
                engine.value = null;
                snapshot.value = null;
                initializationError.value = null;
                if (!parent) return;
                let alive = true;
                let instance: WebEngine | undefined;
                let cleanup: (() => void) | undefined;
                let unsubscribe: (() => void) | undefined;
                const canvas = document.createElement('canvas');
                canvas.style.cssText = 'width:100%;height:100%;display:block;touch-action:none';
                parent.append(canvas);
                onCleanup(() => {
                    alive = false;
                    cleanup?.();
                    unsubscribe?.();
                    canvas.remove();
                    engine.value = null;
                    snapshot.value = null;
                    void instance?.dispose();
                });
                void createEngine({ ...options, canvas }).then(async (result) => {
                    if (!result.ok) {
                        if (alive) initializationError.value = result.error;
                        canvas.remove();
                        return;
                    }
                    instance = result.value;
                    if (!alive) {
                        await instance.dispose();
                        return;
                    }
                    try {
                        cleanup = bindCanvas(instance, parent, canvas, options.pixelRatio);
                    } catch (reason) {
                        initializationError.value = error('InvalidInput', 'Adapter', reason);
                        canvas.remove();
                        await instance.dispose();
                        return;
                    }
                    engine.value = markRaw(instance);
                    snapshot.value = instance.getSnapshot();
                    unsubscribe = instance.subscribe(() => {
                        snapshot.value = instance!.getSnapshot();
                    });
                });
            },
            { immediate: true, flush: 'post' },
        );
    });
    onBeforeUnmount(() => {
        stop?.();
    });
    return {
        engine: shallowReadonly(engine),
        snapshot: shallowReadonly(snapshot),
        initializationError: shallowReadonly(initializationError),
    };
}
