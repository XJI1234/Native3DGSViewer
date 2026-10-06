import {chromium} from '@playwright/test';
import {mkdir,writeFile} from 'node:fs/promises';
import {evaluateBenchmark} from './browser-benchmark-guards.mjs';
const base=process.env.GS_TEST_URL??'http://127.0.0.1:5187';
const browser=await chromium.launch({channel:'msedge',headless:true});
const page=await browser.newPage(),errors=[];
page.on('pageerror',e=>errors.push(e.message));page.on('console',m=>{if(m.type()==='error')errors.push(m.text());});
try {
    await page.goto(base);await page.waitForFunction(()=>window.gs);
    const result=await evaluateBenchmark(page,async()=>{
        const {Renderer,engine}=window.gs,Camera=engine.camera.constructor,rows=[];engine.pause();
        const realNow=performance.now.bind(performance);let now=0;
        Object.defineProperty(performance,'now',{configurable:true,value:()=>now});
        try {
        for(const packing of ['padded','compact','tiled']){
            const canvases=[0,1].map(()=>{const c=document.createElement('canvas');c.width=128;c.height=128;document.body.append(c);return c;});
            const renderers=[],uploaded=[];
            try{
                renderers.push(await Renderer.create(canvases[0],16*2**20,{mode:'strict'}));
                renderers.push(await Renderer.create(canvases[1],16*2**20,{mode:'adaptive',targetFrameMs:100,maxSortAgeMs:1000}));
                const count=65,capacity=32,stride=packing==='padded'?64:56,pages=[];
                for(let base=0;base<count;base+=capacity){
                    const n=Math.min(capacity,count-base),words=stride/4;
                    const data=new Float32Array((packing==='tiled'?64:n)*words);
                    for(let local=0;local<n;local++){
                        const p=new Float32Array(words),i=base+local;
                        p.set([i===64?3.6:(i%8-3.5)*.3,((i/8|0)%8-3.5)*.2,0]);
                        const s=packing==='padded'?4:3,q=packing==='padded'?8:6,c=packing==='padded'?12:10;
                        p.set([.09,.07,.05],s);p[q+3]=1;p.set([i%2?.8:.2,.4,.6,.65],c);
                        for(let j=0;j<words;j++)data[packing==='tiled'?j*64+local:local*words+j]=p[j];
                    }
                    pages.push(data.buffer);
                }
                const scene={count,degree:0,stride,pageCapacity:capacity,pages,packing:packing==='padded'?undefined:packing,origin:[0,0,0],min:[-2,-2,0],max:[4,2,0],maxScale:.09,source:'adaptive-fixture',decodeMs:0};
                for(const r of renderers)uploaded.push(await r.upload(scene,0,new AbortController().signal,()=>{}));
                const camera=new Camera();camera.setPose({position:[0,0,5],target:[0,0,0],up:[0,1,0]});
                async function present(i){
                    const r=renderers[i],s=uploaded[i],f=camera.frame(scene,128,128,count,0,stride,capacity);
                    const stats=r.render(s,f,camera.revision),texture=canvases[i].getContext('webgpu').getCurrentTexture();
                    const b=r.device.createBuffer({size:128*128*4,usage:GPUBufferUsage.COPY_DST|GPUBufferUsage.MAP_READ});
                    try{const e=r.device.createCommandEncoder();e.copyTextureToBuffer({texture},{buffer:b,bytesPerRow:512},{width:128,height:128});r.device.queue.submit([e.finish()]);await b.mapAsync(GPUMapMode.READ);const pixels=new Uint8Array(b.getMappedRange()).slice();b.unmap();return {stats,pixels};}finally{b.destroy();}
                }
                const a=await present(0),b=await present(1);
                if(a.pixels.some((v,i)=>v!==b.pixels[i]))throw Error('Fresh ordering image differs '+packing);
                camera.setPose({position:[0,0,5],target:[.8,0,0],up:[0,1,0]});
                const c=await present(0),d=await present(1);
                if(d.stats.sorted||d.stats.sortReason!=='reuse')throw Error('Pure rotation did not reuse');
                if(c.pixels.some((v,i)=>v!==d.pixels[i]))throw Error('Reuse visibility image differs '+packing);
                if(!d.pixels.some((v,i)=>i%4!==3&&v>0)||d.pixels.every((v,i)=>v===b.pixels[i]))throw Error('Projection frozen or blank');
                const edge=(pixels)=>pixels.some((v,i)=>i%4!==3&&((i/4|0)%128)>=112&&v>0);
                if(edge(b.pixels)||!edge(d.pixels))throw Error('Newly visible edge point not proven '+packing);
                await renderers[1].capture(uploaded[1],camera.frame(scene,128,128,count,0,stride,capacity),camera.revision);
                now+=1;camera.setPose({position:[.005,0,5],target:[.8,0,0],up:[0,1,0]});
                const moved=await present(1);if(moved.stats.sorted||!(moved.stats.sortPositionErrorRatio>0))throw Error('Small motion not retained');
                now+=1001;
                if(!renderers[1].needsSort(uploaded[1]))throw Error('Idle refresh missing');
                const refreshed=await present(1);if(!refreshed.stats.sorted)throw Error('Idle refresh did not sort');
                const exact=await renderers[0].capture(uploaded[0],camera.frame(scene,128,128,count,0,stride,capacity),camera.revision);
                const captured=await renderers[1].capture(uploaded[1],camera.frame(scene,128,128,count,0,stride,capacity),camera.revision);
                if(exact.some((v,i)=>v!==captured[i]))throw Error('Forced capture differs '+packing);
                camera.setFlipY(true);const flip=await present(1);if(!flip.stats.sorted||flip.stats.sortReason!=='frame-change')throw Error('Y flip did not invalidate');
                camera.setPose({position:[0,0,5],target:[0,0,10],up:[0,1,0]});
                const black=await present(1);if(black.pixels.some((v,i)=>i%4!==3&&v!==0))throw Error('Stale culled ellipse '+packing);
                rows.push({packing,freshEqual:true,rotationReuseEqual:true,currentProjection:true,newVisibility:true,translationReuse:true,idleRefresh:true,captureEqual:true,yFlipFresh:true,allCulledBlack:true,bytes:uploaded.map(s=>s.bytes)});
            }finally{await Promise.all(renderers.map(r=>r.dispose()));canvases.forEach(c=>c.remove());}
        }
        return rows;
        }finally{Object.defineProperty(performance,'now',{configurable:true,value:realNow});}
    });
    if(errors.length)throw Error(errors.join('\n'));
    await mkdir('docs/verification/evidence/adaptive-2026-10-06',{recursive:true});
    await writeFile('docs/verification/evidence/adaptive-2026-10-06/correctness.json',JSON.stringify({complete:true,result,errors},null,2));
    console.log(JSON.stringify(result));
}finally{await browser.close();}
