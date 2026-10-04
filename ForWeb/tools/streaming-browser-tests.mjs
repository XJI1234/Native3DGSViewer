import {evaluateBenchmark} from './browser-benchmark-guards.mjs';
import {createServer} from 'node:http';
import {gzipSync} from 'node:zlib';
import {chromium} from '@playwright/test';
import {writeFile} from 'node:fs/promises';
const names=['x','y','z','scale_0','scale_1','scale_2','rot_1','rot_2','rot_3','rot_0','f_dc_0','f_dc_1','f_dc_2','opacity'];
const header=Buffer.from('ply\nformat binary_little_endian 1.0\nelement vertex 1\n'+names.map(n=>`property float ${n}\n`).join('')+'end_header\n');
const encoded=gzipSync(Buffer.concat([header,Buffer.from(new Float32Array([0,0,0,-2,-2,-2,0,0,0,1,0,0,0,0]).buffer)]));
const server=createServer((_req,res)=>{res.writeHead(200,{'Content-Encoding':'gzip','Content-Length':encoded.length,'Access-Control-Allow-Origin':'*'});res.end(encoded);});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const encodedUrl=`http://127.0.0.1:${server.address().port}/encoded.ply`;
let browser;
try{
    browser=await chromium.launch({channel:'msedge',headless:true});
    const page=await browser.newPage();const errors=[];page.on('pageerror',e=>errors.push(e.message));
    await page.goto('http://127.0.0.1:5173');await page.waitForFunction(()=>window.gs);
    const result=await evaluateBenchmark(page, async(encodedUrl)=>{
        const canvas=document.createElement('canvas');canvas.width=canvas.height=64;
        const created=await window.gs.createEngine({canvas,assets:{baseUrl:new URL('/assets/',location.href)},limits:{cpuBytes:64*2**20}});
        if(!created.ok)throw Error(JSON.stringify(created));const engine=created.value;engine.pause();
        const rows=[];
        try{
            const encoded=await engine.open({kind:'url',url:encodedUrl}).result;if(!encoded.ok||engine.getSnapshot().sceneCount!==1)throw Error('HTTP encoded body length '+JSON.stringify(encoded));await engine.closeScene();if(engine.getSnapshot().error?.stage==='StorageCleanup')throw Error(engine.getSnapshot().error.diagnostic);
            for(let version=1;version<=3;version++)for(let degree=0;degree<=3;degree++){
                const header=new Uint8Array(16),view=new DataView(header.buffer);
                view.setUint32(0,0x5053474e,true);view.setUint32(4,version,true);view.setUint32(8,2,true);header[12]=degree;header[13]=30;
                const widths=[version===1?6:9,1,3,3,version>=3?4:3,3*((degree+1)**2-1)];
                const raw=new Blob([header,...widths.map((width,field)=>new Uint8Array(width*2).fill(field===0||field===4?0:128))]);
                const compressed=new Uint8Array(await new Response(raw.stream().pipeThrough(new CompressionStream('gzip'))).arrayBuffer());
                const loaded=await engine.open({kind:'blob',blob:new Blob([compressed])}).result;
                if(!loaded.ok||engine.getSnapshot().sceneCount!==2||engine.getSnapshot().degree!==degree)throw Error(JSON.stringify({version,degree,loaded}));
                rows.push({version,degree,fractionalBits:30,ok:true});
                if(version===3&&degree===3){
                    const crc=new Uint8Array(compressed);crc[crc.length-8]^=1;
                    for(const bad of [crc,compressed.slice(0,-1),new Blob([compressed,compressed]),new Blob([compressed,new Uint8Array([0])])]){
                        const failure=await engine.open({kind:'blob',blob:bad instanceof Blob?bad:new Blob([bad])}).result;
                        if(failure.ok||failure.error.code!=='DecoderFailure'||engine.getSnapshot().sceneCount!==2)throw Error('Streaming corrupt input published or lost old scene');
                    }
                }
                await engine.closeScene();if(engine.getSnapshot().error?.stage==='StorageCleanup')throw Error(engine.getSnapshot().error.diagnostic);
            }
            const names=['x','y','z','scale_0','scale_1','scale_2','rot_1','rot_2','rot_3','rot_0','f_dc_0','f_dc_1','f_dc_2','opacity',...Array.from({length:45},(_,i)=>`f_rest_${i}`),...Array.from({length:250},(_,i)=>`extra_${i}`)];
            const count=150000,stride=names.length*4;
            const header=new TextEncoder().encode('ply\nformat binary_little_endian 1.0\nelement vertex '+count+'\n'+names.map(name=>`property float ${name}\n`).join('')+'end_header\n');
            const points=new Float32Array(count*names.length);
            for(let i=0;i<count;i++){const at=i*names.length;points[at]=(i%500)*.01;points[at+1]=Math.floor(i/500)*.01;points[at+3]=points[at+4]=points[at+5]=-7;points[at+9]=1;}
            const loaded=await engine.open({kind:'blob',blob:new Blob([header,points])}).result;
            if(!loaded.ok||engine.getSnapshot().sceneCount!==count)throw Error(JSON.stringify({widePly:loaded}));
            await engine.closeScene();if(engine.getSnapshot().error?.stage==='StorageCleanup')throw Error(engine.getSnapshot().error.diagnostic);
            const root=await navigator.storage.getDirectory();const jobs=[];for await(const name of root.keys())if(name.startsWith('gs-'))jobs.push(name);
            if(jobs.length)throw Error('OPFS leak '+jobs);
            return {httpContentEncoding:true,legacy:rows,corruptGzipCases:4,widePly:{count,stride,sourceBytes:header.length+points.byteLength,cpuBudget:64*2**20,complete:true},remainingJobs:jobs};
        }finally{await engine.dispose();if(engine.getSnapshot().error?.stage==='StorageCleanup')throw Error(engine.getSnapshot().error.diagnostic);}
    },encodedUrl);
    if(errors.length)throw Error(errors.join('\n'));
    await writeFile('docs/verification/evidence/streaming-browser-tests.json',JSON.stringify({result,errors},null,2));console.log(JSON.stringify(result));
}finally{try{await browser?.close();}finally{await new Promise(resolve=>server.close(resolve));}}
