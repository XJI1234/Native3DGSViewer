import type { Limits, Scene, Source, Vec3 } from '../splat-types/index';
import { check, type Decoder, withBytes } from './wasm';

interface SyncFile {
    write(data: ArrayBufferView, options: { at: number }): number;
    read(data: ArrayBufferView, options: { at: number }): number;
    truncate(size: number): void;
    flush(): void;
    close(): void;
}
async function openFile(directory: FileSystemDirectoryHandle, name: string) {
    const file = await directory.getFileHandle(name, { create: true });
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
    const declared = Number(response.headers.get('Content-Length'));
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
    const totalBytes = count * stride;
    if (
        !count ||
        degree > 3 ||
        totalBytes > limits.sceneBytes ||
        retainedBytes + 96 * 2 ** 20 > limits.cpuBytes
    )
        throw Error('ResourceLimit: streaming batch budget');
    const prefix = new Uint8Array(await blob.slice(0, 16).arrayBuffer());
    const ply = prefix[0] === 112;
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
        // Bound bytes as well as points: unknown scalar PLY columns may make rows wide.
        check(wasm, wasm._gs_begin_batch(ply && source.plyCoordinates !== 'rub' ? 1 : 0, 1));
        const sourceStride = ply ? wasm._gs_meta(11) : 0;
        const batch = ply ? Math.min(16384, Math.floor(chunkBytes / sourceStride)) : 16384;
        if (!batch) throw Error('ResourceLimit: PLY record exceeds batch capacity');
        wasm._gs_release();
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
                for (let field = 0; field < widths.length; field++) {
                    const width = widths[field]!;
                    const part = new Uint8Array(
                        await raw
                            .slice(offsets[field]! + start * width, offsets[field]! + (start + n) * width)
                            .arrayBuffer(),
                    );
                    if (part.length !== n * width) throw Error('DecoderFailure: truncated SPZ attributes');
                    bytes.set(part, cursor);
                    cursor += part.length;
                }
                withBytes(wasm, bytes, (p) =>
                    check(wasm, wasm._gs_raw_spz(p, bytes.length, version, fractional)),
                );
            }
            // Pack world centers BEFORE finish rebases the disposable batch.
            const p = wasm._gs_pack_compact(0, n);
            if (!p) throw Error('OutOfMemory: pack batch');
            try {
                write(packed.access, wasm.HEAPU8.subarray(p, p + n * stride), start * stride);
            } finally {
                wasm._free(p);
            }
            check(wasm, wasm._gs_finish(0));
            for (let k = 0; k < 3; k++) {
                min[k] = Math.min(min[k]!, wasm._gs_meta(3 + k));
                max[k] = Math.max(max[k]!, wasm._gs_meta(6 + k));
            }
            maxScale = Math.max(maxScale, wasm._gs_meta(9));
            wasm._gs_release();
            report('Decoding', start + n, count);
        }
        const origin = min.map((v, k) => v + (max[k]! - v) / 2) as unknown as Vec3;
        const batchBytes = batch * stride;
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
                    if (!n) throw Error('DecoderFailure: backing read stalled');
                    read += n;
                }
                if (!wasm._gs_rebase(p, length / stride, stride, ...origin))
                    throw Error('DecoderFailure: rebase');
                write(packed.access, wasm.HEAPU8.subarray(p, p + length), offset);
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
                packing: 'compact',
                pageCapacity: Math.floor(pageBytes / stride),
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
