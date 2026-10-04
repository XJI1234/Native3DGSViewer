import { expect, test, vi } from 'vitest';
import { decodeStream } from '../../src/model-io/streaming';
import type { Decoder } from '../../src/model-io/wasm';
import { defaultLimits } from '../../src/splat-types/index';

function fixture(shortView: boolean) {
    const access = {
        write: vi.fn((bytes: Uint8Array) => bytes.length),
        read: vi.fn(() => {
            throw Error('Unexpected backing read');
        }),
        truncate: vi.fn(),
        close: vi.fn(),
        flush: vi.fn(),
        getSize: () => 3583,
    };
    const directory = {
        getFileHandle: async () => ({ createSyncAccessHandle: async () => access }),
    } as unknown as FileSystemDirectoryHandle;
    const wasm = {
        HEAPU8: new Uint8Array(8192),
        _gs_count: () => 64,
        _gs_degree: () => 0,
        _gs_begin_batch: () => 1,
        _gs_release: vi.fn(),
        _gs_meta: (index: number) => (index === 11 ? 56 : 1),
        _malloc: () => 64,
        _free: vi.fn(),
        _gs_chunk: () => 1,
        _gs_pack_tiled: () => (shortView ? 8192 : 1024),
        _gs_finish_batch: () => 1,
    } as unknown as Decoder;
    const blob = new Blob(['p', new Uint8Array(64 * 56)]);
    return {
        access,
        wasm,
        run: () =>
            decodeStream(
                blob,
                { kind: 'blob', blob },
                wasm,
                directory,
                128 * 2 ** 20,
                defaultLimits,
                0,
                () => {},
                0,
            ),
    };
}

test('streaming rejects a short WASM packing view before writing or publishing', async () => {
    const { run, access } = fixture(true);
    await expect(run()).rejects.toThrow('pack view length');
    expect(access.write).not.toHaveBeenCalled();
    expect(access.close).toHaveBeenCalledOnce();
});

test('streaming rejects a truncated backing before rebase and closes its handle', async () => {
    const { run, access } = fixture(false);
    await expect(run()).rejects.toThrow('packed size before rebase 3583/3584');
    expect(access.read).not.toHaveBeenCalled();
    expect(access.close).toHaveBeenCalledOnce();
});
