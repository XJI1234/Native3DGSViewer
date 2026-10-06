import type { RefObject } from 'react';
import { useEffect, useRef, useState, useSyncExternalStore } from 'react';
import type { EngineOptions, Snapshot, WebEngine } from './engine/engine';
import { createEngine } from './index';
import { type EngineError, error } from './splat-types/index';
import { bindCanvas } from './web-adapters/dom';
export interface AdapterOptions extends Omit<EngineOptions, 'canvas'> {
    pixelRatio?: number;
}
const emptySnapshot: Snapshot = Object.freeze({
    decoder: null,
    phase: 'Idle',
    requestId: 0,
    sceneCount: 0,
    degree: 0,
    source: null,
    progress: null,
    error: null,
    stats: null,
    deviceGeneration: 0,
    viewportRevision: 0,
});
const noSubscribe = () => () => {};
export function useNative3DGS(
    host: RefObject<HTMLElement | null> | HTMLElement | null,
    options: AdapterOptions = {},
) {
    const initial = useRef(options);
    const [engine, setEngine] = useState<WebEngine | null>(null);
    const [initializationError, setError] = useState<EngineError | null>(null);
    useEffect(() => {
        const parent = host && 'current' in host ? host.current : host;
        setError(null);
        if (!parent) return;
        let alive = true;
        let instance: WebEngine | undefined;
        let unbind: (() => void) | undefined;
        // Each effect owns its Canvas; StrictMode cleanup cannot steal the next instance's surface.
        const canvas = document.createElement('canvas');
        canvas.style.cssText = 'width:100%;height:100%;display:block;touch-action:none';
        parent.append(canvas);
        void createEngine({ ...initial.current, canvas }).then((result) => {
            if (!result.ok) {
                if (alive) setError(result.error);
                canvas.remove();
                return;
            }
            instance = result.value;
            if (!alive) {
                void instance.dispose();
                return;
            }
            try {
                unbind = bindCanvas(instance, parent, canvas, initial.current.pixelRatio);
                setEngine(instance);
            } catch (reason) {
                setError(error('InvalidInput', 'Adapter', reason));
                canvas.remove();
                void instance.dispose();
            }
        });
        return () => {
            alive = false;
            unbind?.();
            setEngine(null);
            canvas.remove();
            void instance?.dispose();
        };
    }, [host]);
    const snapshot = useSyncExternalStore(
        engine?.subscribe ?? noSubscribe,
        engine?.getSnapshot ?? (() => emptySnapshot),
        () => emptySnapshot,
    );
    return { engine, snapshot, initializationError };
}
