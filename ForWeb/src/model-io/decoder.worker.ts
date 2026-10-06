import type { Limits, Progress, Scene, Source, Vec3 } from '../splat-types/index';
import { type DecoderInfo, type DecoderOptions, decoderPolicy } from './decoder-options';
import { decodeStream, download } from './streaming';
import { check, createDecoder, disposeDecoder, withBytes } from './wasm';

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
        decoder?: DecoderOptions;
    }>,
) => {
    const { source, limits, assets, pageBytes, retainedBytes, job, decoder = {} } = event.data;
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
        const initBegin = performance.now();
        let wasm = await createDecoder(assets);
        try {
            const prefix = new Uint8Array(
                await blob.slice(0, Math.min(blob.size, 1024 * 1024)).arrayBuffer(),
            );
            const probe = () =>
                withBytes(wasm, prefix, (p) => {
                    const admitted = wasm._gs_probe(p, prefix.length, blob.size, limits.sceneBytes);
                    if (!admitted)
                        throw Error(
                            `${wasm.UTF8ToString(wasm._gs_error())}; input=${blob.size}, prefix=${prefix.length}`,
                        );
                });
            probe();
            const ply = prefix[0] === 112 && prefix[1] === 108 && prefix[2] === 121;
            if (source.plyCoordinates !== undefined && !['rdf', 'rub'].includes(source.plyCoordinates))
                throw Error('InvalidInput: plyCoordinates');
            const streamingBytes = wasm._gs_count() * (128 + 24 * ((wasm._gs_degree() + 1) ** 2 - 1));
            const streaming =
                (ply && streamingBytes > 64 * 2 ** 20) || (prefix[0] === 0x1f && prefix[1] === 0x8b);
            let info: DecoderInfo = decoderPolicy(
                decoder,
                globalThis.crossOriginIsolated && typeof SharedArrayBuffer !== 'undefined',
                navigator.hardwareConcurrency,
                wasm._gs_count(),
                streaming,
                limits.cpuBytes - retainedBytes,
            );
            if (info.backend === 'pthreads') {
                const original = wasm;
                try {
                    wasm = await createDecoder(assets, info.threads);
                } catch (reason) {
                    if (decoder.mode === 'parallel') throw reason;
                    info = {
                        backend: 'single',
                        threads: 1,
                        fallbackReason: `Parallel initialization failed: ${String(reason).slice(0, 256)}`,
                    };
                }
                if (wasm !== original) {
                    disposeDecoder(original);
                    probe();
                }
            }
            let initializationMs = performance.now() - initBegin;
            if (streaming) {
                const runStream = () =>
                    decodeStream(
                        blob,
                        source,
                        wasm,
                        directory,
                        pageBytes,
                        limits,
                        retainedBytes,
                        progress,
                        begin,
                        info.threads,
                    );
                let decoded: Awaited<ReturnType<typeof decodeStream>>;
                try {
                    decoded = await runStream();
                } catch (reason) {
                    // Admission fails before inflate/backing creation; safe to retry the base runtime.
                    if (
                        decoder.mode === 'parallel' ||
                        info.backend !== 'pthreads' ||
                        !(reason instanceof Error) ||
                        reason.message !== 'ResourceLimit: streaming CPU peak'
                    )
                        throw reason;
                    const fallbackBegin = performance.now();
                    disposeDecoder(wasm);
                    wasm = await createDecoder(assets);
                    probe();
                    initializationMs += performance.now() - fallbackBegin;
                    info = {
                        backend: 'single',
                        threads: 1,
                        fallbackReason: 'Streaming CPU admission selects single-thread decoder',
                    };
                    decoded = await runStream();
                }
                const { scene, file } = decoded;
                disposeDecoder(wasm);
                scope.postMessage({
                    kind: 'ready',
                    scene: {
                        ...scene,
                        decoder: info,
                        decodeTimings: { ...scene.decodeTimings, initializationMs },
                    },
                    file,
                });
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
                decoder: info,
                decodeTimings: { initializationMs },
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
            disposeDecoder(wasm);
            scope.postMessage({ kind: 'ready', scene }, pages);
        } finally {
            disposeDecoder(wasm);
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
