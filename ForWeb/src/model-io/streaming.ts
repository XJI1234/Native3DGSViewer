import type { Limits, Scene, Source, Vec3 } from '../splat-types/index';
import { check, type Decoder, withBytes } from './wasm';

interface SyncFile {
    write(data: ArrayBufferView, options: { at: number }): number;
    read(data: ArrayBufferView, options: { at: number }): number;
    truncate(size: number): void;
    flush(): void;
    close(): void;
    getSize(): number;
}
async function openFile(directory: FileSystemDirectoryHandle, name: string) {
    const file = await directory.getFileHandle(name, { create: true });
    if (!('createSyncAccessHandle' in file))
        throw Error('UnsupportedCapability: synchronous OPFS worker access');
    const access = await (
        file as unknown as { createSyncAccessHandle(): Promise<SyncFile> }
    ).createSyncAccessHandle();
    return { file, access };
}
function write(access: SyncFile, data: Uint8Array, at: number) {
    let offset = 0;
    while (offset < data.length) {
        const n = access.write(data.subarray(offset), { at: at + offset });
        if (!n) throw Error('ResourceLimit: storage write stalled');
        offset += n;
    }
}
const chunkBytes = 4 * 2 ** 20;
type Report = (stage: string, done: number, total: number | null) => void;

export async function download(
    source: Source,
    directory: FileSystemDirectoryHandle,
    limits: Limits,
    report: Report,
): Promise<Blob> {
    if (source.kind === 'blob') {
        if (!(source.blob instanceof Blob)) throw Error('InvalidInput: Blob');
        return source.blob;
    }
    if (source.kind !== 'url') throw Error('InvalidInput: source kind');
    let url: URL;
    try {
        url = new URL(source.url);
    } catch {
        throw Error('InvalidInput: absolute URL required');
    }
    if (!['http:', 'https:'].includes(url.protocol)) throw Error('InvalidInput: URL protocol');
    const response = await fetch(url).catch((e) => {
        throw Error(`NetworkFailure: ${e}`);
    });
    if (!response.ok || !response.body) throw Error(`NetworkFailure: HTTP ${response.status}`);
    // Fetch exposes decoded bytes; Content-Length describes the encoded HTTP body.
    const encoded = response.headers.get('Content-Encoding');
    const declared =
        response.type === 'cors' || (encoded && encoded !== 'identity')
            ? 0
            : Number(response.headers.get('Content-Length'));
    if (declared > limits.inputBytes) {
        await response.body.cancel();
        throw Error('ResourceLimit: input bytes');
    }
    const input = await openFile(directory, 'input');
    const reader = response.body.getReader();
    let total = 0;
    try {
        input.access.truncate(0);
        while (true) {
            const { done, value } = await reader.read().catch((e) => {
                throw Error(`NetworkFailure: ${e}`);
            });
            if (done) break;
            if (total + value.length > limits.inputBytes) throw Error('ResourceLimit: input bytes');
            write(input.access, value, total);
            total += value.length;
            report('Downloading', total, declared || null);
        }
        if (declared && declared !== total) throw Error('NetworkFailure: truncated body');
        input.access.flush();
    } finally {
        await reader.cancel().catch(() => {});
        input.access.close();
    }
    const file = await input.file.getFile();
    if (file.size !== total)
        throw Error(`ResourceLimit: browser input snapshot length ${file.size}/${total}`);
    return file;
}

async function inflate(
    blob: Blob,
    directory: FileSystemDirectoryHandle,
    wasm: Decoder,
    expectedMax: number,
    report: Report,
): Promise<File> {
    const raw = await openFile(directory, 'inflated');
    const out = wasm._malloc(chunkBytes);
    if (!out) {
        raw.access.close();
        throw Error('OutOfMemory');
    }
    let total = 0,
        ended = false;
    try {
        if (!wasm._gs_inflate_begin()) throw Error('DecoderFailure: inflate init');
        raw.access.truncate(0);
        for (let at = 0; at < blob.size; at += chunkBytes) {
            const bytes = new Uint8Array(await blob.slice(at, at + chunkBytes).arrayBuffer());
            withBytes(wasm, bytes, (p) => {
                let used = 0,
                    produced = 0;
                do {
                    const status = wasm._gs_inflate_step(p + used, bytes.length - used, out, chunkBytes);
                    if (status === 3) break;
                    used += wasm._gs_inflate_consumed();
                    produced = wasm._gs_inflate_produced();
                    if (!status || total + produced > expectedMax)
                        throw Error('DecoderFailure: SPZ gzip checksum/length');
                    write(raw.access, wasm.HEAPU8.subarray(out, out + produced), total);
                    total += produced;
                    if (status === 2) {
                        if (used !== bytes.length || at + bytes.length !== blob.size)
                            throw Error('DecoderFailure: SPZ trailing compressed data');
                        ended = true;
                        break;
                    }
                } while (used < bytes.length || produced === chunkBytes);
            });
            report('Inflating', at + bytes.length, blob.size);
        }
        if (!ended) throw Error('DecoderFailure: truncated SPZ gzip');
        raw.access.flush();
    } finally {
        wasm._gs_inflate_end();
        wasm._free(out);
        raw.access.close();
    }
    const file = await raw.file.getFile();
    if (file.size !== total)
        throw Error(`ResourceLimit: browser inflated snapshot length ${file.size}/${total}`);
    return file;
}

export async function decodeStream(
    blob: Blob,
    source: Source,
    wasm: Decoder,
    directory: FileSystemDirectoryHandle,
    pageBytes: number,
    limits: Limits,
    retainedBytes: number,
    report: Report,
    begin: number,
): Promise<{ scene: Scene; file: File }> {
    const count = wasm._gs_count(),
        degree = wasm._gs_degree(),
        stride = 56 + 12 * ((degree + 1) ** 2 - 1);
    const storageCount = Math.ceil(count / 64) * 64;
    const totalBytes = storageCount * stride;
    if (!count || degree > 3 || totalBytes > limits.sceneBytes)
        throw Error('ResourceLimit: streaming batch budget');
    const prefix = new Uint8Array(await blob.slice(0, 16).arrayBuffer());
    const ply = prefix[0] === 112;
    // Byte bound prevents wide PLY rows from bypassing CPU admission.
    check(wasm, wasm._gs_begin_batch(ply && source.plyCoordinates !== 'rub' ? 1 : 0, 1));
    const sourceStride = ply ? wasm._gs_meta(11) : 20 + 3 * ((degree + 1) ** 2 - 1);
    const capacity = Math.min(65536, Math.floor(chunkBytes / sourceStride));
    const batch = capacity >= 64 ? Math.floor(capacity / 64) * 64 : capacity;
    if (!batch || (batch < 64 && count > batch))
        throw Error('ResourceLimit: source record exceeds batch capacity');
    wasm._gs_release();
    const actualBatch = Math.min(count, batch);
    const packedBatch = Math.ceil(actualBatch / 64) * 64;
    const normalizedBatch = actualBatch * (stride + 8) + 256;
    const inputBatch = actualBatch * sourceStride;
    // Include allocator growth/headroom, JS attribute slices, packing/rebase and gzip scratch.
    const nativeScratch = ply ? 2 * normalizedBatch + inputBatch : 3 * normalizedBatch + 2 * inputBatch;
    const cpuPeak =
        retainedBytes +
        32 * 2 ** 20 +
        Math.max(16 * 2 ** 20, Math.ceil(nativeScratch * 1.5)) +
        2 * inputBatch +
        packedBatch * stride +
        chunkBytes +
        2 ** 20;
    if (cpuPeak > limits.cpuBytes) throw Error('ResourceLimit: streaming CPU peak');
    let raw: Blob = blob,
        version = 0,
        fractional = 0,
        widths: number[] = [],
        offsets: number[] = [];
    if (!ply) {
        raw = await inflate(blob, directory, wasm, 16 + count * (20 + 3 * ((degree + 1) ** 2 - 1)), report);
        const header = new Uint8Array(await raw.slice(0, 16).arrayBuffer());
        const view = new DataView(header.buffer);
        version = view.getUint32(4, true);
        fractional = header[13]!;
        if (
            view.getUint32(0, true) !== 0x5053474e ||
            version < 1 ||
            version > 3 ||
            view.getUint32(8, true) !== count ||
            header[12] !== degree ||
            fractional > 30 ||
            header[14] !== 0
        )
            throw Error('DecoderFailure: SPZ header changed after admission');
        widths = [version === 1 ? 6 : 9, 1, 3, 3, version >= 3 ? 4 : 3, 3 * ((degree + 1) ** 2 - 1)];
        let offset = 16;
        offsets = widths.map((width) => {
            const start = offset;
            offset += width * count;
            return start;
        });
        if (raw.size !== offset) throw Error('DecoderFailure: SPZ inflated length');
    }
    const packed = await openFile(directory, 'scene');
    const min = [Infinity, Infinity, Infinity],
        max = [-Infinity, -Infinity, -Infinity];
    let maxScale = 0;
    try {
        packed.access.truncate(totalBytes);
        for (let start = 0; start < count; start += batch) {
            const n = Math.min(batch, count - start);
            check(wasm, wasm._gs_begin_batch(ply && source.plyCoordinates !== 'rub' ? 1 : 0, n));
            if (ply) {
                const offset = wasm._gs_meta(10),
                    width = wasm._gs_meta(11);
                const bytes = new Uint8Array(
                    await raw.slice(offset + start * width, offset + (start + n) * width).arrayBuffer(),
                );
                withBytes(wasm, bytes, (p) => check(wasm, wasm._gs_chunk(p, bytes.length)));
            } else {
                const bytes = new Uint8Array(n * widths.reduce((sum, width) => sum + width, 0));
                let cursor = 0;
                const parts = await Promise.all(
                    widths.map((width, field) =>
                        raw
                            .slice(offsets[field]! + start * width, offsets[field]! + (start + n) * width)
                            .arrayBuffer(),
                    ),
                );
                for (let field = 0; field < widths.length; field++) {
                    const width = widths[field]!;
                    const part = new Uint8Array(parts[field]!);
                    if (part.length !== n * width) throw Error('DecoderFailure: truncated SPZ attributes');
                    bytes.set(part, cursor);
                    cursor += part.length;
                }
                withBytes(wasm, bytes, (p) =>
                    check(wasm, wasm._gs_raw_spz(p, bytes.length, version, fractional)),
                );
            }
            // Pack world centers BEFORE finish rebases the disposable batch.
            const p = wasm._gs_pack_tiled(0, n);
            if (!p) throw Error('OutOfMemory: pack batch');
            try {
                const expected = Math.ceil(n / 64) * 64 * stride;
                const bytes = wasm.HEAPU8.subarray(p, p + expected);
                if (bytes.byteLength !== expected)
                    throw Error(
                        `DecoderFailure: pack view length start=${start}, ptr=${p}, expected=${expected}, actual=${bytes.byteLength}, heap=${wasm.HEAPU8.byteLength}`,
                    );
                write(packed.access, bytes, start * stride);
            } finally {
                wasm._free(p);
            }
            check(wasm, wasm._gs_finish_batch());
            for (let k = 0; k < 3; k++) {
                min[k] = Math.min(min[k]!, wasm._gs_meta(3 + k));
                max[k] = Math.max(max[k]!, wasm._gs_meta(6 + k));
            }
            maxScale = Math.max(maxScale, wasm._gs_meta(9));
            wasm._gs_release();
            report('Decoding', start + n, count);
        }
        const origin = min.map((v, k) => v + (max[k]! - v) / 2) as unknown as Vec3;
        if (packed.access.getSize() !== totalBytes)
            throw Error(`DecoderFailure: packed size before rebase ${packed.access.getSize()}/${totalBytes}`);
        const batchBytes = Math.floor(chunkBytes / (64 * stride)) * 64 * stride;
        const p = wasm._malloc(batchBytes);
        if (!p) throw Error('OutOfMemory: rebase batch');
        try {
            for (let offset = 0; offset < totalBytes; offset += batchBytes) {
                const length = Math.min(batchBytes, totalBytes - offset);
                let read = 0;
                while (read < length) {
                    const n = packed.access.read(wasm.HEAPU8.subarray(p + read, p + length), {
                        at: offset + read,
                    });
                    if (!n)
                        throw Error(
                            `DecoderFailure: backing read stalled at=${offset + read}, length=${length}, read=${read}, heap=${wasm.HEAPU8.byteLength}, ptr=${p}, file=${packed.access.getSize()}, expected=${totalBytes}`,
                        );
                    read += n;
                }
                if (!wasm._gs_rebase_tiled(p, length / stride, stride, ...origin))
                    throw Error('DecoderFailure: rebase');
                write(packed.access, wasm.HEAPU8.subarray(p, p + length), offset);
                if (packed.access.getSize() !== totalBytes)
                    throw Error(
                        `DecoderFailure: rebase write resized backing at=${offset}, length=${length}, file=${packed.access.getSize()}, expected=${totalBytes}`,
                    );
                report('Rebasing', offset + length, totalBytes);
            }
        } finally {
            wasm._free(p);
        }
        packed.access.flush();
        packed.access.close();
        const file = await packed.file.getFile();
        if (file.size !== totalBytes)
            throw Error(`ResourceLimit: browser scene snapshot length ${file.size}/${totalBytes}`);
        await directory.removeEntry('input').catch((e) => {
            if (e.name !== 'NotFoundError') throw e;
        });
        await directory.removeEntry('inflated').catch((e) => {
            if (e.name !== 'NotFoundError') throw e;
        });
        return {
            scene: {
                count,
                degree,
                stride,
                packing: 'tiled',
                pageCapacity: Math.floor(pageBytes / (64 * stride)) * 64,
                pages: [],
                origin,
                min: min as unknown as Vec3,
                max: max as unknown as Vec3,
                maxScale,
                source: source.kind === 'blob' ? (source.name ?? 'Blob') : 'URL',
                decodeMs: performance.now() - begin,
            },
            file,
        };
    } catch (reason) {
        try {
            packed.access.close();
        } catch {
            /* Already closed before snapshot. */
        }
        throw reason;
    }
}
