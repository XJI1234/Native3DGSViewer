import type { Limits, Progress, Scene, Source, Vec3 } from '../splat-types/index';
import { decodeStream, download } from './streaming';
import { check, createDecoder, withBytes } from './wasm';

const scope = globalThis as unknown as {
    onmessage: ((e: MessageEvent) => void) | null;
    postMessage: (data: unknown, transfer?: Transferable[]) => void;
};
scope.onmessage = async (
    event: MessageEvent<{
        source: Source;
        limits: Limits;
        assets: string;
        pageBytes: number;
        retainedBytes: number;
        job: string;
    }>,
) => {
    const { source, limits, assets, pageBytes, retainedBytes, job } = event.data;
    const begin = performance.now();
    const progress = (stage: string, done: number, total: number | null) =>
        scope.postMessage({ kind: 'progress', value: { stage, done, total } satisfies Progress });
    try {
        if (!navigator.storage?.getDirectory) throw Error('UnsupportedCapability: OPFS storage');
        if (retainedBytes + 40 * 2 ** 20 > limits.cpuBytes) throw Error('ResourceLimit: input CPU peak');
        const root = await navigator.storage.getDirectory();
        const directory = await root.getDirectoryHandle(job, { create: true });
        const blob = await download(source, directory, limits, progress);
        if (blob.size > limits.inputBytes) throw Error('ResourceLimit: input bytes');
        const wasm = await createDecoder(assets);
        try {
            const prefix = new Uint8Array(
                await blob.slice(0, Math.min(blob.size, 1024 * 1024)).arrayBuffer(),
            );
            withBytes(wasm, prefix, (p) => {
                const admitted = wasm._gs_probe(p, prefix.length, blob.size, limits.sceneBytes);
                if (!admitted)
                    throw Error(
                        `${wasm.UTF8ToString(wasm._gs_error())}; input=${blob.size}, prefix=${prefix.length}`,
                    );
            });
            const ply = prefix[0] === 112 && prefix[1] === 108 && prefix[2] === 121;
            if (source.plyCoordinates !== undefined && !['rdf', 'rub'].includes(source.plyCoordinates))
                throw Error('InvalidInput: plyCoordinates');
            const streamingBytes = wasm._gs_count() * (128 + 24 * ((wasm._gs_degree() + 1) ** 2 - 1));
            if ((ply && streamingBytes > 64 * 2 ** 20) || (prefix[0] === 0x1f && prefix[1] === 0x8b)) {
                const { scene, file } = await decodeStream(
                    blob,
                    source,
                    wasm,
                    directory,
                    pageBytes,
                    limits,
                    retainedBytes,
                    progress,
                    begin,
                );
                scope.postMessage({ kind: 'ready', scene, file });
                return;
            }
            const points = wasm._gs_count(),
                sh = wasm._gs_degree();
            const packedStride = [64, 112, 160, 256][sh];
            if (packedStride === undefined || !points) throw Error('UnsupportedFormat: count/SH');
            const normalized = points * (64 + 12 * ((sh + 1) ** 2 - 1)) + 256;
            const packed = points * packedStride,
                groups = Math.ceil(points / 256);
            const gpuPeak =
                packed +
                points * 64 +
                Math.max(64, groups * 64) * 2 +
                64 * (Math.ceil(groups / 256) + 1) +
                560;
            const temporary = ply ? 4 * 2 ** 20 : blob.size + normalized * 2;
            const wasmPeak = normalized + Math.max(temporary, Math.min(pageBytes, packed)) + 32 * 2 ** 20;
            const cpuPeak = retainedBytes + blob.size * 2 + wasmPeak + packed;
            if (
                points > 4 * Math.floor(pageBytes / packedStride) ||
                points * 48 > pageBytes ||
                groups > 65535 ||
                gpuPeak > limits.gpuBytes ||
                wasmPeak > 960 * 2 ** 20 ||
                cpuPeak > limits.cpuBytes
            )
                throw Error('ResourceLimit: estimated CPU/WASM/GPU peak');
            check(wasm, wasm._gs_begin(source.plyCoordinates === 'rub' ? 0 : 1));
            if (ply) {
                const offset = wasm._gs_meta(10),
                    stride = wasm._gs_meta(11),
                    count = wasm._gs_count();
                const batch = Math.max(1, Math.floor((4 * 2 ** 20) / stride));
                for (let start = 0; start < count; start += batch) {
                    const points = Math.min(batch, count - start);
                    const bytes = new Uint8Array(
                        await blob
                            .slice(offset + start * stride, offset + (start + points) * stride)
                            .arrayBuffer(),
                    );
                    withBytes(wasm, bytes, (p) => check(wasm, wasm._gs_chunk(p, bytes.length)));
                    progress('Decoding', start + points, count);
                }
            } else {
                progress('Decoding', 0, null);
                const bytes = new Uint8Array(await blob.arrayBuffer());
                withBytes(wasm, bytes, (p) => check(wasm, wasm._gs_spz(p, bytes.length)));
            }
            check(wasm, wasm._gs_finish(ply ? 0 : 1));
            const count = wasm._gs_count(),
                degree = wasm._gs_degree(),
                stride = wasm._gs_stride();
            const pageCapacity = Math.floor(pageBytes / stride);
            if (pageCapacity < 1 || count > 4 * pageCapacity)
                throw Error('ResourceLimit: four-page GPU binding capacity');
            const origin: [number, number, number] = [wasm._gs_meta(0), wasm._gs_meta(1), wasm._gs_meta(2)];
            const min = Array.from({ length: 3 }, (_, i) => wasm._gs_meta(3 + i)) as unknown as Vec3;
            const max = Array.from({ length: 3 }, (_, i) => wasm._gs_meta(6 + i)) as unknown as Vec3;
            const pages: ArrayBuffer[] = [];
            for (let start = 0; start < count; start += pageCapacity) {
                const n = Math.min(pageCapacity, count - start);
                const ptr = wasm._gs_pack(start, n);
                if (!ptr) throw Error('OutOfMemory: pack');
                try {
                    pages.push(wasm.HEAPU8.slice(ptr, ptr + n * stride).buffer);
                } finally {
                    wasm._free(ptr);
                }
                progress('Packing', Math.min(count, start + n), count);
            }
            const scene: Scene = {
                count,
                degree,
                stride,
                pageCapacity,
                pages,
                origin,
                min,
                max,
                maxScale: wasm._gs_meta(9),
                source: source.kind === 'blob' ? (source.name ?? 'Blob') : 'URL',
                decodeMs: performance.now() - begin,
            };
            scope.postMessage({ kind: 'ready', scene }, pages);
        } finally {
            wasm._gs_release();
        }
    } catch (reason) {
        scope.postMessage({
            kind: 'error',
            reason:
                reason instanceof DOMException && reason.name === 'QuotaExceededError'
                    ? 'ResourceLimit: browser storage quota'
                    : reason instanceof Error
                      ? reason.message
                      : String(reason),
        });
    }
};
