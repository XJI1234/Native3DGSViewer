import {readFile,writeFile} from 'node:fs/promises';
import {join} from 'node:path';
const root=process.argv[2]??'docs/verification/evidence/parallel-2026-10-05';
const read=async name=>JSON.parse(await readFile(join(root,name),'utf8'));
const median=values=>[...values].sort((a,b)=>a-b)[Math.floor(values.length/2)];
const complete=async name=>{const d=await read(`${name}/results.json`);if(!d.complete||d.failures.length)throw Error(`Incomplete ${name}`);return d;};
const paired=await complete('paired-final');
const scheduling=await Promise.all(['latency-final-depth1','latency-final-depth2'].map(async name=>{const d=await complete(name),r=d.rows[0];return {name,depth:d.frameDepth,model:r.model,fps:median(r.perRun.map(p=>p.submissionFps)),cameraP50:median(r.perRun.map(p=>p.cameraToCompleteMs?.p50??NaN)),cameraP95:median(r.perRun.map(p=>p.cameraToCompleteMs?.p95??NaN)),persistent:d.persistent??false};}));
const stages=[];
for(const name of ['stages-before-persistent','stages-before-persistent-large','stages-after-fusion','stages-no-clear','stages-group-count','stages-alpha-bounds','stages-radix6']){
    const d=await complete(name);for(const r of d.rows)stages.push({experiment:name,model:r.model,count:r.loaded.count,metrics:Object.fromEntries(['projectionMs','sortMs','drawMs','gpuMs'].map(k=>[k,median(r.perRun.map(p=>p.metrics[k].p50))])),rounds:r.perRun.map(p=>p.metrics.gpuMs.p50)});
}
const large=await complete('large-final');
const interactions=[...paired.rows,...large.rows].map(r=>({model:r.model,engine:r.mode,count:r.count,bytes:r.inputBytes,sha256:r.sha256,fps:median(r.perRun.map(p=>p.submissionFps)),p95:median(r.perRun.map(p=>p.intervalMs.p95)),onePercentLow:median(r.perRun.map(p=>p.onePercentLowFps)),rounds:r.perRun.map(p=>p.submissionFps)}));
const {date,browser,headless,persistent,frameDepth,warmupMs,sampleMs,runs,resolution,requested,provenance,complete:completed}=paired;
await writeFile(join(root,'summary.json'),JSON.stringify({schema:1,protocol:{date,browser,headless,persistent,frameDepth,warmupMs,sampleMs,runs,resolution,requested,provenance,complete:completed},metricLimitations:'Pre-OCR paired data retain FPS/intervals only; historical age and sort completion labels are excluded.',scheduling,stages,interactions},null,2));
console.log(interactions.map(r=>({model:r.model,engine:r.engine,fps:r.fps,p95:r.p95,low:r.onePercentLow})));
