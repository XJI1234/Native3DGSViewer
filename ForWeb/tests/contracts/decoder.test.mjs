import {test} from 'node:test';
import assert from 'node:assert/strict';
import createDecoder from '../../public/assets/decoder.mjs';
import {gzipSync,zstdCompressSync} from 'node:zlib';
const wasm=await createDecoder();
function put(bytes,fn){const p=wasm._malloc(bytes.length);assert.ok(p);wasm.HEAPU8.set(bytes,p);try{return fn(p);}finally{wasm._free(p);}}

test('legacy SPZ rejects inflated trailing payload and verifies checksum/length',()=>{
    const header=Buffer.alloc(16);header.writeUInt32LE(0x5053474e);header.writeUInt32LE(2,4);header.writeUInt32LE(1,8);header[13]=12;
    const raw=Buffer.concat([header,Buffer.alloc(19)]),valid=gzipSync(raw);
    const bomb=gzipSync(Buffer.concat([raw,Buffer.alloc(8*2**20)]));
    for(const input of [bomb,valid.subarray(0,valid.length-1),Buffer.concat([valid,valid])]){
        assert.equal(put(input,p=>wasm._gs_probe(p,input.length,input.length,1e6)),1);assert.equal(wasm._gs_begin(0),1);
        assert.equal(put(input,p=>wasm._gs_spz(p,input.length)),0);wasm._gs_release();
    }
    assert.equal(put(valid,p=>wasm._gs_probe(p,valid.length,valid.length,1e6)),1);assert.equal(wasm._gs_begin(0),1);
    assert.equal(put(valid,p=>wasm._gs_spz(p,valid.length)),1);assert.equal(wasm._gs_finish(1),1);wasm._gs_release();
});

test('all supported SPZ versions and SH degrees decode and reject truncated streams',()=>{
    for(let version=1;version<=4;version++)for(let degree=0;degree<=3;degree++){
        const header=Buffer.alloc(version===4?32:16);header.writeUInt32LE(0x5053474e);header.writeUInt32LE(version,4);header.writeUInt32LE(1,8);header[12]=degree;header[13]=12;
        const widths=[version===1?6:9,1,3,3,version>=3?4:3,...(degree?[3*((degree+1)**2-1)]:[])];
        let input;
        if(version===4){
            header[15]=widths.length;header.writeUInt32LE(32,16);
            const streams=widths.map(n=>zstdCompressSync(Buffer.alloc(n))),toc=Buffer.alloc(widths.length*16);
            streams.forEach((s,i)=>{toc.writeBigUInt64LE(BigInt(s.length),i*16);toc.writeBigUInt64LE(BigInt(widths[i]),i*16+8);});
            input=Buffer.concat([header,toc,...streams]);
        }else input=gzipSync(Buffer.concat([header,...widths.map(n=>Buffer.alloc(n))]));
        assert.equal(put(input,p=>wasm._gs_probe(p,input.length,input.length,1e6)),1,`probe v${version} SH${degree}`);
        assert.equal(wasm._gs_begin(0),1);assert.equal(put(input,p=>wasm._gs_spz(p,input.length)),1);assert.equal(wasm._gs_finish(1),1);
        assert.equal(wasm._gs_degree(),degree);assert.equal(wasm._gs_stride(),[64,112,160,256][degree]);wasm._gs_release();
        assert.equal(put(input,p=>wasm._gs_probe(p,input.length,input.length,1e6)),1);assert.equal(wasm._gs_begin(0),1);
        assert.equal(put(input,p=>wasm._gs_spz(p,input.length-1)),0);wasm._gs_release();
    }
});
function ply(){
    const names=['x','y','z','f_dc_0','f_dc_1','f_dc_2','opacity','scale_0','scale_1','scale_2','rot_0','rot_1','rot_2','rot_3'];
    const header=new TextEncoder().encode('ply\nformat binary_little_endian 1.0\nelement vertex 1\n'+names.map(n=>`property float ${n}\n`).join('')+'end_header\n');
    const body=new Float32Array([1,2,3,0,0,0,0,-2,-2,-2,1,0,0,0]);
    const bytes=new Uint8Array(header.length+body.byteLength);bytes.set(header);bytes.set(new Uint8Array(body.buffer),header.length);return {bytes,header,body};
}
test('rejects corrupt input without publishing a scene',()=>{const bytes=new Uint8Array([1,2,3]);assert.equal(put(bytes,p=>wasm._gs_probe(p,bytes.length,bytes.length,1e6)),0);wasm._gs_release();});
test('normalizes PLY coordinates, rotation, DC, scale and sigmoid',()=>{
    const {bytes,body}=ply();assert.equal(put(bytes,p=>wasm._gs_probe(p,bytes.length,bytes.length,1e6)),1);
    assert.equal(wasm._gs_begin(1),1);assert.equal(put(new Uint8Array(body.buffer),p=>wasm._gs_chunk(p,body.byteLength)),1);
    assert.equal(wasm._gs_finish(0),1);assert.equal(wasm._gs_count(),1);assert.equal(wasm._gs_meta(0),1);assert.equal(wasm._gs_meta(1),-2);assert.equal(wasm._gs_meta(2),-3);
    const p=wasm._gs_pack(0,1);const point=new Float32Array(wasm.HEAPU8.buffer,p,16);assert.equal(point[12],0.5);assert.equal(point[15],0.5);assert.ok(Math.abs(point[4]-Math.exp(-2))<1e-6);assert.equal(wasm._gs_stride(),64);wasm._free(p);wasm._gs_release();
});
test('refuses truncated body, partial chunk and oversized declaration',()=>{
    const {bytes}=ply();assert.equal(put(bytes,p=>wasm._gs_probe(p,bytes.length,bytes.length-1,1e6)),0);
    assert.equal(put(bytes,p=>wasm._gs_probe(p,bytes.length,bytes.length,1)),0);
    assert.equal(put(bytes,p=>wasm._gs_probe(p,bytes.length,bytes.length,1e6)),1);wasm._gs_begin(1);
    assert.equal(put(new Uint8Array(3),p=>wasm._gs_chunk(p,3)),0);assert.equal(wasm._gs_finish(0),0);wasm._gs_release();
});

test('compiled property layout preserves reordered columns and rejects nonfinite attributes',()=>{
    const {bytes,body}=ply();
    const text=new TextDecoder().decode(bytes);
    const header=new TextEncoder().encode(text.slice(0,text.indexOf('end_header')+11).replace('property float x\nproperty float y','property float y\nproperty float x'));
    const reordered=new Float32Array(body);[reordered[0],reordered[1]]=[reordered[1],reordered[0]];
    const input=new Uint8Array(header.length+body.byteLength);input.set(header);input.set(new Uint8Array(reordered.buffer),header.length);
    assert.equal(put(input,p=>wasm._gs_probe(p,input.length,input.length,1e6)),1);assert.equal(wasm._gs_begin(1),1);
    assert.equal(put(new Uint8Array(reordered.buffer),p=>wasm._gs_chunk(p,reordered.byteLength)),1);assert.equal(wasm._gs_finish(0),1);
    assert.equal(wasm._gs_meta(0),1);assert.equal(wasm._gs_meta(1),-2);wasm._gs_release();
    assert.equal(put(bytes,p=>wasm._gs_probe(p,bytes.length,bytes.length,1e6)),1);wasm._gs_begin(1);body[0]=NaN;
    assert.equal(put(new Uint8Array(body.buffer),p=>wasm._gs_chunk(p,body.byteLength)),0);wasm._gs_release();
});

test('SH channel-major PLY becomes interleaved GPU coefficients in RUB',()=>{
    const {bytes,body}=ply();const old=new TextDecoder().decode(bytes.slice(0,bytes.length-body.byteLength));
    const header=new TextEncoder().encode(old.replace('end_header',Array.from({length:9},(_,i)=>`property float f_rest_${i}\n`).join('')+'end_header'));
    const expanded=new Float32Array([...body,1,2,3,4,5,6,7,8,9]);const input=new Uint8Array(header.length+expanded.byteLength);input.set(header);input.set(new Uint8Array(expanded.buffer),header.length);
    assert.equal(put(input,p=>wasm._gs_probe(p,input.length,input.length,1e6)),1);assert.equal(wasm._gs_begin(0),1);
    assert.equal(put(new Uint8Array(expanded.buffer),p=>wasm._gs_chunk(p,expanded.byteLength)),1);assert.equal(wasm._gs_finish(0),1);
    const p=wasm._gs_pack(0,1);assert.deepEqual(Array.from(new Float32Array(wasm.HEAPU8.buffer,p+64,9)),[1,4,7,2,5,8,3,6,9]);wasm._free(p);wasm._gs_release();
});
