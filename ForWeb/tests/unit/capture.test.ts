import { expect, test, vi } from 'vitest';
import { type GpuScene, Renderer } from '../../src/render-core/renderer';

test('concurrent captures reserve shared budget and release it on completion or allocation failure', async () => {
    vi.stubGlobal('GPUBufferUsage', { COPY_DST: 1, MAP_READ: 2 });
    vi.stubGlobal('GPUMapMode', { READ: 1 });
    let complete!: () => void;
    const wait = new Promise<void>((resolve) => {
        complete = resolve;
    });
    const make = vi.fn(() => ({
        mapAsync: () => wait,
        getMappedRange: () => new ArrayBuffer(256),
        unmap: vi.fn(),
        destroy: vi.fn(),
    }));
    const renderer = Object.assign(Object.create(Renderer.prototype) as Renderer, {
        canvas: { width: 8, height: 1 },
        budget: 1900,
        baseBytes: 384,
        captureBytes: 0,
        residentBytes: 0,
        device: { createBuffer: make },
        format: 'rgba8unorm',
        render: vi.fn(),
    });
    const scene = { bytes: 1024 } as GpuScene;
    const first = renderer.capture(scene, new ArrayBuffer(0), 0);
    await expect(renderer.capture(scene, new ArrayBuffer(0), 0)).rejects.toThrow('ResourceLimit');
    expect(make).toHaveBeenCalledOnce();
    complete();
    expect((await first).length).toBe(32);
    make.mockImplementationOnce(() => {
        throw Error('allocation failure');
    });
    await expect(renderer.capture(scene, new ArrayBuffer(0), 0)).rejects.toThrow('allocation failure');
    expect((await renderer.capture(scene, new ArrayBuffer(0), 0)).length).toBe(32);
    vi.unstubAllGlobals();
});
