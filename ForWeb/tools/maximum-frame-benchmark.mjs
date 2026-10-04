import {evaluateBenchmark, disposeBenchmark, validateTimingSamples} from './browser-benchmark-guards.mjs';
import {chromium} from '@playwright/test';
import {writeFile} from 'node:fs/promises';
const context=await chromium.launchPersistentContext('.local/browser-max-frame',{channel:'msedge',headless:true,viewport:{width:1920,height:1080}});
const page=await context.newPage(),errors=[];page.on('pageerror',e=>errors.push(e.message));page.on('console',m=>{if(m.type()==='error')errors.push(m.text())});
try {
    await page.goto('http://127.0.0.1:5173/benchmark.html?engine=native&complete=1');await page.waitForFunction(()=>window.bench);
    const loaded=await evaluateBenchmark(page, ()=>window.bench.load('spz/jiulonghu_v1.spz'));
    const samples=await evaluateBenchmark(page, ()=>window.bench.completed(30,120,3));
    validateTimingSamples(samples,360,['completedMs','projectionMs','sortMs','drawMs','gpuMs']);
    const quantile=(xs,p)=>{const sorted=[...xs].sort((a,b)=>a-b);return sorted[Math.floor((sorted.length-1)*p)]};
    const perRun=Array.from({length:3},(_,run)=>({run,metrics:Object.fromEntries(['completedMs','projectionMs','sortMs','drawMs','gpuMs'].map(metric=>{const xs=samples.filter(s=>s.run===run).map(s=>s[metric]);return [metric,{p50:quantile(xs,.5),p95:quantile(xs,.95)}]}))}));
    await evaluateBenchmark(page, ()=>window.bench.dispose());
    if(errors.length)throw Error(errors.join('\n'));
    const result={date:new Date().toISOString(),protocol:'full22m SH3; 1920x1080;30 warmup+120frames*3; one submitted complete frame at a time',loaded,perRun,samples,errors};
    await writeFile('docs/verification/evidence/maximum-completed-frame.json',JSON.stringify(result,null,2));
    console.log(JSON.stringify({loaded,perRun,errors}));
} catch(reason) {await writeFile('docs/verification/evidence/maximum-completed-frame-failure.json',JSON.stringify({date:new Date().toISOString(),error:String(reason),errors,complete:false},null,2));throw reason;} finally {try{await disposeBenchmark(page);}finally{await context.close();}}
