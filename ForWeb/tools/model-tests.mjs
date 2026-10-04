import {evaluateBenchmark} from './browser-benchmark-guards.mjs';
import {chromium} from '@playwright/test';
import {readFile,writeFile} from 'node:fs/promises';
const manifest=JSON.parse(await readFile('docs/verification/evidence/model-manifest.json','utf8'));
const browser=await chromium.launchPersistentContext('.local/browser-models',{channel:'msedge',headless:true});const page=await browser.newPage();const errors=[];page.on('pageerror',e=>errors.push(e.message));
const rows=[];
const output=process.env.GS_MODEL_OUTPUT??'docs/verification/evidence/model-tests-large.json';
try{
await page.goto('http://127.0.0.1:5173');await page.waitForFunction(()=>window.gs);
// This persistent profile is reserved for this runner; clear jobs interrupted by a previous test process.
await evaluateBenchmark(page, async()=>{const root=await navigator.storage.getDirectory();for await(const name of root.keys())if(name.startsWith('gs-'))await root.removeEntry(name,{recursive:true});});
for(const model of manifest.models.filter(model=>!process.env.GS_MODEL_FILTER||model.name.includes(process.env.GS_MODEL_FILTER))){
    const row=await evaluateBenchmark(page, async(model)=>{
        const engine=window.gs.engine;await engine.closeScene();if(engine.getSnapshot().error?.stage==='StorageCleanup')throw Error(engine.getSnapshot().error.diagnostic);const start=performance.now();let stage='Inspecting',last=start;const stages={};const unsubscribe=engine.subscribe(()=>{const snapshot=engine.getSnapshot(),next=snapshot.progress?.stage??snapshot.phase;if(next!==stage){stages[stage]=(stages[stage]??0)+performance.now()-last;stage=next;last=performance.now();}});const operation=engine.open({kind:'url',url:new URL('/models/'+model.name.split('/').map(encodeURIComponent).join('/'),location.href).href});
        const result=await operation.result,loadMs=performance.now()-start,snapshot=engine.getSnapshot();unsubscribe();stages[stage]=(stages[stage]??0)+performance.now()-last;
        if(result.ok && (snapshot.sceneCount!==model.count||snapshot.degree!==model.degree))throw Error('Model count/SH mismatch');
        const details=engine.active?{gpuBytes:engine.active.bytes,packing:engine.active.scene.packing??'padded',backingBytes:engine.active.scene.backing?.totalBytes??0,cpuResidentBytes:engine.renderer.retainedBytes}:{};
        const cleanupStart=performance.now();await engine.closeScene();if(engine.getSnapshot().error?.stage==='StorageCleanup')throw Error(engine.getSnapshot().error.diagnostic);
        const root=await navigator.storage.getDirectory();const jobs=[];for await(const name of root.keys())if(name.startsWith('gs-'))jobs.push(name);
        if(jobs.length)throw Error('Storage leak: '+jobs.join(','));
        return {model:model.name,sha256:model.sha256,result,count:snapshot.sceneCount,degree:snapshot.degree,ms:loadMs,cleanupMs:performance.now()-cleanupStart,stages,...details};
    },model);rows.push(row);console.log(JSON.stringify(row));
    await writeFile(output,JSON.stringify({rows,errors},null,2));
    if(!row.result.ok)throw Error(JSON.stringify(row));
}
const stress=await evaluateBenchmark(page, async()=>{
    const engine=window.gs.engine;let accepted=0,cancelled=0;
    for(let i=0;i<12;i++){
        const operation=engine.open({kind:'url',url:new URL('/models/changjin_v1.ply',location.href).href});
        if(i%2){setTimeout(()=>operation.cancel(),50);const result=await operation.result;if(result.ok||result.error.code!=='Cancelled')throw Error('Cancellation stress');cancelled++;}
        else{if(!(await operation.result).ok)throw Error('Stress load');accepted++;}
        if(i%3===0){const recovery=engine.recover();await engine.closeScene();if(engine.getSnapshot().error?.stage==='StorageCleanup')throw Error(engine.getSnapshot().error.diagnostic);if(!(await recovery).ok)throw Error('Close/recovery stress');if(engine.getSnapshot().sceneCount!==0)throw Error('Scene resurrection');}
        await engine.closeScene();if(engine.getSnapshot().error?.stage==='StorageCleanup')throw Error(engine.getSnapshot().error.diagnostic);
    }
    await engine.dispose();return {accepted,cancelled,shutdown:engine.getSnapshot()};
});
if(errors.length)throw Error(errors.join('\n'));
await writeFile(output,JSON.stringify({rows,stress,errors},null,2));
}finally{await browser.close();}
