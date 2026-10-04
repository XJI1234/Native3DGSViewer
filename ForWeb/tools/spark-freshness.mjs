// Short diagnostic of Spark ordering-origin lag, separate from the full benchmark.
import { chromium } from '@playwright/test';
import { mkdir, readFile, writeFile } from 'node:fs/promises';

await mkdir('.local', { recursive: true });
const slug='spark-freshness-'+Date.now(); // ignored generated files have no Vite watcher invalidation
let source = await readFile('apps/benchmark.ts', 'utf8');
const injection = `
    type SortInput={driveStarted:number;started:number;position:number[];direction:number[]};
    const diagnostic = {collect:false,samples:[] as unknown[],sorts:[] as unknown[],failures:[] as string[],isSorting:()=>spark.sorting,latest:null as null|(SortInput&{completed:number}),lastSample:0};
    (window as unknown as {sortDiagnostic:unknown}).sortDiagnostic=diagnostic;
    if(spark.readPause!==1||spark.minSortIntervalMs!==0)throw Error('Unsupported diagnostic scheduling');
    let pending:SortInput|null=null;
    const originalRead=spark.readbackDepth;
    spark.readbackDepth=function(...args:Parameters<typeof originalRead>){
        if(pending){pending.started=performance.now();pending.position=args[0].current.viewOrigin.toArray();pending.direction=args[0].current.viewDirection.toArray();}
        return originalRead.apply(this,args);
    };
    const originalSort=spark.driveSort;
    spark.driveSort=function(...args:Parameters<typeof originalSort>){
        const eligible=!this.sorting&&this.sortDirty,before=this.lastSortTime,started=performance.now();
        const work=originalSort.apply(this,args);
        if(eligible&&this.lastSortTime!==before){
            const input:SortInput={driveStarted:started,started,position:[],direction:[]};pending=input;
            void work.then(()=>{if(input.position.length!==3){diagnostic.failures.push('Missing sort input observation');return;}const completed=performance.now();diagnostic.latest={...input,completed};if(diagnostic.collect)diagnostic.sorts.push({...input,completed,latencyMs:completed-started});},error=>{if(diagnostic.collect)diagnostic.failures.push(String(error));});
        }
        return work;
    };
`;
const drawInjection = `
        const now=performance.now(),latest=diagnostic.latest;
        if(diagnostic.collect&&latest&&now-diagnostic.lastSample>=5){
            const direction=camera.getWorldDirection(new THREE.Vector3());
            const dot=direction.dot(new THREE.Vector3().fromArray(latest.direction));
            diagnostic.samples.push({at:now,frameId,orderingOriginAgeMs:now-latest.started,completedSortLatencyMs:latest.completed-latest.driveStarted,centerError:camera.position.distanceTo(new THREE.Vector3().fromArray(latest.position)),directionErrorDegrees:Math.acos(Math.max(-1,Math.min(1,dot)))*180/Math.PI,sorting:spark.sorting});
            diagnostic.lastSample=now;
        }
`;
if (!source.includes('scene.add(spark);let mesh:') || !source.includes('if(timed)gl.endQuery')) {
    throw Error('Benchmark diagnostic insertion points changed');
}
source = source.replace('scene.add(spark);let mesh:', 'scene.add(spark);' + injection + 'let mesh:')
    .replace('if(timed)gl.endQuery', drawInjection + '\n        if(timed)gl.endQuery');
await writeFile('.local/'+slug+'.ts', source);
await writeFile('.local/'+slug+'.html', '<link rel="icon" href="data:,"><canvas id="canvas" width="1920" height="1080"></canvas><script type="module" src="./'+slug+'.ts"></script>');

const browser = await chromium.launch({ channel: 'msedge', headless: true,
    args: ['--disable-frame-rate-limit', '--disable-gpu-vsync'] });
const rows = [];
const quantile = (xs, p) => [...xs].sort((a,b)=>a-b)[Math.floor((xs.length-1)*p)];
try {
    for (const model of ['changjin_v1.ply','spz/shengyi_v1.spz','spz/tumu_v1.spz']) {
        const reference = await browser.newPage({ viewport: {width:1920,height:1080} });
        await reference.goto('http://127.0.0.1:5173/benchmark.html?engine=native');
        await reference.waitForFunction(()=>window.bench);
        const loaded = await reference.evaluate(model=>window.bench.load(model), model);
        await reference.evaluate(()=>window.bench.dispose());
        await reference.close();
        const page = await browser.newPage({ viewport: {width:1920,height:1080} });
        const errors=[];
        page.on('pageerror',error=>errors.push(error.message));
        page.on('console',message=>{if(message.type()==='error')errors.push(message.text());});
        await page.goto('http://127.0.0.1:5173/.local/'+slug+'.html?engine=spark');
        try { await page.waitForFunction(()=>window.bench); }
        catch (error) { throw Error(JSON.stringify({model,errors,cause:error.message})); }
        await page.evaluate(({model,pose})=>window.bench.load(model,pose),{model,pose:loaded.pose});
        await page.evaluate(()=>window.bench.bench(3000,0,0));
        const diagnostic = await page.evaluate(async()=>{
            window.sortDiagnostic.collect=true;
            await window.bench.bench(0,12000,1);
            window.sortDiagnostic.collect=false;
            return {samples:window.sortDiagnostic.samples,sorts:window.sortDiagnostic.sorts,failures:window.sortDiagnostic.failures};
        });
        if (!diagnostic.samples.length || !diagnostic.sorts.length || diagnostic.failures.length || errors.length) throw Error(JSON.stringify({model,errors,failures:diagnostic.failures}));
        const summary = {};
        for (const key of ['orderingOriginAgeMs','completedSortLatencyMs','centerError','directionErrorDegrees']) {
            const values=diagnostic.samples.map(sample=>sample[key]);
            summary[key]={p50:quantile(values,.5),p95:quantile(values,.95),p99:quantile(values,.99),max:Math.max(...values)};
        }
        rows.push({model,sortCompletions:diagnostic.sorts.length,summary,...diagnostic,errors});
        await page.waitForFunction(()=>!window.sortDiagnostic.isSorting(),undefined,{timeout:10000});
        await page.evaluate(()=>window.bench.dispose());
        await page.waitForTimeout(100);
        if (errors.length) throw Error(JSON.stringify({model,errors}));
        await page.close();
        console.log(JSON.stringify({model,sortCompletions:diagnostic.sorts.length,summary}));
    }
} finally {
    await browser.close();
    await writeFile('docs/verification/evidence/spark-sort-freshness.json',JSON.stringify({
        date:new Date().toISOString(),browser:browser.version(),spark:'2.3.1',warmupMs:3000,sampleMs:12000,
        metric:'Time/pose difference from last completed driveSort input to current render camera',
        limitations:['Short diagnostic, not the 30s+60s×3 performance benchmark','Promise completion is ordering upload scheduled, not physical presentation','Sort-origin lag is not an image-error or motion-to-photon measurement','Uses pinned Spark internal hooks; does not change SDK'],
        complete:rows.length===3,rows},null,2));
}
