import { openFile, write } from './opfs';
import { type Decoder, withBytes } from './wasm';

interface AttributeStream {
    compressedOffset: number;
    compressedBytes: number;
    rawOffset: number;
    rawBytes: number;
}
export interface SpzV4Layout {
    streams: AttributeStream[];
    widths: number[];
    fractional: number;
    rawBytes: number;
    windowBytes: number;
}

export function parseSpzV4(
    bytes: Uint8Array,
    inputBytes: number,
    count: number,
    degree: number,
): SpzV4Layout {
    const bad = () => Error('DecoderFailure: SPZ v4 header/TOC changed after admission');
    if (bytes.length < 32) throw bad();
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const widths = [9, 1, 3, 3, 4, ...(degree ? [3 * ((degree + 1) ** 2 - 1)] : [])];
    const toc = view.getUint32(16, true),
        end = toc + widths.length * 16;
    if (
        view.getUint32(0, true) !== 0x5053474e ||
        view.getUint32(4, true) !== 4 ||
        view.getUint32(8, true) !== count ||
        bytes[12] !== degree ||
        bytes[13]! > 30 ||
        bytes[14] !== 0 ||
        bytes[15] !== widths.length ||
        toc !== 32 ||
        end > bytes.length ||
        end > inputBytes
    )
        throw bad();
    let compressedOffset = end,
        rawOffset = 0;
    const streams = widths.map((width, i) => {
        const compressed = view.getBigUint64(toc + i * 16, true),
            rawBytes = width * count;
        if (
            !compressed ||
            compressed > BigInt(inputBytes - compressedOffset) ||
            view.getBigUint64(toc + i * 16 + 8, true) !== BigInt(rawBytes)
        )
            throw bad();
        const stream = { compressedOffset, compressedBytes: Number(compressed), rawOffset, rawBytes };
        compressedOffset += stream.compressedBytes;
        rawOffset += rawBytes;
        return stream;
    });
    if (compressedOffset !== inputBytes) throw bad();
    return { streams, widths, fractional: bytes[13]!, rawBytes: rawOffset, windowBytes: 0 };
}

export async function inspectSpzV4(blob: Blob, count: number, degree: number, wasm: Decoder) {
    const layout = parseSpzV4(
        new Uint8Array(await blob.slice(0, 128).arrayBuffer()),
        blob.size,
        count,
        degree,
    );
    for (const stream of layout.streams) {
        const header = new Uint8Array(
            await blob
                .slice(
                    stream.compressedOffset,
                    stream.compressedOffset + Math.min(18, stream.compressedBytes),
                )
                .arrayBuffer(),
        );
        const window = withBytes(wasm, header, (p) => wasm._gs_zstd_window(p, header.length));
        if (window < 0) throw Error('DecoderFailure: SPZ v4 Zstd frame header/dictionary');
        if (window > 128 * 2 ** 20) throw Error('ResourceLimit: SPZ v4 Zstd window exceeds 128 MiB');
        layout.windowBytes = Math.max(layout.windowBytes, window);
    }
    return layout;
}

export async function inflateSpzV4(
    blob: Blob,
    layout: SpzV4Layout,
    directory: FileSystemDirectoryHandle,
    wasm: Decoder,
    report: (stage: string, done: number, total: number | null) => void,
): Promise<File> {
    const chunkBytes = 4 * 2 ** 20,
        raw = await openFile(directory, 'inflated');
    let out = 0;
    try {
        out = wasm._malloc(chunkBytes);
        if (!out) throw Error('OutOfMemory: Zstd output');
        raw.access.truncate(0);
        for (const stream of layout.streams) {
            const windowLog = Math.max(10, Math.ceil(Math.log2(Math.max(1, layout.windowBytes))));
            if (!wasm._gs_zstd_begin(windowLog)) throw Error('OutOfMemory: Zstd context');
            let total = 0,
                ended = false;
            for (let at = 0; at < stream.compressedBytes; at += chunkBytes) {
                const input = new Uint8Array(
                    await blob
                        .slice(
                            stream.compressedOffset + at,
                            stream.compressedOffset + Math.min(at + chunkBytes, stream.compressedBytes),
                        )
                        .arrayBuffer(),
                );
                withBytes(wasm, input, (p) => {
                    let used = 0,
                        produced = 0;
                    do {
                        const status = wasm._gs_zstd_step(p + used, input.length - used, out, chunkBytes);
                        if (status === 3) break;
                        used += wasm._gs_zstd_consumed();
                        produced = wasm._gs_zstd_produced();
                        if (!status || total + produced > stream.rawBytes)
                            throw Error('DecoderFailure: SPZ v4 Zstd checksum/length/window');
                        write(
                            raw.access,
                            wasm.HEAPU8.subarray(out, out + produced),
                            stream.rawOffset + total,
                        );
                        total += produced;
                        if (status === 2) {
                            if (
                                used !== input.length ||
                                at + used !== stream.compressedBytes ||
                                total !== stream.rawBytes
                            )
                                throw Error('DecoderFailure: SPZ v4 Zstd length/trailing data');
                            ended = true;
                            break;
                        }
                    } while (used < input.length || produced === chunkBytes);
                });
                report('Inflating', stream.compressedOffset + at + input.length, blob.size);
            }
            if (!ended) throw Error('DecoderFailure: truncated SPZ v4 Zstd frame');
            wasm._gs_zstd_end();
        }
        raw.access.flush();
        if (raw.access.getSize() !== layout.rawBytes) throw Error('DecoderFailure: SPZ v4 inflated length');
    } finally {
        wasm._gs_zstd_end();
        if (out) wasm._free(out);
        raw.access.close();
    }
    const file = await raw.file.getFile();
    if (file.size !== layout.rawBytes)
        throw Error(`ResourceLimit: browser v4 snapshot length ${file.size}/${layout.rawBytes}`);
    return file;
}
