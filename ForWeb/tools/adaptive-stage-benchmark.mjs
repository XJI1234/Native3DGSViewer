import {readFile,mkdir,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {launchBenchmarkBrowser} from './benchmark-browser.mjs';
import {evaluateBenchmark,disposeBenchmark} from './browser-benchmark-guards.mjs';
import {verifyServedAssets,verifyServedModel} from './benchmark-provenance.mjs';
const base=process.env.GS_TEST_URL??'http://127.0.0.1:5187',output='docs/verification/evidence/adaptive-2026-10-06/stages.json';
const manifest=JSON.parse(await readFile('docs/verification/evidence/model-manifest.json','utf8'));
const references=JSON.parse(await readFile('docs/verification/evidence/parallel-2026-10-05/stages-before-persistent-large/results.json','utf8'));
const names=(process.env.GS_STAGE_MODELS??'spz/jiulonghu_v1.spz').split(',');
const files=await Promise.all(['src/render-core/renderer.ts','src/render-core/shaders.ts','src/render-core/sort.ts','src/render-core/sorting-policy.ts','public/assets/decoder.mjs','public/assets/decoder.wasm','public/assets/threaded/decoder.mjs','public/assets/threaded/decoder.wasm'].map(async path=>({path,sha256:createHash('sha256').update(await readFile(path)).digest('hex')})));
const servedAssets=await verifyServedAssets(base,files),rows=[];
await mkdir('docs/verification/evidence/adaptive-2026-10-06',{recursive:true});
for(const name of names){
    const entry=manifest.models.find(m=>m.name===name),pose=references.rows.find(r=>r.model===name)?.loaded.pose;
    if(!entry||!pose)throw Error('Missing model or reference pose');
    const input=await verifyServedModel(base,entry);
    for(const sorting of ['strict','adaptive']){
        const browser=await launchBenchmarkBrowser(true),page=await browser.newPage(),errors=[];
        page.on('pageerror',e=>errors.push(e.message));page.on('console',m=>{if(m.type()==='error')errors.push(m.text());});
        try{
            await page.goto(`${base}/benchmark.html?engine=native&sorting=${sorting}&decoder=parallel&threads=4`);await page.waitForFunction(()=>window.bench);
            const loaded=await evaluateBenchmark(page,({name,pose})=>window.bench.load(name,pose),{name,pose});
            if(loaded.count!==entry.count||loaded.degree!==3||loaded.decoder.backend!=='pthreads')throw Error('Unexpected model');
            const samples=await evaluateBenchmark(page,async pose=>{
                const engine=window.benchInternals.engine,r=engine.renderer,s=engine.active,result=[];
                engine.pause();
                const dx=pose.position[0]-pose.target[0],dz=pose.position[2]-pose.target[2];
                for(let run=-1;run<3;run++)for(let i=0;i<(run<0?30:120);i++){
                    const angle=.2*Math.sin(i*2*Math.PI/120);
                    engine.camera.setPose({...pose,position:[pose.target[0]+dx*Math.cos(angle)+dz*Math.sin(angle),pose.position[1],pose.target[2]-dx*Math.sin(angle)+dz*Math.cos(angle)]});
                    await r.timingReady;
                    const start=performance.now(),stats=r.render(s,engine.camera.frame(s.scene,1920,1080,s.scene.count,s.scene.degree,s.scene.stride,s.scene.pageCapacity),engine.camera.revision);
                    await r.device.queue.onSubmittedWorkDone();await r.timingReady;
                    if(r.lastGpuFrame!==stats.frameId||r.lastGpu===null)throw Error('Unattributed timing');
                    if(run>=0)result.push({run,frame:i,frameId:stats.frameId,gpuFrameId:r.lastGpuFrame,sorted:stats.sorted,reason:stats.sortReason,sortAgeMs:stats.sortAgeMs,sortPositionErrorRatio:stats.sortPositionErrorRatio,cpuMs:stats.cpuMs,wallMs:performance.now()-start,projectionMs:r.lastProjection,sortMs:r.lastSort,drawMs:r.lastDraw,gpuMs:r.lastGpu});
                }
                return result;
            },pose);
            const pixels=await evaluateBenchmark(page,async pose=>Array.from(await window.bench.capturePose(pose)),pose);
            const rgbaSha256=createHash('sha256').update(Uint8Array.from(pixels)).digest('hex');
            if(sorting==='adaptive'&&rows.find(r=>r.name===name&&r.sorting==='strict')?.rgbaSha256!==rgbaSha256)throw Error('Real-model forced capture differs');
            if(errors.length)throw Error(errors.join('\n'));
            await disposeBenchmark(page);rows.push({name,sorting,input,loaded,samples,rgbaSha256,errors});
            await writeFile(output,JSON.stringify({complete:false,headless:true,freshCase:true,warmupFrames:30,frames:120,runs:3,protocol:'completed GPU stages; 30 warmup + 120 x 3; serialized fence/map; NOT browsing FPS',files,servedAssets,rows},null,2));
            console.log('Completed stages',name,sorting);
        }finally{await browser.close();}
    }
}
await writeFile(output,JSON.stringify({complete:true,headless:true,freshCase:true,warmupFrames:30,frames:120,runs:3,protocol:'completed GPU stages; 30 warmup + 120 x 3; serialized fence/map; NOT browsing FPS',files,servedAssets,rows},null,2));
