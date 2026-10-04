import {chromium} from '@playwright/test';
import {writeFile} from 'node:fs/promises';
const output=process.env.GS_SOAK_OUTPUT??'docs/verification/evidence/stability-tests.json';
const duration=Number(process.env.GS_SOAK_MS??1800000),cycles=Number(process.env.GS_SOAK_CYCLES??100);
const browser=await chromium.launch({channel:'msedge',headless:true,args:['--enable-precise-memory-info']});const page=await browser.newPage({viewport:{width:1280,height:900}});const errors=[];
page.on('pageerror',e=>errors.push(e.message));page.on('console',m=>{if(m.type()==='error')errors.push(m.text());});
await page.addInitScript(()=>{
    const devices=new Map(),owners=new WeakMap(),contexts=new Set();let workers=0;
    const create=GPUDevice.prototype.createBuffer,destroy=GPUBuffer.prototype.destroy,deviceDestroy=GPUDevice.prototype.destroy;
    GPUDevice.prototype.createBuffer=function(options){const buffer=create.call(this,options);let buffers=devices.get(this);if(!buffers){buffers=new Map();devices.set(this,buffers);}buffers.set(buffer,options.size);owners.set(buffer,this);return buffer;};
    GPUBuffer.prototype.destroy=function(){devices.get(owners.get(this))?.delete(this);return destroy.call(this);};
    GPUDevice.prototype.destroy=function(){devices.delete(this);return deviceDestroy.call(this);};
    const configure=GPUCanvasContext.prototype.configure,unconfigure=GPUCanvasContext.prototype.unconfigure;
    GPUCanvasContext.prototype.configure=function(options){contexts.add(this);return configure.call(this,options);};
    GPUCanvasContext.prototype.unconfigure=function(){contexts.delete(this);return unconfigure.call(this);};
    const NativeWorker=Worker;
    window.Worker=class extends NativeWorker{constructor(...args){super(...args);workers++;this.closed=false;}terminate(){if(!this.closed){this.closed=true;workers--;}return super.terminate();}};
    window.ownedResources=()=>({devices:devices.size,buffers:[...devices.values()].reduce((n,b)=>n+b.size,0),bufferBytes:[...devices.values()].reduce((n,b)=>n+[...b.values()].reduce((a,v)=>a+v,0),0),contexts:contexts.size,workers,jsHeap:performance.memory?.usedJSHeapSize??null});
});
const samples=[];
try{
await page.goto('http://127.0.0.1:5173');await page.waitForFunction(()=>window.gs);
const cycleResult=await page.evaluate(async(cycles)=>{
    const engine=window.gs.engine,url=new URL('/models/changjin_v1.ply',location.href).href;
    let replacements=0,cancelled=0,mounts=0;
    const counts=[];
    for(let i=0;i<cycles;i++){
        if(i%3===0){const result=await engine.open({kind:'url',url}).result;if(!result.ok)throw Error(JSON.stringify(result));replacements++;}
        else if(i%3===1){const before=engine.getSnapshot().sceneCount,operation=engine.open({kind:'url',url});setTimeout(()=>operation.cancel(),10);const result=await operation.result;if(result.ok||result.error.code!=='Cancelled'||engine.getSnapshot().sceneCount!==before)throw Error('Cancel lifecycle');cancelled++;}
        else{const canvas=document.createElement('canvas');canvas.width=320;canvas.height=240;document.body.append(canvas);const created=await window.gs.createEngine({canvas,assets:{baseUrl:new URL('/assets/',location.href)}});if(!created.ok)throw Error(JSON.stringify(created));await created.value.dispose();canvas.remove();mounts++;}
        if(i%10===9){await engine.closeScene();const r=window.ownedResources();if(r.workers!==0||r.contexts!==1||r.devices!==1||r.buffers!==4)throw Error('Owned resource growth '+JSON.stringify(r));counts.push({cycle:i,resources:r});}
    }
    const result=await engine.open({kind:'url',url}).result;if(!result.ok)throw Error(JSON.stringify(result));
    window.soakPose=engine.camera.getPose();window.soakStart=performance.now();window.soakFrames=0;
    const animate=now=>{const base=window.soakPose,dx=base.position[0]-base.target[0],dz=base.position[2]-base.target[2],angle=.2*Math.sin((now-window.soakStart)*2*Math.PI/12000);engine.camera.setPose({position:[base.target[0]+dx*Math.cos(angle)+dz*Math.sin(angle),base.position[1],base.target[2]-dx*Math.sin(angle)+dz*Math.cos(angle)],target:base.target,up:base.up});engine.requestFrame();window.soakFrames++;window.soakRaf=requestAnimationFrame(animate);};window.soakRaf=requestAnimationFrame(animate);
    return {replacements,cancelled,mounts,counts};
},cycles);
const baseline=await page.evaluate(()=>({resources:window.ownedResources(),frameId:window.gs.engine.getSnapshot().stats?.frameId??0,gpuFrameId:window.gs.engine.renderer.lastGpuFrame??0,timestamps:window.gs.engine.renderer.device.features.has('timestamp-query')}));let lastFrame=baseline.frameId,lastGpu=baseline.gpuFrameId;
const cdp=await page.context().newCDPSession(page);const start=Date.now();
while(Date.now()-start<duration){
    await new Promise(r=>setTimeout(r,Math.min(30000,duration-(Date.now()-start))));
    await cdp.send('HeapProfiler.collectGarbage');
    const sample=await page.evaluate(()=>({resources:window.ownedResources(),phase:window.gs.engine.getSnapshot().phase,frameId:window.gs.engine.getSnapshot().stats?.frameId??0,gpuFrameId:window.gs.engine.renderer.lastGpuFrame??0,frames:window.soakFrames}));sample.elapsedMs=Date.now()-start;samples.push(sample);
    if(sample.resources.buffers!==baseline.resources.buffers||sample.resources.bufferBytes!==baseline.resources.bufferBytes||sample.frameId<=lastFrame||(baseline.timestamps&&sample.gpuFrameId<=lastGpu))throw Error('Soak buffer growth or stalled GPU frames');lastFrame=sample.frameId;lastGpu=sample.gpuFrameId;
    if(sample.resources.workers!==0||sample.resources.devices!==1||sample.resources.contexts!==1||sample.phase!=='Ready')throw Error('Soak state/resource regression');
    await writeFile(output,JSON.stringify({duration,cycles,cycleResult,samples,errors,complete:false},null,2));console.log(JSON.stringify(sample));
}
const shutdown=await page.evaluate(async()=>{cancelAnimationFrame(window.soakRaf);const engine=window.gs.engine;await engine.closeScene();const closed=window.ownedResources();await engine.dispose();return {closed,disposed:window.ownedResources(),phase:engine.getSnapshot().phase};});
if(errors.length||shutdown.disposed.buffers||shutdown.disposed.workers||shutdown.disposed.contexts||shutdown.disposed.devices||shutdown.phase!=='Stopped')throw Error(JSON.stringify({errors,shutdown}));
await writeFile(output,JSON.stringify({duration,cycles,cycleResult,samples,errors,shutdown,complete:true},null,2));
}finally{await browser.close();}
