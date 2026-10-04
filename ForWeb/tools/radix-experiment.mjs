import {chromium} from '@playwright/test';
import {writeFile} from 'node:fs/promises';
const browser=await chromium.launch({channel:'msedge',headless:true});const page=await browser.newPage();const errors=[];page.on('console',m=>{if(m.type()==='error')errors.push(m.text());});
try{
await page.goto('http://127.0.0.1:5173');await page.waitForFunction(()=>window.gs);
const rows=await page.evaluate(async()=>{
    const adapter=await navigator.gpu.requestAdapter({powerPreference:'high-performance'}),device=await adapter.requestDevice({requiredFeatures:['timestamp-query']});const rows=[];
    try{for(const count of [170799,804758,1248730,1888950])for(const bits of [4,8]){
        const keys=Uint32Array.from({length:count},(_,i)=>i%5===0?7:(Math.imul(i,1664525)+1013904223)>>>0);
        const expected=Array.from({length:count},(_,i)=>i).sort((a,b)=>keys[a]-keys[b]);const sorted=await window.gs.testSort(device,keys,bits);
        if(expected.some((index,i)=>sorted[2*i]!==keys[index]||sorted[2*i+1]!==index))throw Error('Stable order '+bits);
        const samples=await window.gs.benchmarkSort(device,count,30,bits),median=[...samples].sort((a,b)=>a-b)[15];rows.push({count,bits,median,samples,stable:true});
    }}finally{device.destroy();}return rows;
});
if(errors.length)throw Error(errors.join('\n'));await writeFile('docs/verification/evidence/radix-experiment.json',JSON.stringify(rows,null,2));console.log(JSON.stringify(rows.map(({samples,...row})=>row)));
}finally{await browser.close();}
