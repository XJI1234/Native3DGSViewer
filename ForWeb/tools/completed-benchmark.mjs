import {evaluateBenchmark, disposeBenchmark, validateTimingSamples} from './browser-benchmark-guards.mjs';
import {chromium} from '@playwright/test';
import {writeFile,mkdir,readFile} from 'node:fs/promises';
const models=(process.env.GS_BENCH_MODELS??'changjin_v1.ply,spz/shengyi_v1.spz,spz/tumu_v1.spz').split(',');
const warmup=30,frames=120,runs=3;
const dir=process.env.GS_BENCH_OUTPUT??'docs/verification/evidence/benchmark-completed';await mkdir(dir,{recursive:true});
const manifest=JSON.parse(await readFile('docs/verification/evidence/model-manifest.json','utf8'));
const browser=await chromium.launch({channel:'msedge',headless:true});
const failures=[];const rows=[];const quantile=(xs,p)=>{const sorted=[...xs].sort((a,b)=>a-b);return sorted[Math.floor((sorted.length-1)*p)]};
try{
for(const model of models){let pose;
for(const mode of ['native','spark']){
    const page=await browser.newPage({viewport:{width:1920,height:1080},deviceScaleFactor:1});const errors=[];
    page.on('pageerror',e=>errors.push(e.message));page.on('console',message=>{if(message.type()==='error')errors.push(message.text());});
    try{
        await page.goto(`http://127.0.0.1:5173/benchmark.html?engine=${mode}&complete=1`);await page.waitForFunction(()=>window.bench);
        const loaded=await evaluateBenchmark(page, ({model,pose})=>window.bench.load(model,pose),{model,pose});pose=loaded.pose;
        const samples=await evaluateBenchmark(page, ({warmup,frames,runs})=>window.bench.completed(warmup,frames,runs),{warmup,frames,runs});
        validateTimingSamples(samples, frames*runs, mode==='native'?['completedMs','cpuMs','projectionMs','sortMs','drawMs','gpuMs']:['completedMs','cpuMs','gpuMs','updateMs']);
        for (let run=0;run<runs;run++) if(samples.filter(s=>s.run===run).length!==frames)throw Error('Missing run samples');
        for(const [view,angle,scale] of [['default',0,1],['left',-.2,.55],['right',.2,.55]]) {
            const d=pose.position.map((v,i)=>v-pose.target[i]);
            const camera={position:[pose.target[0]+scale*(d[0]*Math.cos(angle)+d[2]*Math.sin(angle)),pose.target[1]+scale*d[1],pose.target[2]+scale*(-d[0]*Math.sin(angle)+d[2]*Math.cos(angle))],target:pose.target,up:pose.up};
            const pixels=await evaluateBenchmark(page, async camera=>(await window.bench.capturePose(camera)).toBase64(),camera);
            await writeFile(`${dir}/${model.replaceAll('/','_').replaceAll('.','_')}-${view}-${mode}.rgba`,Buffer.from(pixels,'base64'));
        }
        const perRun=Array.from({length:runs},(_,run)=>{
            const values=samples.filter(sample=>sample.run===run),metrics={};
            for(const metric of ['completedMs','cpuMs','projectionMs','sortMs','drawMs','gpuMs','updateMs']){
                const xs=values.map(sample=>sample[metric]).filter(value=>typeof value==='number');
                if(xs.length)metrics[metric]={p50:quantile(xs,.5),p95:quantile(xs,.95)};
            }
            return {run,frames:values.length,metrics};
        });
        const row={model,sha256:manifest.models.find(entry=>entry.name===model)?.sha256,mode,loaded,perRun,errors};rows.push(row);
        await writeFile(`${dir}/${model.replaceAll('/','_')}-${mode}.json`,JSON.stringify(samples,null,2));
        console.log(JSON.stringify(row));if(errors.length)throw Error(errors.join('\n'));

    }catch(reason){failures.push({model,mode,error:String(reason)});throw reason;}finally{try{await disposeBenchmark(page);}catch(reason){failures.push({model,mode,cleanup:String(reason)});process.exitCode=1;}if(errors.length){failures.push({model,mode,errors:[...errors]});process.exitCode=1;}await page.close();await writeFile(`${dir}/results.json`,JSON.stringify({date:new Date().toISOString(),browser:browser.version(),warmup,frames,runs,resolution:[1920,1080],requested:{models,modes:['native','spark']},complete:failures.length===0&&rows.length===models.length*2,failures,protocol:'one complete submission at a time; fixed identical 120-pose path; Spark explicit update with autoUpdate=false and sortedCenter assertion; native WebGPU completion + timestamp readback; Spark gl.finish; includes default Spark readPause. GPU timer scopes differ; completedMs includes timer-observation overhead.',rows},null,2));}
}}
}finally{await browser.close();}
