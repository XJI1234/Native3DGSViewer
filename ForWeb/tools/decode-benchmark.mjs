import {readFile,writeFile} from 'node:fs/promises';
import {performance} from 'node:perf_hooks';
import createDecoder from '../public/assets/decoder.mjs';
const bytes=await readFile(process.env.GS_DECODE_MODEL??'C:/Users/21544/Desktop/zhishan/changjin_v1.ply');
const wasm=await createDecoder(),put=(bytes,fn)=>{const p=wasm._malloc(bytes.length);if(!p)throw Error('OOM');try{wasm.HEAPU8.set(bytes,p);if(!fn(p))throw Error(wasm.UTF8ToString(wasm._gs_error()));}finally{wasm._free(p);}};
const times=[];
for(let run=-1;run<5;run++){
    const start=performance.now();put(bytes.subarray(0,65536),p=>wasm._gs_probe(p,65536,bytes.length,768*2**20));if(!wasm._gs_begin(1))throw Error('begin');
    const offset=wasm._gs_meta(10),stride=wasm._gs_meta(11),count=wasm._gs_count(),batch=Math.floor(4*2**20/stride);
    for(let i=0;i<count;i+=batch){const chunk=bytes.subarray(offset+i*stride,offset+Math.min(count,i+batch)*stride);put(chunk,p=>wasm._gs_chunk(p,chunk.length));}
    if(!wasm._gs_finish(0))throw Error('finish');const ms=performance.now()-start;wasm._gs_release();if(run>=0)times.push(ms);
}
const row={label:process.env.GS_DECODE_LABEL??'current',source:'changjin_v1.ply',times,median:[...times].sort((a,b)=>a-b)[2],scope:'WASM probe + normalize, input preloaded, no packing/network/GPU'};
await writeFile('docs/verification/evidence/decode-'+row.label+'.json',JSON.stringify(row,null,2));console.log(JSON.stringify(row));
