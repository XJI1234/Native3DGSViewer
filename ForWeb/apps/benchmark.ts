import * as THREE from 'three';
import {SparkRenderer,SplatMesh} from '@sparkjsdev/spark';
import {createEngine} from '../src/index';
import type {Pose} from '../src/engine/camera';
import type {Renderer,GpuScene} from '../src/render-core/renderer';
const canvas=document.querySelector<HTMLCanvasElement>('#canvas')!;
const mode=new URLSearchParams(location.search).get('engine')??'native';
const completedMode=new URLSearchParams(location.search).get('complete')==='1';
const next=()=>new Promise<number>(resolve=>requestAnimationFrame(resolve));
const delay=(ms:number)=>new Promise<void>(resolve=>setTimeout(resolve,ms));
let initial:Pose;
let draw:(pose:Pose)=>{cpuMs:number;gpuMs:number|null;gpuFrameId:number|null;frameId:number}|void;
let setPose:(pose:Pose)=>void;
let capture:()=>Promise<Uint8Array>;
let load:(path:string,pose?:Pose)=>Promise<unknown>;
let dispose:()=>Promise<void>;
let completedFrame:(pose:Pose)=>Promise<unknown>;
let rotate=(pose:Pose,angle:number):Pose=>{
    const d=pose.position.map((p,i)=>p-pose.target[i]!);
    return {position:[pose.target[0]+d[0]!*Math.cos(angle)+d[2]!*Math.sin(angle),pose.position[1],pose.target[2]-d[0]!*Math.sin(angle)+d[2]!*Math.cos(angle)],target:pose.target,up:pose.up};
};
if(mode==='native'){
    const result=await createEngine({canvas,assets:{baseUrl:new URL('/assets/',location.href)}});
    if(!result.ok)throw Error(JSON.stringify(result));
    const engine=result.value;engine.pause();
    (window as unknown as {benchInternals:unknown}).benchInternals={engine};
    const internals=engine as unknown as {renderer:Renderer;active:GpuScene};
    setPose=pose=>engine.camera.setPose(pose);
    draw=pose=>{setPose(pose);const scene=internals.active;return internals.renderer.render(scene,engine.camera.frame(scene.scene,1920,1080,scene.scene.count,scene.scene.degree,scene.scene.stride,scene.scene.pageCapacity),engine.camera.revision);};
    load=async(path,pose)=>{const start=performance.now();const result=await engine.open({kind:'url',url:new URL('/models/'+path,location.href).href}).result;if(!result.ok)throw Error(JSON.stringify(result));const firstFrameMs=performance.now()-start;initial=pose??engine.camera.getPose();setPose(initial);draw(initial);await internals.renderer.device.queue.onSubmittedWorkDone();const info=engine.capabilities.adapter;return {ms:performance.now()-start,firstFrameMs,count:engine.getSnapshot().sceneCount,degree:engine.getSnapshot().degree,pose:initial,adapter:{vendor:info.vendor,architecture:info.architecture,device:info.device,description:info.description}};};
    capture=async()=>{const result=await engine.capture();if(!result.ok)throw Error(JSON.stringify(result));return result.value.rgba;};
    dispose=async()=>{await engine.dispose();const failure=engine.getSnapshot().error;if(failure?.stage==='StorageCleanup')throw Error(failure.diagnostic);};
    completedFrame=async(pose)=>{
        await (internals.renderer as unknown as {timingReady:Promise<void>}).timingReady;
        const start=performance.now(),stats=draw(pose);
        await internals.renderer.device.queue.onSubmittedWorkDone();
        // Await the timestamp map callback before observing this completed sample.
        await (internals.renderer as unknown as {timingReady:Promise<void>}).timingReady;
        const timing=internals.renderer as unknown as {lastProjection:number|null;lastSort:number|null;lastDraw:number|null;lastGpu:number|null;lastGpuFrame:number|null};
        if(timing.lastGpuFrame!==stats?.frameId || timing.lastGpu===null)throw Error('Native completed frame timestamp missing or stale');
        return {completedMs:performance.now()-start,cpuMs:stats?.cpuMs,projectionMs:timing.lastProjection,sortMs:timing.lastSort,drawMs:timing.lastDraw,gpuMs:timing.lastGpu,gpuFrameId:timing.lastGpuFrame,frameId:stats.frameId};
    };
}else{
    const renderer=new THREE.WebGLRenderer({canvas,antialias:false,alpha:false,premultipliedAlpha:true,preserveDrawingBuffer:true});
    renderer.setPixelRatio(1);renderer.setSize(1920,1080,false);renderer.outputColorSpace=THREE.SRGBColorSpace;renderer.toneMapping=THREE.NoToneMapping;renderer.setClearColor(0,1);
    const camera=new THREE.PerspectiveCamera(60,1920/1080,0.00001,100000);
    const scene=new THREE.Scene();
    const spark=new SparkRenderer({renderer,maxStdDev:3,minAlpha:1/255,maxPixelRadius:4096,preBlurAmount:0.3,blurAmount:0,falloff:1,sortRadial:true,minSortIntervalMs:0,enableLod:false,accumExtSplats:true});
    scene.add(spark);let mesh:SplatMesh|undefined;
    if(completedMode)spark.autoUpdate=false;
    setPose=pose=>{camera.position.fromArray(pose.position);camera.up.fromArray(pose.up);camera.lookAt(...pose.target);const distance=Math.hypot(...pose.position.map((p,i)=>p-pose.target[i]!));camera.near=Math.max(1e-5,distance*1e-5);camera.far=Math.max(distance*100,1000);camera.updateProjectionMatrix();};
    const gl=renderer.getContext(),timer=gl.getExtension('EXT_disjoint_timer_query_webgl2');
    let query:WebGLQuery|null=null,frameId=0,lastGpu:number|null=null,lastGpuFrame:number|null=null,queryFrame=0;
    draw=pose=>{
        frameId++;setPose(pose);
        if(query && gl.getQueryParameter(query,gl.QUERY_RESULT_AVAILABLE)){
            if(!gl.getParameter(timer.GPU_DISJOINT_EXT)){lastGpu=gl.getQueryParameter(query,gl.QUERY_RESULT)/1e6;lastGpuFrame=queryFrame;}
            else{lastGpu=null;lastGpuFrame=null;}
            gl.deleteQuery(query);query=null;
        }
        const timed=timer&&!query;if(timed){query=gl.createQuery();queryFrame=frameId;gl.beginQuery(timer.TIME_ELAPSED_EXT,query);}
        const start=performance.now();renderer.render(scene,camera);
        if(timed)gl.endQuery(timer.TIME_ELAPSED_EXT);
        return {cpuMs:performance.now()-start,gpuMs:lastGpu,gpuFrameId:lastGpuFrame,frameId};
    };
    load=async(path,pose)=>{
        if(!pose)throw Error('Spark needs reference world pose');
        if(mesh){scene.remove(mesh);mesh.dispose();}
        const start=performance.now();mesh=new SplatMesh({url:new URL('/models/'+path,location.href).href,lod:false,nonLod:true,enableLod:false,extSplats:true});
        await mesh.initialized;mesh.rotation.x=path.endsWith('.ply')?Math.PI:0;scene.add(mesh);initial=pose;setPose(pose);
        await spark.update({scene,camera});
        const deadline=performance.now()+120000;
        while(!spark.orderingTexture || spark.activeSplats!==mesh.numSplats || spark.sorting){if(performance.now()>deadline)throw Error('Spark first ordering timeout');draw(pose);await next();}
        draw(pose);gl.finish();const firstFrameMs=performance.now()-start;
        for(let i=0;i<60;i++){draw(pose);await next();}
        return {ms:performance.now()-start,firstFrameMs,count:mesh.numSplats,degree:mesh.splats?.getNumSh()??0,pose,rotationX:mesh.rotation.x};
    };
    capture=async()=>{draw(initial);await delay(100);draw(initial);const gl=renderer.getContext(),rgba=new Uint8Array(1920*1080*4);gl.readPixels(0,0,1920,1080,gl.RGBA,gl.UNSIGNED_BYTE,rgba);const flipped=new Uint8Array(rgba.length);for(let y=0;y<1080;y++)flipped.set(rgba.subarray(y*1920*4,(y+1)*1920*4),(1079-y)*1920*4);return flipped;};
    dispose=async()=>{if(query)gl.deleteQuery(query);mesh?.dispose();spark.dispose();renderer.dispose();renderer.forceContextLoss();};
    completedFrame=async(pose)=>{
        const start=performance.now();setPose(pose);
        while(spark.sorting)await delay(0);
        // Explicit update drives the worker sort for this exact pose and awaits it.
        const sortStart=performance.now();await spark.update({scene,camera});
        while(spark.sorting)await delay(0);
        const updateMs=performance.now()-sortStart;
        if(spark.sortedCenter.distanceTo(camera.position)>1e-6)throw Error('Spark stale ordering pose');
        // Drain a query from a previous capture/load before timing this submission.
        if(query){while(!gl.getQueryParameter(query,gl.QUERY_RESULT_AVAILABLE))await delay(0);gl.deleteQuery(query);query=null;}
        const stats=draw(pose);gl.finish();
        if(!timer || !query || queryFrame!==stats?.frameId)throw Error('Spark completed frame timestamp missing or stale');
        while(!gl.getQueryParameter(query,gl.QUERY_RESULT_AVAILABLE))await delay(0);
        if(gl.getParameter(timer.GPU_DISJOINT_EXT))throw Error('Spark disjoint GPU timing');
        const gpuMs=gl.getQueryParameter(query,gl.QUERY_RESULT)/1e6,gpuFrameId=queryFrame;
        gl.deleteQuery(query);query=null;
        return {completedMs:performance.now()-start,updateMs,cpuMs:stats.cpuMs,gpuMs,gpuFrameId,frameId:stats.frameId};
    };
}
const bench=async(warmupMs:number,sampleMs:number,runs:number)=>{
    const samples:{run:number;frame:number;intervalMs:number;cpuMs:number;gpuMs:number|null;gpuFrameId:number|null}[]=[];
    for(let run=-1;run<runs;run++){
        const duration=run<0?warmupMs:sampleMs,start=performance.now();let previous=await next(),frame=0;
        while(performance.now()-start<duration){
            const now=await next(),pose=rotate(initial,0.20*Math.sin((now-start)*2*Math.PI/12000));
            const cpu=performance.now(),stats=draw(pose);const cpuMs=performance.now()-cpu;
            if(run>=0)samples.push({run,frame,intervalMs:now-previous,cpuMs,gpuMs:stats?.gpuMs??null,gpuFrameId:stats?.gpuFrameId??null});previous=now;frame++;
        }
    }
    setPose(initial);draw(initial);return samples;
};
const completed=async(warmup:number,frames:number,runs:number)=>{
    const samples:unknown[]=[];
    for(let run=-1;run<runs;run++)for(let frame=0;frame<(run<0?warmup:frames);frame++){
        const pose=rotate(initial,0.20*Math.sin(frame*2*Math.PI/frames));
        const stats=await completedFrame(pose);
        if(run>=0)samples.push({run,frame,...stats as object});
    }
    return samples;
};
(window as unknown as {bench:unknown}).bench={load,capture,bench,completed,dispose,mode,capturePose:async(pose:Pose)=>{const old=initial;initial=pose;if(completedMode)await completedFrame(pose);for(let i=0;i<60;i++){draw(pose);await next();}const pixels=await capture();initial=old;draw(old);return pixels;}};
