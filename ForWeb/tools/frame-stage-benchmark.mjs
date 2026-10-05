import {launchBenchmarkBrowser} from './benchmark-browser.mjs';
import {mkdir,readFile,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {evaluateBenchmark,disposeBenchmark,validateTimingSamples} from './browser-benchmark-guards.mjs';
const models=(process.env.GS_BENCH_MODELS??'zhihuizhimen.ply').split(',');
const dir=process.env.GS_BENCH_OUTPUT??'docs/verification/evidence/parallel-2026-10-05/stages';
await mkdir(dir,{recursive:true});
const files=await Promise.all(['src/render-core/sort.ts','src/render-core/renderer.ts','src/render-core/shaders.ts'].map(async path=>({path,sha256:createHash('sha256').update(await readFile(path)).digest('hex')})));
const browser=await launchBenchmarkBrowser(true),rows=[],failures=[];
const quantile=(values,p)=>{const s=[...values].sort((a,b)=>a-b);return s[Math.floor((s.length-1)*p)];};
try{for(const model of models){
 const page=await browser.newPage({viewport:{width:1920,height:1080},deviceScaleFactor:1});const errors=[];
 page.on('pageerror',e=>errors.push(e.message));page.on('console',m=>{if(m.type()==='error')errors.push(m.text());});
 try{
  await page.goto('http://127.0.0.1:5173/benchmark.html?engine=native&complete=1');await page.waitForFunction(()=>window.bench);
  const loaded=await evaluateBenchmark(page,model=>window.bench.load(model),model);
  console.log('Stage loaded',model,loaded.count);
  const samples=await evaluateBenchmark(page,()=>window.bench.completed(30,120,3));
  validateTimingSamples(samples,360,['completedMs','projectionMs','sortMs','drawMs','gpuMs']);
  const perRun=Array.from({length:3},(_,run)=>{const s=samples.filter(s=>s.run===run);return {run,metrics:Object.fromEntries(['completedMs','projectionMs','sortMs','drawMs','gpuMs','cpuMs'].map(key=>[key,{p50:quantile(s.map(s=>s[key]),.5),p95:quantile(s.map(s=>s[key]),.95)}]))};});
  if(errors.length)throw Error(errors.join('\n'));
  await writeFile(`${dir}/${model.replaceAll('/','_')}.json`,JSON.stringify({loaded,samples,perRun},null,2));
  rows.push({model,loaded,perRun});console.log('GPU stages',model,perRun.map(r=>r.metrics.gpuMs.p50.toFixed(3)).join('/'));
 }catch(e){failures.push({model,error:String(e)});console.error('Failed stage',model,String(e));}
 finally{try{await disposeBenchmark(page);}catch(e){failures.push({model,cleanup:String(e)});}await page.close();}
}}finally{await browser.close();await writeFile(`${dir}/results.json`,JSON.stringify({date:new Date().toISOString(),browser:browser.version(),files,complete:rows.length===models.length&&!failures.length,failures,rows},null,2));}
if(failures.length)process.exitCode=1;
