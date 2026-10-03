import {chromium} from '@playwright/test';
import {readFile,writeFile} from 'node:fs/promises';
const manifest=JSON.parse(await readFile('docs/verification/evidence/model-manifest.json','utf8'));
const browser=await chromium.launch({channel:'msedge',headless:true});const page=await browser.newPage();const errors=[];page.on('pageerror',e=>errors.push(e.message));
const rows=[];
try{
await page.goto('http://127.0.0.1:5173');await page.waitForFunction(()=>window.gs);
for(const model of manifest.models){
    const row=await page.evaluate(async(model)=>{
        const engine=window.gs.engine;await engine.closeScene();const start=performance.now();const operation=engine.open({kind:'url',url:new URL('/models/'+model.name,location.href).href});
        const result=await operation.result,snapshot=engine.getSnapshot();
        if(result.ok && (snapshot.sceneCount!==model.count||snapshot.degree!==model.degree))throw Error('Model count/SH mismatch');
        if(!result.ok && result.error.code!=='ResourceLimit')throw Error(JSON.stringify({model:model.name,result}));
        return {model:model.name,sha256:model.sha256,result,count:snapshot.sceneCount,degree:snapshot.degree,ms:performance.now()-start};
    },model);rows.push(row);console.log(JSON.stringify(row));
    await writeFile('docs/verification/evidence/model-tests.json',JSON.stringify({rows,errors},null,2));
}
const stress=await page.evaluate(async()=>{
    const engine=window.gs.engine;let accepted=0,cancelled=0;
    for(let i=0;i<12;i++){
        const operation=engine.open({kind:'url',url:new URL('/models/changjin_v1.ply',location.href).href});
        if(i%2){setTimeout(()=>operation.cancel(),50);const result=await operation.result;if(result.ok||result.error.code!=='Cancelled')throw Error('Cancellation stress');cancelled++;}
        else{if(!(await operation.result).ok)throw Error('Stress load');accepted++;}
        if(i%3===0){const recovery=engine.recover();await engine.closeScene();if(!(await recovery).ok)throw Error('Close/recovery stress');if(engine.getSnapshot().sceneCount!==0)throw Error('Scene resurrection');}
        await engine.closeScene();
    }
    await engine.dispose();return {accepted,cancelled,shutdown:engine.getSnapshot()};
});
if(errors.length)throw Error(errors.join('\n'));
await writeFile('docs/verification/evidence/model-tests.json',JSON.stringify({rows,stress,errors},null,2));
}finally{await browser.close();}
