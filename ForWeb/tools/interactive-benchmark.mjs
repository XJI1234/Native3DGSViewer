import {launchBenchmarkBrowser} from './benchmark-browser.mjs';
import {mkdir,readFile,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';
import {verifyServedModel,verifyServedAssets} from './benchmark-provenance.mjs';
import {evaluateBenchmark,disposeBenchmark} from './browser-benchmark-guards.mjs';

const models=(process.env.GS_BENCH_MODELS??'changjin_v1.ply,shengyi_v1.ply,spz/shengyi_v1.spz,tumu_v1.ply,spz/tumu_v1.spz,zhihuizhimen.ply').split(',');
const warmupMs=Number(process.env.GS_BENCH_WARMUP_MS??10000),sampleMs=Number(process.env.GS_BENCH_SAMPLE_MS??20000),runs=Number(process.env.GS_BENCH_RUNS??3);
const dir=process.env.GS_BENCH_OUTPUT??'docs/verification/evidence/interactive-2026-10-04';
const traced=process.env.GS_BENCH_TRACE==='1';
const modes=(process.env.GS_BENCH_ENGINES??'native,spark').split(',');
const frameDepth=Number(process.env.GS_BENCH_FRAME_DEPTH??2);
const decoder=process.env.GS_BENCH_DECODER??'single',threads=Number(process.env.GS_BENCH_THREADS??4),base=process.env.GS_TEST_URL??'http://127.0.0.1:5173';
if(!['single','auto','parallel'].includes(decoder)||!Number.isInteger(threads)||threads<1||threads>8)throw Error('Invalid decoder configuration');
await mkdir(dir,{recursive:true});
const manifest=JSON.parse(await readFile('docs/verification/evidence/model-manifest.json','utf8'));
const poseFile=process.env.GS_BENCH_POSES;
const poseText=poseFile?await readFile(poseFile,'utf8'):null;
const poseOverrides=poseText?JSON.parse(poseText):null;
const reference=poseOverrides?null:JSON.parse(await readFile('../docs/reports/native3dgs-2026-10-04/evidence/web-paired/results.json','utf8'));
const largeReference=poseOverrides?null:JSON.parse(await readFile('docs/verification/evidence/parallel-2026-10-05/stages-before-persistent-large/results.json','utf8'));
const rows=[],failures=[];
const quantile=(values,p)=>{const s=[...values].sort((a,b)=>a-b);return s[Math.floor((s.length-1)*p)]??null;};
const stats=values=>({p50:quantile(values,.5),p95:quantile(values,.95),p99:quantile(values,.99),mean:values.length?values.reduce((a,b)=>a+b,0)/values.length:null});
const provenance={commit:execFileSync('git',['rev-parse','HEAD'],{encoding:'utf8'}).trim(),files:await Promise.all(['apps/benchmark.ts','tools/interactive-benchmark.mjs','src/engine/engine.ts','src/render-core/renderer.ts','src/render-core/shaders.ts','src/render-core/sort.ts','src/render-core/sorting-policy.ts','public/assets/decoder.mjs','public/assets/decoder.wasm','public/assets/threaded/decoder.mjs','public/assets/threaded/decoder.wasm','node_modules/@sparkjsdev/spark/dist/spark.module.js'].map(async path=>({path,sha256:createHash('sha256').update(await readFile(path)).digest('hex')})))};
if(poseText)provenance.poseOverride={path:poseFile,sha256:createHash('sha256').update(poseText).digest('hex')};
const freshCase=process.env.GS_BENCH_FRESH_CASE==='1';
const sorting=process.env.GS_BENCH_SORTING??'strict';
if(!['strict','adaptive'].includes(sorting))throw Error('Invalid sorting configuration');
provenance.servedAssets=await verifyServedAssets(base,provenance.files);
let browser=await launchBenchmarkBrowser(false),browserVersion=browser.version();
async function save(){await writeFile(`${dir}/results.json`,JSON.stringify({date:new Date().toISOString(),browser:browserVersion,freshCase,headless:false,persistent:process.env.GS_BENCH_PERSISTENT==='1',browserArgs:[],frameDepth,decoder,threads,sorting,base,warmupMs,sampleMs,runs,traced,resolution:[1920,1080],requested:{models,modes},provenance,complete:rows.length===models.length*modes.length&&!failures.length,failures,rows},null,2));}
try{
for(let i=0;i<models.length;i++){
    const model=models[i],entry=manifest.models.find(m=>m.name===model);
    if(!entry)throw Error(`Unknown model ${model}`);
    provenance.models??=[];provenance.models.push(await verifyServedModel(base,entry));
    const pose=poseOverrides?poseOverrides[model]:reference.rows.find(r=>r.model===model&&r.mode==='native')?.loaded.pose??largeReference.rows.find(r=>r.model===model)?.loaded.pose;
    if(!pose)throw Error(`Missing reference pose ${model}`);
    // Alternate order to reduce systematic engine-order bias.
    for(const mode of i%2?[...modes].reverse():modes){
        if(!browser){browser=await launchBenchmarkBrowser(false);browserVersion=browser.version();}
        const page=await browser.newPage({viewport:{width:1920,height:1080},deviceScaleFactor:1});
        const errors=[],warnings=[];let disposing=false,cdp;
        page.on('pageerror',e=>{if(!disposing)errors.push(e.message);});page.on('console',m=>{if(!disposing&&m.type()==='error')errors.push(m.text());if(!disposing&&m.type()==='warning')warnings.push(m.text());});
        try{
            await page.bringToFront();await page.goto(`${base}/benchmark.html?engine=${mode}&interactive=1&frames=${frameDepth}&decoder=${decoder}&threads=${threads}&sorting=${sorting}`);await page.waitForFunction(()=>window.bench,{},{timeout:60000});
            const loaded=await evaluateBenchmark(page,({model,pose})=>window.bench.load(model,pose),{model,pose});
            if(loaded.count!==entry.count||loaded.degree!==3)throw Error('Unexpected point count or SH');
            if(mode==='native'&&decoder==='parallel'&&loaded.decoder?.backend!=='pthreads')throw Error('Parallel interaction silently fell back');
            const slug=model.replaceAll('/','_')+'-'+mode;
            // Outside timed region: keep visual evidence of a nonempty canvas.
            await page.screenshot({path:`${dir}/${slug}.png`});
            let navigationStart;
            if(traced){cdp=await page.context().newCDPSession(page);await cdp.send('Performance.enable');const metrics=await cdp.send('Performance.getMetrics');navigationStart=metrics.metrics.find(m=>m.name==='NavigationStart')?.value;if(!Number.isFinite(navigationStart))throw Error('Trace clock origin missing');await cdp.send('Tracing.start',{categories:'benchmark,cc,viz,gpu,devtools.timeline,disabled-by-default-devtools.timeline.frame',transferMode:'ReturnAsStream'});}
            console.log('Started',model,mode,new Date().toISOString());
            const result=await evaluateBenchmark(page,({warmupMs,sampleMs,runs})=>window.bench.interactive(warmupMs,sampleMs,runs),{warmupMs,sampleMs,runs});
            if(traced)result.traceNavigationStart=navigationStart;
            if(traced){const done=new Promise(resolve=>cdp.once('Tracing.tracingComplete',resolve));await cdp.send('Tracing.end');const {stream}=await done;let data='';while(true){const chunk=await cdp.send('IO.read',{handle:stream});data+=chunk.data;if(chunk.eof)break;}await cdp.send('IO.close',{handle:stream});await writeFile(`${dir}/${slug}-trace.json`,data);}
            if(result.visibility.some(v=>v.state!=='visible'))throw Error('Browser became hidden');
            const perRun=Array.from({length:runs},(_,run)=>{
                const s=result.samples.filter(s=>s.run===run),input=result.inputs.filter(s=>s.run===run),w=result.runWindows.find(w=>w.run===run);
                if(s.length<30||!w||w.durationMs<sampleMs)throw Error(`Insufficient run ${run}`);
                if(s.some(s=>!Number.isFinite(s.intervalMs)||s.intervalMs<=0))throw Error('Invalid frame interval');
                const frameTimes=s.map(s=>s.intervalMs),slow=[...frameTimes].sort((a,b)=>b-a).slice(0,Math.max(1,Math.ceil(s.length*.01)));
                const complete=(result.completions??[]).filter(c=>c.run===run);
                return {run,durationMs:w.durationMs,submissions:s.length+1,submissionFps:1000*(s.length+1)/w.durationMs,inputFps:1000*(input.length+1)/w.durationMs,intervalMs:stats(frameTimes),cpuMs:stats(s.map(s=>s.cpuMs)),cameraToSubmissionMs:stats(s.map(s=>s.cameraAgeMs)),cameraToCompleteMs:stats(complete.map(c=>c.cameraToCompleteMs)),submitToCompleteMs:stats(complete.map(c=>c.submitToCompleteMs)),onePercentLowFps:1000/(slow.reduce((a,b)=>a+b,0)/slow.length),over16_7:s.filter(s=>s.intervalMs>16.7).length/s.length,over33_3:s.filter(s=>s.intervalMs>33.3).length/s.length,over50:s.filter(s=>s.intervalMs>50).length/s.length,longTasks:result.longTasks.filter(t=>t.start>=w.start&&t.start<w.end),nativeSortSubmissions:mode==='native'?s.filter(s=>s.sorted).length:null,sparkWorkerCompletionObservations:mode==='spark'?s.filter(s=>s.sorted).length:null,sortPositionErrorRatio:stats(s.map(s=>s.positionErrorRatio).filter(Number.isFinite)),sortPoseAgeMs:stats(s.map(s=>s.sortAgeMs).filter(Number.isFinite))};
            });
            await writeFile(`${dir}/${slug}.json`,JSON.stringify({model,mode,loaded,...result,perRun,errors,warnings},null,2));
            if(errors.length)throw Error(errors.join('\n'));
            disposing=true;await disposeBenchmark(page);
            rows.push({model,mode,sha256:entry.sha256,count:entry.count,inputBytes:entry.bytes,loaded,perRun,errors,warnings,rawFile:`${slug}.json`});
            console.log('Completed',model,mode,perRun.map(r=>r.submissionFps.toFixed(1)).join('/'),'fps');
        }catch(reason){failures.push({model,mode,error:String(reason)});console.error('Failed',model,mode,String(reason));}
        finally{disposing=true;try{await disposeBenchmark(page);}catch(reason){failures.push({model,mode,cleanup:String(reason)});}await page.close();if(freshCase){const closing=browser;browser=undefined;await closing.close();}await save();}
    }
}
}finally{await browser?.close();await save();}
if(failures.length)process.exitCode=1;
