import {chromium} from '@playwright/test';
import {writeFile,mkdir} from 'node:fs/promises';
const browser=await chromium.launch({channel:process.env.GS_BROWSER_CHANNEL??'msedge',headless:true});const page=await browser.newPage({viewport:{width:1024,height:768}});
const errors=[];page.on('pageerror',e=>errors.push(e.message));page.on('console',m=>{if(m.type()==='error')errors.push(m.text());});
try {
    await page.goto('http://127.0.0.1:5173');await page.waitForFunction(()=>window.gs||document.querySelector('#status')?.textContent.includes('diagnostic'),{timeout:30000});
    if(!await page.evaluate(()=>!!window.gs))throw Error(await page.locator('#status').innerText());
    const result=await page.evaluate(async()=>{
        const {testSort}=window.gs;const adapter=await navigator.gpu.requestAdapter({powerPreference:'high-performance'});if(!adapter)throw Error('Hardware WebGPU unavailable');
        if(adapter.info.isFallbackAdapter || /swiftshader|software/i.test(adapter.info.description))throw Error('Software adapter is not accepted');
        const device=await adapter.requestDevice();const results=[];
        for(const n of [0,1,255,256,257,1025,65537,1048576]){
            const keys=Uint32Array.from({length:n},(_,i)=>(Math.imul(i,1664525)+1013904223)>>>0);
            for(let i=0;i<n;i+=5)keys[i]=7;
            const expected=Array.from({length:n},(_,i)=>i).sort((a,b)=>keys[a]-keys[b]);
            const pairs=await testSort(device,keys);if(expected.some((index,i)=>pairs[2*i]!==keys[index]||pairs[2*i+1]!==index))throw Error(`Stable sort failed at ${n}`);
            results.push({n,pass:true});
        }
        const canvas=document.createElement('canvas');canvas.width=64;canvas.height=64;document.body.append(canvas);
        const renderer=await window.gs.Renderer.create(canvas,32*2**20);
        const point=(z,color)=>{const p=new Float32Array(16);p[2]=z;p.set([0.5,0.5,0.5],4);p[11]=1;p.set(color,12);p[15]=0.5;return p.buffer;};
        const scene={count:2,degree:0,stride:64,pageCapacity:1,pages:[point(0,[1,0,0]),point(1,[0,0,1])],origin:[0,0,0],min:[-1,-1,-1],max:[1,1,1],maxScale:0.5,source:'synthetic',decodeMs:0};
        const uploaded=await renderer.upload(scene,0,new AbortController().signal,()=>{});
        const camera=new window.gs.engine.camera.constructor();
        const pixels=await renderer.capture(uploaded,camera.frame(scene,64,64,2,0,64,1),camera.revision);
        const center=Array.from(pixels.slice((32*64+32)*4,(32*64+32)*4+4));
        if(Math.abs(center[0]-64)>4||center[1]!==0||Math.abs(center[2]-127)>4||center[3]!==255)throw Error(`Two-page global transparency failed ${center}`);
        renderer.release(uploaded);await renderer.dispose();canvas.remove();
        const info={vendor:adapter.info.vendor,architecture:adapter.info.architecture,device:adapter.info.device,description:adapter.info.description};
        if(!/nvidia/i.test(info.vendor))throw Error(`Expected hardware NVIDIA adapter, got ${JSON.stringify(info)}`);
        device.destroy();return {info,sort:results,image:{twoPageTransparency:center},browser:navigator.userAgent};
    });
    if(errors.length)throw Error(errors.join('\n'));
    await mkdir('docs/verification/evidence',{recursive:true});await writeFile('docs/verification/evidence/gpu-tests.json',JSON.stringify(result,null,2));console.log(JSON.stringify(result));
}catch(reason){console.error('Browser errors:',errors);throw reason;}finally{await browser.close();}
