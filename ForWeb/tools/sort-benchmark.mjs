import {chromium} from '@playwright/test';
import {writeFile,mkdir} from 'node:fs/promises';
const label=process.argv[2]??'baseline';
const browser=await chromium.launch({channel:'msedge',headless:true});const page=await browser.newPage();
try {await page.goto('http://127.0.0.1:5173');await page.waitForFunction(()=>window.gs);const result=await page.evaluate(async()=>{
    const adapter=await navigator.gpu.requestAdapter({powerPreference:'high-performance'});const device=await adapter.requestDevice({requiredFeatures:['timestamp-query']});
    const rows=[];for(const count of [170799,804758,1048576]){const times=await window.gs.benchmarkSort(device,count);const sorted=[...times].sort((a,b)=>a-b);rows.push({count,times,median:sorted[Math.floor(sorted.length/2)]});}device.destroy();return rows;
});await mkdir('docs/verification/evidence',{recursive:true});await writeFile(`docs/verification/evidence/sort-${label}.json`,JSON.stringify(result,null,2));console.log(JSON.stringify(result.map(r=>({count:r.count,median:r.median}))));}finally{await browser.close();}
