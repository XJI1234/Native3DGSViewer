import { afterEach, describe, expect, it, vi } from 'vitest';
import { Camera } from '../../src/engine/camera';
import type { WebEngine } from '../../src/engine/engine';
import { bindCanvas } from '../../src/web-adapters/dom';

afterEach(() => {
    vi.unstubAllGlobals();
    vi.restoreAllMocks();
});

describe('DOM navigation lifetime', () => {
    it('keeps fly mouse lock and guards focus loss and late acquisition', async () => {
        vi.spyOn(performance, 'now').mockReturnValue(0);
        const callbacks = new Map<number, FrameRequestCallback>();
        let id = 0;
        vi.stubGlobal('requestAnimationFrame', (cb: FrameRequestCallback) => {
            callbacks.set(++id, cb);
            return id;
        });
        vi.stubGlobal('cancelAnimationFrame', (key: number) => callbacks.delete(key));
        const win = new EventTarget();
        const doc = Object.assign(new EventTarget(), {
            hidden: false,
            activeElement: null as unknown,
            pointerLockElement: null as unknown,
            exitPointerLock: vi.fn(() => {
                doc.pointerLockElement = null;
            }),
        });
        vi.stubGlobal('window', win);
        vi.stubGlobal('document', doc);
        vi.stubGlobal(
            'ResizeObserver',
            class {
                observe() {}
                disconnect() {}
            },
        );
        const canvas = Object.assign(new EventTarget(), {
            tabIndex: -1,
            setAttribute: vi.fn(),
            setPointerCapture: vi.fn(),
            hasPointerCapture: () => false,
            releasePointerCapture: vi.fn(),
            focus: () => {
                doc.activeElement = canvas;
            },
            requestPointerLock: () => {
                doc.pointerLockElement = canvas;
                doc.dispatchEvent(new Event('pointerlockchange'));
                return Promise.resolve();
            },
        });
        const camera = new Camera();
        camera.setMode('fly');
        const engine = {
            camera,
            maxViewportDimension: 4096,
            getSnapshot: () => ({ phase: 'Ready' }),
            resize: vi.fn(),
            requestFrame: vi.fn(),
        } as unknown as WebEngine;
        const host = { getBoundingClientRect: () => ({ width: 800, height: 600 }) } as HTMLElement;
        const unbind = bindCanvas(engine, host, canvas as unknown as HTMLCanvasElement, 1);
        const fire = (target: EventTarget, type: string, fields: object) =>
            target.dispatchEvent(Object.assign(new Event(type, { cancelable: true }), fields));
        let time = 0;
        const step = (advance = 16) => {
            time += advance;
            const batch = [...callbacks.values()];
            callbacks.clear();
            batch.forEach((cb) => {
                cb(time);
            });
        };
        fire(canvas, 'pointerdown', { pointerId: 1, button: 0, clientX: 100, clientY: 100 });
        expect(doc.pointerLockElement).toBe(canvas);
        fire(win, 'keydown', { code: 'KeyW' });
        step();
        fire(win, 'keyup', { code: 'KeyW' });
        step();
        expect(doc.pointerLockElement).toBe(canvas);
        expect(doc.exitPointerLock).not.toHaveBeenCalled();
        const fly = vi.spyOn(camera, 'fly');
        step(1000);
        fire(win, 'keydown', { code: 'KeyW' });
        step();
        expect(fly.mock.lastCall?.[3]).toBeCloseTo(0.016);
        fire(win, 'keyup', { code: 'KeyW' });
        camera.setMode('orbit');
        step();
        expect(doc.pointerLockElement).toBeNull();
        camera.setMode('fly');
        fire(canvas, 'pointerup', { pointerId: 1 });
        fire(canvas, 'pointerdown', { pointerId: 2, button: 0, clientX: 100, clientY: 100 });
        win.dispatchEvent(new Event('blur'));
        expect(doc.pointerLockElement).toBeNull();
        expect(callbacks.size).toBe(0);
        fire(canvas, 'pointerup', { pointerId: 2 });
        doc.activeElement = canvas;
        fire(win, 'keydown', { code: 'KeyW' });
        doc.activeElement = {};
        canvas.dispatchEvent(new Event('blur'));
        expect(callbacks.size).toBe(0);
        let completeLock!: () => void;
        canvas.requestPointerLock = () =>
            new Promise<void>((resolve) => {
                completeLock = resolve;
            });
        fire(canvas, 'pointerdown', { pointerId: 3, button: 0, clientX: 100, clientY: 100 });
        unbind();
        doc.pointerLockElement = canvas;
        doc.dispatchEvent(new Event('pointerlockchange'));
        completeLock();
        await Promise.resolve();
        expect(doc.pointerLockElement).toBeNull();
        expect(callbacks.size).toBe(0);
    });
});
