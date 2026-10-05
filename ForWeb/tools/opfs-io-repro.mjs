import {chromium} from '@playwright/test';
import {mkdtemp,rm,mkdir,writeFile} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
const rows=[];
for(const persistent of [false,true]){
 const profile=await mkdtemp(join(tmpdir(),'gs-opfs-repro-'));
 const browser=persistent?await chromium.launchPersistentContext(profile,{channel:'msedge',headless:true}):await chromium.launch({channel:'msedge',headless:true});
 const page=await browser.newPage();
 try{await page.goto('http://127.0.0.1:5173');
  for(const variant of ['plain-sync','wasm-sync','plain-async','wasm-async']){
  const result=await page.evaluate(async variant=>{
   const source=`onmessage=async({data:variant})=>{let handle;try{const root=await navigator.storage.getDirectory(),file=await root.getFileHandle('io-repro',{create:true});handle=await file.createSyncAccessHandle();const size=923851264,chunk=4183808,memory=new WebAssembly.Memory({initial:256}),data=variant.startsWith('wasm')?new Uint8Array(memory.buffer,1162000,chunk):new Uint8Array(chunk),blob=new Blob([data]);handle.truncate(size);const sizes=[handle.getSize()];for(let offset=0;offset<size;offset+=chunk){if(variant.endsWith('async'))await blob.arrayBuffer();const length=Math.min(chunk,size-offset);let n=0;while(n<length){const wrote=handle.write(data.subarray(n,length),{at:offset+n});if(!wrote)throw Error('write zero');n+=wrote;}}sizes.push(handle.getSize());for(let offset=0;offset<size;offset+=chunk){const length=Math.min(chunk,size-offset);let n=0;while(n<length){const read=handle.read(data.subarray(n,length),{at:offset+n});if(!read)throw Error('read zero at '+(offset+n)+' size='+handle.getSize());n+=read;}let written=0;while(written<length){const wrote=handle.write(data.subarray(written,length),{at:offset+written});if(!wrote)throw Error('rewrite zero');written+=wrote;}}sizes.push(handle.getSize());handle.close();handle=null;await root.removeEntry('io-repro');postMessage({ok:true,sizes});}catch(e){let error=String(e);try{handle?.close();const root=await navigator.storage.getDirectory();await root.removeEntry('io-repro');}catch(cleanup){if(cleanup.name!=='NotFoundError')error+='; cleanup: '+cleanup;}postMessage({ok:false,error});}}`;
   const url=URL.createObjectURL(new Blob([source],{type:'text/javascript'})),worker=new Worker(url);
   try{return await new Promise((resolve,reject)=>{worker.onmessage=e=>resolve(e.data);worker.onerror=e=>reject(Error(e.message));worker.postMessage(variant);});}finally{worker.terminate();URL.revokeObjectURL(url);}
  },variant);rows.push({persistent,variant,...result});console.log(persistent?'persistent':'private',variant,result);
  }
 }finally{await browser.close();await rm(profile,{recursive:true,force:true});}
}
await mkdir('docs/verification/evidence/parallel-2026-10-05',{recursive:true});await writeFile('docs/verification/evidence/parallel-2026-10-05/opfs-repro-final.json',JSON.stringify(rows,null,2));
