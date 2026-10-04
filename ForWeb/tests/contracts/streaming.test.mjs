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
        for(let i=0;i<6;i++) {
            if(version===1)attrs[0].writeUInt16LE([0x3c00,0xc000,0x3800,0xbc00,0x4000,0xb800][i],i*2);
            else attrs[0].writeIntLE([4096,-8192,2048,-4096,8192,-2048][i],i*3,3);
        }
        attrs[1].set([64,192]);attrs[2].set([100,130,170,180,90,140]);attrs[3].set([80,100,120,90,110,130]);
        if(version<3)attrs[4].set([150,110,135,100,145,120]);
        else {attrs[4].writeUInt32LE((3*2**30 + 120*2**20 + (512+90)*2**10 + 60)>>>0,0);attrs[4].writeUInt32LE((3*2**30 + (512+70)*2**20 + 110*2**10 + (512+50))>>>0,4);}
        for(let i=0;i<attrs[5].length;i++)attrs[5][i]=90+(i*7)%80;
        const raw=Buffer.concat(attrs), input=gzipSync(Buffer.concat([header,raw]));
        assert.equal(put(input,p=>wasm._gs_probe(p,input.length,input.length,1e6)),1);
        assert.equal(wasm._gs_begin(0),1);assert.equal(put(input,p=>wasm._gs_spz(p,input.length)),1);assert.equal(wasm._gs_finish(1),1);
        const origin=[0,1,2].map(k=>wasm._gs_meta(k)),stride=56+widths[5]*4;
        const p=wasm._gs_pack_compact(0,2),reference=wasm.HEAPU8.slice(p,p+stride*2);wasm._free(p);wasm._gs_release();
        const combined=[];
        for(let point=0;point<2;point++) {
            const batch=Buffer.concat(attrs.map((attr,field)=>attr.subarray(point*widths[field],(point+1)*widths[field])));
            assert.equal(wasm._gs_begin_batch(0,1),1);assert.equal(put(batch,p=>wasm._gs_raw_spz(p,batch.length,version,fractional)),1);
            const q=wasm._gs_pack_compact(0,1);assert.equal(wasm._gs_rebase(q,1,stride,...origin),1);
            const packed=wasm.HEAPU8.slice(q,q+stride),values=new Float32Array(packed.buffer);
            const close=(actual,expected)=>assert.ok(Math.abs(actual-expected)<=2e-6*Math.max(1,Math.abs(expected)),`${actual} != ${expected}`);
            for(let k=0;k<3;k++)close(values[k],(version===1?[1,-2,.5,-1,2,-.5][point*3+k]:[4096,-8192,2048,-4096,8192,-2048][point*3+k]/2**fractional)-origin[k]);
            close(values[13],attrs[1][point]/255);
            for(let k=0;k<3;k++)close(values[3+k],Math.exp(attrs[3][point*3+k]/16-10));
            let rotation;
            if(version<3){rotation=Array.from(attrs[4].subarray(point*3,point*3+3),x=>x/127.5-1);rotation.push(Math.sqrt(1-rotation.reduce((sum,v)=>sum+v*v,0)));}
            else {let bits=attrs[4].readUInt32LE(point*4),sum=0;rotation=[0,0,0,0];const largest=bits>>>30;for(let k=3;k>=0;k--)if(k!==largest){rotation[k]=Math.SQRT1_2*(bits&511)/511*((bits&512)?-1:1);sum+=rotation[k]**2;bits>>>=10;}rotation[largest]=Math.sqrt(1-sum);}
            const norm=Math.hypot(...rotation);rotation.forEach((value,k)=>close(values[6+k],value/norm));
            for(let k=0;k<widths[5];k++)close(values[14+k],(attrs[5][point*widths[5]+k]-128)/128);
            combined.push(Buffer.from(packed));wasm._free(q);wasm._gs_release();
        }
        assert.deepEqual(Buffer.concat(combined),Buffer.from(reference),`v${version}/SH${degree}/bits${fractional}`);
    }
});

test('tiled float32 batches match compact mathematics at partial tiles for every SH degree', () => {
    for (const count of [65,127,129]) for (let degree=0;degree<=3;degree++) {
        const rest=3*((degree+1)**2-1), words=14+rest;
        const names=['x','y','z','scale_0','scale_1','scale_2','rot_1','rot_2','rot_3','rot_0','f_dc_0','f_dc_1','f_dc_2','opacity',...Array.from({length:rest},(_,i)=>`f_rest_${i}`)];
        const header=Buffer.from(`ply\nformat binary_little_endian 1.0\nelement vertex ${count}\n`+names.map(n=>`property float ${n}\n`).join('')+'end_header\n');
        const body=new Float32Array(count*words);
        for(let i=0;i<count;i++)for(let k=0;k<words;k++)body[i*words+k]=k<3?1000+i*3+k:k<6?-2+i*.001:k<10?(k===9?1:.01*(k-5)):Math.sin(i+k)*.1;
        const input=Buffer.concat([header,Buffer.from(body.buffer)]);
        assert.equal(put(input,p=>wasm._gs_probe(p,input.length,input.length,1e7)),1);
        assert.equal(wasm._gs_begin(1),1);
        assert.equal(put(new Uint8Array(body.buffer),p=>wasm._gs_chunk(p,body.byteLength)),1);
        const origin=[1010,-1020,-1030], stride=words*4;
        const aos=wasm._gs_pack_compact(0,count), tiled=wasm._gs_pack_tiled(0,count), padded=Math.ceil(count/64)*64;
        assert.ok(aos&&tiled);
        try {
            const initial=new Float32Array(wasm.HEAPU8.slice(tiled,tiled+padded*stride).buffer);
            for(let i=count;i<padded;i++)for(let k=0;k<words;k++)assert.equal(initial[Math.floor(i/64)*64*words+k*64+i%64],0);
            assert.equal(wasm._gs_rebase(aos,count,stride,...origin),1);
            assert.equal(wasm._gs_rebase_tiled(tiled,padded,stride,...origin),1);
            const a=new Float32Array(wasm.HEAPU8.slice(aos,aos+count*stride).buffer), b=new Float32Array(wasm.HEAPU8.slice(tiled,tiled+padded*stride).buffer);
            for(let i=0;i<count;i++)for(let k=0;k<words;k++)assert.equal(b[Math.floor(i/64)*64*words+k*64+i%64],a[i*words+k],`${count}/SH${degree}/point${i}/field${k}`);
            assert.equal(wasm._gs_rebase_tiled(tiled,count,stride,...origin),0);
        } finally {wasm._free(aos);wasm._free(tiled);wasm._gs_release();}
    }
});

test('world batch validation retains near-zero bounds without a disposable float32 rebase error', () => {
    const names=['x','y','z','scale_0','scale_1','scale_2','rot_1','rot_2','rot_3','rot_0','f_dc_0','f_dc_1','f_dc_2','opacity'];
    const header=Buffer.from('ply\nformat binary_little_endian 1.0\nelement vertex 2\n'+names.map(n=>`property float ${n}\n`).join('')+'end_header\n');
    const body=new Float32Array([0,67.41854095458984,0,-2,-2,-2,0,0,0,1,0,0,0,0,1,.9322479963302612,1,-2,-2,-2,0,0,0,1,0,0,0,0]);
    const input=Buffer.concat([header,Buffer.from(body.buffer)]);
    assert.equal(put(input,p=>wasm._gs_probe(p,input.length,input.length,1e6)),1);
    for(const worldValidation of [false,true]) {
        assert.equal(wasm._gs_begin_batch(1,2),1);
        assert.equal(put(new Uint8Array(body.buffer),p=>wasm._gs_chunk(p,body.byteLength)),1);
        if(!worldValidation)assert.equal(wasm._gs_finish(0),0,'old local-batch tolerance reproduces false rejection');
        else {
            assert.equal(wasm._gs_finish_batch(),1);
            assert.equal(wasm._gs_meta(1),0);
            assert.equal(wasm._gs_meta(4),-body[1]);
            assert.equal(wasm._gs_meta(7),-body[15]);
            const p=wasm._gs_pack_compact(0,2),values=new Float32Array(wasm.HEAPU8.slice(p,p+112).buffer);
            assert.equal(values[1],-body[1]);assert.equal(values[15],-body[15]);wasm._free(p);
        }
        wasm._gs_release();
    }
});


test('inflate distinguishes a zero-input drain needing more compressed bytes from corruption', () => {
    assert.equal(wasm._gs_inflate_begin(),1);
    const out=wasm._malloc(32);
    try {assert.equal(wasm._gs_inflate_step(0,0,out,32),3);assert.equal(wasm._gs_inflate_produced(),0);}
    finally {wasm._free(out);wasm._gs_inflate_end();}
});


test('exactly full inflate output may need input before the gzip trailer', () => {
    const raw=Buffer.alloc(32,42),gzip=gzipSync(raw),out=wasm._malloc(32);
    assert.equal(wasm._gs_inflate_begin(),1);
    try {
        const prefix=gzip.subarray(0,-8);
        assert.equal(put(prefix,p=>wasm._gs_inflate_step(p,prefix.length,out,32)),1);
        assert.equal(wasm._gs_inflate_produced(),32);
        assert.deepEqual(Buffer.from(wasm.HEAPU8.slice(out,out+32)),raw);
        assert.equal(wasm._gs_inflate_step(0,0,out,32),3);
        assert.equal(put(gzip.subarray(-8),p=>wasm._gs_inflate_step(p,8,out,32)),2);
    } finally {wasm._free(out);wasm._gs_inflate_end();}
});
