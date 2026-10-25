import { test } from 'node:test';
import assert from 'node:assert/strict';
import { zstdCompressSync, constants } from 'node:zlib';
import createDecoder from '../../public/assets/decoder.mjs';

const wasm = await createDecoder();
function put(bytes, fn) {
    const p = wasm._malloc(bytes.length);
    assert.ok(p);
    try { wasm.HEAPU8.set(bytes, p); return fn(p); } finally { wasm._free(p); }
}
function inflate(input, inputSize = 13, capacity = 97) {
    assert.equal(wasm._gs_zstd_begin(23), 1);
    const out = wasm._malloc(capacity), chunks = [];
    let at = 0, status = 1;
    try {
        while (at < input.length && status !== 2 && status !== 0) {
            const chunk = input.subarray(at, at + inputSize);
            put(chunk, p => {
                let used = 0, produced;
                do {
                    status = wasm._gs_zstd_step(p + used, chunk.length - used, out, capacity);
                    used += wasm._gs_zstd_consumed();
                    produced = wasm._gs_zstd_produced();
                    chunks.push(Buffer.from(wasm.HEAPU8.slice(out, out + produced)));
                } while (status === 1 && (used < chunk.length || produced === capacity));
                at += used;
            });
        }
        return { status, at, bytes: Buffer.concat(chunks) };
    } finally { wasm._free(out); wasm._gs_zstd_end(); }
}

test('Zstd streaming preserves bytes with tiny input/output and bounds the window', () => {
    assert.equal(typeof wasm._gs_zstd_begin, 'function', 'bounded Zstd ABI is required');
    const raw = Buffer.from('bounded-output-test'.repeat(10000));
    const compressed = zstdCompressSync(raw, { params: { [constants.ZSTD_c_checksumFlag]: 1 } });
    assert.ok(put(compressed.subarray(0, 18), p => wasm._gs_zstd_window(p, Math.min(18, compressed.length))) > 0);
    for (const [input, output] of [[1, 1], [13, 97], [4096, 4096]]) {
        const result = inflate(compressed, input, output);
        assert.equal(result.status, 2); assert.equal(result.at, compressed.length);
        assert.deepEqual(result.bytes, raw);
    }
    const damaged = Buffer.from(compressed); damaged[damaged.length - 1] ^= 1;
    assert.equal(inflate(damaged).status, 0);
    assert.notEqual(inflate(compressed.subarray(0, -1)).status, 2);
    const trailing = Buffer.concat([compressed, compressed]);
    assert.equal(inflate(trailing).at, compressed.length, 'caller can reject trailing frame');
    assert.equal(wasm._gs_zstd_begin(9), 0);
    assert.equal(wasm._gs_zstd_begin(28), 0);
    const largeWindow = Buffer.from([0x28, 0xb5, 0x2f, 0xfd, 0, 0x78]);
    assert.equal(put(largeWindow, p => wasm._gs_zstd_window(p, largeWindow.length)), 32 * 2 ** 20);
    assert.equal(inflate(largeWindow).status, 0, 'window must fail before allocating it');
});

test('v4 bounded vendor unpack matches whole-scene math at SH0–3', () => {
    for (let degree = 0; degree <= 3; degree++) {
        const count = 65, widths = [9, 1, 3, 3, 4, 3 * ((degree + 1) ** 2 - 1)];
        const attrs = widths.map((w, field) => Buffer.alloc(w * count, field === 0 || field === 4 ? 0 : 128));
        for (let i = 0; i < count; i++) {
            attrs[0].writeIntLE((i - 32) * 128, i * 9, 3);
            attrs[4].writeUInt32LE((3 * 2 ** 30 + i * 1024) >>> 0, i * 4);
        }
        const header = Buffer.alloc(32), streams = attrs.filter(a => a.length).map(a => zstdCompressSync(a));
        header.writeUInt32LE(0x5053474e); header.writeUInt32LE(4, 4); header.writeUInt32LE(count, 8);
        header[12] = degree; header[13] = 12; header[15] = streams.length; header.writeUInt32LE(32, 16);
        const toc = Buffer.alloc(streams.length * 16);
        streams.forEach((s, i) => { toc.writeBigUInt64LE(BigInt(s.length), i * 16); toc.writeBigUInt64LE(BigInt(attrs[i].length), i * 16 + 8); });
        const file = Buffer.concat([header, toc, ...streams]);
        assert.equal(put(file, p => wasm._gs_probe(p, file.length, file.length, 1e7)), 1);
        assert.equal(wasm._gs_begin(0), 1);
        assert.equal(put(file, p => wasm._gs_spz(p, file.length)), 1);
        assert.equal(wasm._gs_finish(1), 1);
        const stride = 56 + widths[5] * 4, origin = [0, 1, 2].map(k => wasm._gs_meta(k));
        const p = wasm._gs_pack_compact(0, count), reference = wasm.HEAPU8.slice(p, p + count * stride);
        wasm._free(p); wasm._gs_release();
        for (let start = 0; start < count; start += 64) {
            const n = Math.min(64, count - start);
            const raw = Buffer.concat(attrs.map((a, i) => a.subarray(start * widths[i], (start + n) * widths[i])));
            assert.equal(wasm._gs_begin_batch(0, n), 1);
            assert.equal(put(raw, q => wasm._gs_raw_spz(q, raw.length, 4, 12)), 1);
            const q = wasm._gs_pack_compact(0, n);
            assert.equal(wasm._gs_rebase(q, n, stride, ...origin), 1);
            assert.deepEqual(wasm.HEAPU8.slice(q, q + n * stride), reference.subarray(start * stride, (start + n) * stride));
            wasm._free(q); wasm._gs_release();
        }
    }
});
