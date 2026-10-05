import {readFile,writeFile} from 'node:fs/promises';
import {join} from 'node:path';
const dir=process.argv[2];if(!dir)throw Error('Usage: node tools/summarize-trace.mjs <evidence directory>');
const result=JSON.parse(await readFile(join(dir,'results.json'),'utf8'));
if(!result.complete||result.failures.length||!result.traced)throw Error('A complete traced run is required');
const rows=[];
const quantile=(a,p)=>[...a].sort((x,y)=>x-y)[Math.floor((a.length-1)*p)];
for(const row of result.rows){
    const raw=JSON.parse(await readFile(join(dir,row.rawFile),'utf8'));
    const events=JSON.parse(await readFile(join(dir,row.rawFile.replace('.json','-trace.json')),'utf8')).traceEvents;
    for(const window of raw.runWindows){
        const begin=(raw.traceNavigationStart*1000+window.start)*1000,end=(raw.traceNavigationStart*1000+window.end)*1000;
        const selected=events.filter(e=>e.ts>=begin&&e.ts<end),metrics={};
        for(const name of ['AnimationFrame::Presentation','DrawFrame','DCompPresenter::Present']){
            const times=selected.filter(e=>e.name===name).map(e=>e.ts).sort((a,b)=>a-b);
            if(times.length<2)throw Error(`Insufficient ${name} events`);
            const intervals=times.slice(1).map((t,i)=>(t-times[i])/1000),slow=[...intervals].sort((a,b)=>b-a).slice(0,Math.max(1,Math.ceil(intervals.length*.01)));
            metrics[name]={events:times.length,rate:1000*times.length/window.durationMs,intervalP50Ms:quantile(intervals,.5),intervalP95Ms:quantile(intervals,.95),onePercentLowFps:1000/(slow.reduce((a,b)=>a+b,0)/slow.length)};
        }
        rows.push({model:row.model,engine:row.mode,depth:result.frameDepth,run:window.run,metrics,submissionFps:row.perRun[window.run].submissionFps});
    }
}
await writeFile(join(dir,'trace-summary.json'),JSON.stringify({warning:'Independent traced run. Browser Present calls are not optical scanout or end-to-end mouse latency.',rows},null,2));
console.log(rows.map(r=>({model:r.model,engine:r.engine,depth:r.depth,present:r.metrics['DCompPresenter::Present'].rate,p95:r.metrics['DCompPresenter::Present'].intervalP95Ms})));
