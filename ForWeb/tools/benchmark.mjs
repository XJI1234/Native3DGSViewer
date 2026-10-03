import {chromium} from '@playwright/test';
import {mkdir,writeFile,readFile} from 'node:fs/promises';
const models=(process.env.GS_BENCH_MODELS??'changjin_v1.ply,spz/shengyi_v1.spz,spz/tumu_v1.spz').split(',');
const warmup=Number(process.env.GS_BENCH_WARMUP_MS??30000),duration=Number(process.env.GS_BENCH_SAMPLE_MS??60000),runs=Number(process.env.GS_BENCH_RUNS??3);
const label=process.env.GS_BENCH_LABEL??'full';
const uncapped=process.env.GS_BENCH_UNCAPPED==='1';
const modes=(process.env.GS_BENCH_ENGINES??'native,spark').split(',');
const dir='docs/verification/evidence/benchmark-'+label;await mkdir(dir,{recursive:true});
const manifest=JSON.parse(await readFile('docs/verification/evidence/model-manifest.json','utf8'));
const browserArgs=['--disable-background-timer-throttling','--disable-renderer-backgrounding',...(uncapped?['--disable-frame-rate-limit','--disable-gpu-vsync']:[])];
const browser=await chromium.launch({channel:'msedge',headless:true,args:browserArgs});
const quantile=(xs,p)=>{const s=[...xs].sort((a,b)=>a-b);return s[Math.floor((s.length-1)*p)]??null;};
const rows=[];
try{
for(const model of models){let pose;
    for(const mode of modes){
        const page=await browser.newPage({viewport:{width:1920,height:1080},deviceScaleFactor:1});const errors=[],disposalErrors=[];let disposing=false;page.on('pageerror',e=>(disposing?disposalErrors:errors).push(e.message));page.on('console',m=>{if(m.type()==='error')(disposing?disposalErrors:errors).push(m.text());});
        try{
            await page.goto('http://127.0.0.1:5173/benchmark.html?engine='+mode);await page.waitForFunction(()=>window.bench,{timeout:60000});
            const loaded=await page.evaluate(({model,pose})=>window.bench.load(model,pose),{model,pose});pose=loaded.pose;
            const slug=model.replaceAll('/','_').replaceAll('.','_')+'-'+mode;
            const pixels=await page.evaluate(async()=>(await window.bench.capture()).toBase64());await writeFile(dir+'/'+slug+'.rgba',Buffer.from(pixels,'base64'));
            for(const [view,angle] of [['left',-.2],['right',.2]]){
                const d=pose.position.map((v,i)=>v-pose.target[i]),closePose={position:[pose.target[0]+.55*(d[0]*Math.cos(angle)+d[2]*Math.sin(angle)),pose.target[1]+.55*d[1],pose.target[2]+.55*(-d[0]*Math.sin(angle)+d[2]*Math.cos(angle))],target:pose.target,up:pose.up};
                const image=await page.evaluate(async(pose)=>(await window.bench.capturePose(pose)).toBase64(),closePose);
                await writeFile(dir+'/'+model.replaceAll('/','_').replaceAll('.','_')+'-'+view+'-'+mode+'.rgba',Buffer.from(image,'base64'));
            }
            console.log('Loaded',model,mode,JSON.stringify(loaded));
            const samples=await page.evaluate(({warmup,duration,runs})=>window.bench.bench(warmup,duration,runs),{warmup,duration,runs});
            await writeFile(dir+'/'+slug+'.csv','run,frame,intervalMs,cpuMs,gpuMs,gpuFrameId\n'+samples.map(r=>[r.run,r.frame,r.intervalMs,r.cpuMs,r.gpuMs??'',r.gpuFrameId??''].join(',')).join('\n'));
            const perRun=Array.from({length:runs},(_,run)=>{const s=samples.filter(r=>r.run===run),gpu=new Map(s.filter(r=>r.gpuFrameId!==null).map(r=>[r.gpuFrameId,r.gpuMs]));return {run,frames:s.length,intervalP50:quantile(s.map(r=>r.intervalMs),.5),intervalP95:quantile(s.map(r=>r.intervalMs),.95),cpuP50:quantile(s.map(r=>r.cpuMs),.5),cpuP95:quantile(s.map(r=>r.cpuMs),.95),gpuP50:quantile([...gpu.values()],.5),gpuP95:quantile([...gpu.values()],.95)};});
            rows.push({model,sha256:manifest.models.find(m=>m.name===model)?.sha256,mode,loaded,perRun,errors,disposalErrors,gpuScope:mode==='native'?'projection + GPU radix + draw':'synchronous WebGL render, excludes asynchronous worker sort and later GPU submissions'});
            if(errors.length)throw Error(errors.join('\n'));
            disposing=true;await page.evaluate(()=>window.bench.dispose());await page.waitForTimeout(100);
        }finally{await page.close();await writeFile(dir+'/results.json',JSON.stringify({date:new Date().toISOString(),warmup,duration,runs,resolution:[1920,1080],spark:'2.3.1',three:'0.180.0',browser:browser.version(),browserArgs,uncappedRequested:uncapped,rows},null,2));}
    }
}
}finally{await browser.close();}
