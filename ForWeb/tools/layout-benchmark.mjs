import {evaluateBenchmark, disposeBenchmark, validateTimingSamples} from './browser-benchmark-guards.mjs';
import {chromium} from '@playwright/test';
import {writeFile} from 'node:fs/promises';
const browser=await chromium.launch({channel:'msedge',headless:true});
const rows=[];
try {
    for(const model of ['spz/shengyi_v1.spz','spz/tumu_v1.spz']) {
        const page=await browser.newPage({viewport:{width:1920,height:1080}});
        const errors=[];page.on('pageerror',e=>errors.push(e.message));page.on('console',message=>{if(message.type()==='error')errors.push(message.text());});
        let result;
        try {
        await page.goto('http://127.0.0.1:5173/benchmark.html?engine=native&complete=1');await page.waitForFunction(()=>window.bench);
        await evaluateBenchmark(page, model=>window.bench.load(model),model);
        result=await evaluateBenchmark(page, async()=>{
            // Access is confined to the development harness; SDK consumers do not depend on these internals.
            const {projection}=await import('/src/render-core/shaders.ts');
            const engine=window.benchInternals?.engine;
            if(!engine)throw Error('Benchmark internals missing');
            const renderer=engine.renderer,tiled=engine.active.scene,words=tiled.stride/4,pages=[];
            for(let base=0;base<tiled.count;base+=tiled.pageCapacity) {
                const n=Math.min(tiled.pageCapacity,tiled.count-base),padded=Math.ceil(n/64)*64;
                const input=new Float32Array(await tiled.backing.read(base*tiled.stride,padded*tiled.stride));
                const output=new Float32Array(n*words);
                for(let i=0;i<n;i++)for(let k=0;k<words;k++)output[i*words+k]=input[Math.floor(i/64)*64*words+k*64+i%64];
                pages.push(output.buffer);
            }
            tiled.backing.retain();
            try {
            const aos={...tiled,packing:'compact',backing:undefined,pages};
            const samples=[],hashes=[],fixedPose=engine.camera.getPose();let referenceImage;
            for(let run=0;run<3;run++)for(const mode of run%2?['tiled','aos-specialized','aos-dynamic']:['aos-dynamic','aos-specialized','tiled']) {
                await renderer.release(engine.active);
                engine.active=await renderer.upload(mode==='tiled'?tiled:aos,0,new AbortController().signal,()=>{});
                if(mode==='aos-dynamic') {
                    const active=engine.active;
                    active.projectPipeline=renderer.device.createComputePipeline({layout:'auto',compute:{module:renderer.device.createShaderModule({code:projection}),entryPoint:'project'}});
                    active.projectGroups=active.pages.map((page,index)=>({count:active.projectGroups[index].count,group:renderer.device.createBindGroup({layout:active.projectPipeline.getBindGroupLayout(0),entries:[page,active.ellipse,active.a,active.args,renderer.frame,active.batches[index]].map((buffer,binding)=>({binding,resource:{buffer}}))})}));
                }
                engine.camera.setPose(fixedPose);
                const image=await window.bench.capture();
                const digest=await crypto.subtle.digest('SHA-256',image);
                let maxError=0,changed=0;if(referenceImage)for(let i=0;i<image.length;i++){const delta=Math.abs(image[i]-referenceImage[i]);maxError=Math.max(maxError,delta);if(delta)changed++;}else referenceImage=image;
                hashes.push({run,mode,maxError,changed,sha256:Array.from(new Uint8Array(digest),x=>x.toString(16).padStart(2,'0')).join('')});
                const measured=await window.bench.completed(30,120,1);
                samples.push({run,mode,measured});
            }

            console.log(JSON.stringify({hashes}));
            if(hashes.some(x=>x.maxError>1))throw Error('Layout image equivalence failed: '+JSON.stringify(hashes));
            return {count:tiled.count,degree:tiled.degree,hashes,samples};
            } finally { await tiled.backing.release(); }
        });
        for(const entry of result.samples) validateTimingSamples(entry.measured,120,['projectionMs','sortMs','drawMs','gpuMs','completedMs']);
        if(errors.length)throw Error(errors.join('\n'));

        const summary=result.samples.map(({mode,run,measured})=>({mode,run,...Object.fromEntries(['projectionMs','sortMs','drawMs','gpuMs'].map(key=>{const values=measured.map(x=>x[key]).sort((a,b)=>a-b);return [key,values[59]]}))}));
        console.log(JSON.stringify({model,count:result.count,summary}));
        } finally { try {await disposeBenchmark(page);} finally {await page.close();} }
        if(errors.length)throw Error(errors.join('\n'));
        rows.push({model,...result});
    }
    await writeFile('docs/verification/evidence/layout-ablation.json',JSON.stringify({date:new Date().toISOString(),browser:browser.version(),protocol:'1920x1080; same cloud and poses; three interleaved runs per layout;30 warmup+120 complete frames; fixed-pose image difference at most 1/255',rows},null,2));
} catch(reason) {await writeFile('docs/verification/evidence/layout-ablation-failure.json',JSON.stringify({date:new Date().toISOString(),error:String(reason),rows,complete:false},null,2));throw reason;} finally {await browser.close();}
