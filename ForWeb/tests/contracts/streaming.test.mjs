import { test } from 'node:test';
import assert from 'node:assert/strict';
import { gzipSync } from 'node:zlib';
import createDecoder from '../../public/assets/decoder.mjs';
const wasm = await createDecoder();
function put(bytes, fn) {
    const p = wasm._malloc(bytes.length);
    assert.ok(p);
    wasm.HEAPU8.set(bytes, p);
    try { return fn(p); } finally { wasm._free(p); }
}
test('incremental gzip handles every boundary and validates CRC and trailing members', () => {
    const raw = Buffer.from('bounded-output-test'.repeat(10000)), compressed = gzipSync(raw);
    for (const input of [compressed, Buffer.concat([compressed, compressed]), Buffer.from(compressed)]) {
        if (input !== compressed && input.length === compressed.length) input[input.length - 8] ^= 1;
        assert.equal(wasm._gs_inflate_begin(), 1);
        const out = wasm._malloc(97), chunks = [];
        let offset = 0, status = 1;
        try {
            while (status === 1 && offset < input.length) {
                const chunk = input.subarray(offset, offset + 13);
                put(chunk, p => {
                    let used = 0;
                    do {
                        status = wasm._gs_inflate_step(p + used, chunk.length - used, out, 97);
                        used += wasm._gs_inflate_consumed();
                        const n = wasm._gs_inflate_produced();
                        chunks.push(Buffer.from(wasm.HEAPU8.slice(out, out + n)));
                    } while (status === 1 && used < chunk.length);
                    offset += used;
                });
            }
            if (input === compressed) {
                assert.equal(status, 2); assert.equal(offset, input.length);
                assert.deepEqual(Buffer.concat(chunks), raw);
            } else assert.ok(status !== 2 || offset !== input.length);
        } finally { wasm._free(out); wasm._gs_inflate_end(); }
    }
});
test('batch packing keeps world centers and one global rebase matches full-scene packing', () => {
    const names = ['x','y','z','scale_0','scale_1','scale_2','rot_1','rot_2','rot_3','rot_0','f_dc_0','f_dc_1','f_dc_2','opacity'];
    const header = Buffer.from('ply\nformat binary_little_endian 1.0\nelement vertex 2\n' + names.map(n => `property float ${n}\n`).join('') + 'end_header\n');
    const body = new Float32Array([100000,2,3,-2,-2,-2,0,0,0,1,0,0,0,0,100003,7,9,-3,-3,-3,0,0,0,1,1,2,3,1]);
    const input = Buffer.concat([header, Buffer.from(body.buffer)]);
    assert.equal(put(input, p => wasm._gs_probe(p, input.length, input.length, 1e6)), 1);
    assert.equal(wasm._gs_begin(1), 1);
    assert.equal(put(new Uint8Array(body.buffer), p => wasm._gs_chunk(p, body.byteLength)), 1);
    assert.equal(wasm._gs_finish(0), 1);
    const origin = [0,1,2].map(k => wasm._gs_meta(k));
    const p = wasm._gs_pack_compact(0,2);
    const reference = wasm.HEAPU8.slice(p,p+112); wasm._free(p); wasm._gs_release();
    const chunks = [];
    for (let i=0;i<2;i++) {
        assert.equal(wasm._gs_begin_batch(1,1),1);
        assert.equal(put(new Uint8Array(body.buffer,i*56,56),p=>wasm._gs_chunk(p,56)),1);
        const p=wasm._gs_pack_compact(0,1);
        assert.equal(wasm._gs_rebase(p,1,56,...origin),1);
        chunks.push(wasm.HEAPU8.slice(p,p+56)); wasm._free(p); wasm._gs_release();
    }
    assert.deepEqual(Buffer.concat(chunks),Buffer.from(reference));
});
test('all legacy SPZ batches preserve full decoder mathematics including fractionalBits 25 and 30', () => {
    for (let version=1;version<=3;version++) for(let degree=0;degree<=3;degree++) for(const fractional of [12,25,30]) {
        const header=Buffer.alloc(16); header.writeUInt32LE(0x5053474e);header.writeUInt32LE(version,4);header.writeUInt32LE(2,8);header[12]=degree;header[13]=fractional;
        const widths=[version===1?6:9,1,3,3,version>=3?4:3,3*((degree+1)**2-1)];
        const attrs=widths.map((width,field)=>Buffer.alloc(width*2,field===0||field===4?0:128));
        const raw=Buffer.concat(attrs), input=gzipSync(Buffer.concat([header,raw]));
        assert.equal(put(input,p=>wasm._gs_probe(p,input.length,input.length,1e6)),1);
        assert.equal(wasm._gs_begin(0),1);assert.equal(put(input,p=>wasm._gs_spz(p,input.length)),1);assert.equal(wasm._gs_finish(1),1);
        const origin=[0,1,2].map(k=>wasm._gs_meta(k)),stride=56+widths[5]*4;
        const p=wasm._gs_pack_compact(0,2),reference=wasm.HEAPU8.slice(p,p+stride*2);wasm._free(p);wasm._gs_release();
        assert.equal(wasm._gs_begin_batch(0,2),1);assert.equal(put(raw,p=>wasm._gs_raw_spz(p,raw.length,version,fractional)),1);
        const q=wasm._gs_pack_compact(0,2);assert.equal(wasm._gs_rebase(q,2,stride,...origin),1);
        assert.deepEqual(wasm.HEAPU8.slice(q,q+stride*2),reference,`v${version}/SH${degree}/bits${fractional}`);wasm._free(q);wasm._gs_release();
    }
});
