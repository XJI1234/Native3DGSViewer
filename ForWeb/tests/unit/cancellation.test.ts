import { expect, test, vi } from 'vitest';
import { Camera } from '../../src/engine/camera';
import { Renderer } from '../../src/render-core/renderer';
import { defaultLimits, type Scene, validateLimits } from '../../src/splat-types/index';

test('aborting a stalled backing read releases upload reservations and never writes late data', async () => {
    vi.stubGlobal('GPUBufferUsage', { STORAGE: 1, COPY_DST: 2 });
    let finish!: (data: ArrayBuffer) => void;
    const read = new Promise<ArrayBuffer>((resolve) => {
        finish = resolve;
    });
    const buffer = { destroy: vi.fn() };
    const queue = { writeBuffer: vi.fn(), onSubmittedWorkDone: async () => {} };
    const renderer = Object.assign(Object.create(Renderer.prototype), {
        device: {
            limits: { maxStorageBufferBindingSize: 2 ** 30 },
            queue,
            createBuffer: () => buffer,
            pushErrorScope: vi.fn(),
            popErrorScope: async () => null,
        },
        budget: 2 ** 30,
        residentBytes: 0,
        cpuResidentBytes: 0,
        captureBytes: 0,
        baseBytes: 0,
    }) as Renderer;
    const backing = {
        totalBytes: 64,
        residentBytes: 0,
        read: () => read,
        retain: vi.fn(),
        release: vi.fn(async () => {}),
    };
    const scene = {
        count: 1,
        degree: 0,
        stride: 64,
        pageCapacity: 1,
        pages: [],
        backing,
    } as unknown as Scene;
    const controller = new AbortController();
    const upload = renderer.upload(scene, 0, controller.signal, () => {});
    controller.abort();
    await expect(upload).rejects.toThrow('Cancelled');
    expect(buffer.destroy).toHaveBeenCalledOnce();
    expect(backing.release).toHaveBeenCalledOnce();
    finish(new ArrayBuffer(64));
    await Promise.resolve();
    expect(queue.writeBuffer).not.toHaveBeenCalled();
    expect(renderer.retainedBytes).toBe(0);
    vi.unstubAllGlobals();
});

test('overflowing finite camera inputs preserve the previous pose and revision', () => {
    const camera = new Camera(),
        pose = camera.getPose(),
        revision = camera.revision;
    expect(() =>
        camera.setPose({
            position: [Number.MAX_VALUE, 0, 0],
            target: [-Number.MAX_VALUE, 0, 0],
            up: [0, 1, 0],
        }),
    ).toThrow();
    expect(camera.getPose()).toEqual(pose);
    expect(camera.revision).toBe(revision);
    expect(() => validateLimits({ ...defaultLimits, timeoutMs: 2147483648 })).toThrow();
});

test('completed and cancelled presentation waits remove device loss subscribers', async () => {
    const listeners = new Set(),
        queue = { onSubmittedWorkDone: async () => {} };
    const renderer = Object.assign(Object.create(Renderer.prototype), {
        lossObservers: listeners,
        device: { queue },
    }) as Renderer;
    for (let i = 0; i < 100; i++) await renderer.waitForWork(new AbortController().signal);
    expect(listeners.size).toBe(0);
    queue.onSubmittedWorkDone = () => new Promise(() => {});
    const controller = new AbortController(),
        wait = renderer.waitForWork(controller.signal);
    expect(listeners.size).toBe(1);
    controller.abort();
    await expect(wait).rejects.toThrow('Cancelled');
    expect(listeners.size).toBe(0);
    const lost = renderer.waitForWork(new AbortController().signal);
    const listener = [...listeners][0] as (info: { message: string }) => void;
    listener({ message: 'fixture loss' });
    await expect(lost).rejects.toThrow('DeviceLost');
    expect(listeners.size).toBe(0);
});
