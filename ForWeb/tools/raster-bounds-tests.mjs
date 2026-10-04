import {chromium} from '@playwright/test';
import {readFile,writeFile} from 'node:fs/promises';
import {evaluateBenchmark} from './browser-benchmark-guards.mjs';

const baseline=await readFile(new URL('../tests/fixtures/drawing-reference.wgsl',import.meta.url),'utf8');
const requireDifferent=process.env.GS_RASTER_REQUIRE_CHANGE==='1';
const browser=await chromium.launch({channel:'msedge',headless:true});
const page=await browser.newPage();const errors=[];
page.on('pageerror',e=>errors.push(e.message));
page.on('console',m=>{if(m.type()==='error')errors.push(m.text());});
try{
    await page.goto('http://127.0.0.1:5173');await page.waitForFunction(()=>window.gs);
    const result=await evaluateBenchmark(page,async({baseline,requireDifferent})=>{
        const {drawing}=await import('/src/render-core/shaders.ts');
        const candidateDiffers=baseline!==drawing;
        if(requireDifferent&&!candidateDiffers)throw Error('No raster candidate differs from the pinned reference');
        const canvas=document.createElement('canvas');canvas.width=192;canvas.height=128;
        const renderer=await window.gs.Renderer.create(canvas,64*2**20),camera=new window.gs.engine.camera.constructor();
        const device=renderer.device,module=device.createShaderModule({code:baseline});
        device.pushErrorScope('validation');
        const reference=device.createRenderPipeline({layout:'auto',vertex:{module,entryPoint:'vertex'},fragment:{module,entryPoint:'fragment',targets:[{format:renderer.format,blend:{color:{srcFactor:'one',dstFactor:'one-minus-src-alpha'},alpha:{srcFactor:'one',dstFactor:'one-minus-src-alpha'}}}]},primitive:{topology:'triangle-strip'}});
        const optimized=renderer.draw,rows=[];
        try{
            for(const count of [1,17,513])for(const opacity of [1/255*1.00001,.008,.02,.1,.5,.999]){
                const p=new Float32Array(count*14);
                for(let i=0;i<count;i++)p.set([Math.sin(i*17)*3,Math.cos(i*7)*2,Math.sin(i*3),.2+(i%5)*.07,.07+(i%7)*.04,.08,0,0,Math.sin(i*.17),Math.cos(i*.17),.2,.7,.3,opacity],i*14);
                const scene={count,degree:0,stride:56,pageCapacity:count,pages:[p.buffer],packing:'compact',origin:[0,0,0],min:[-3,-2,-1],max:[3,2,1],maxScale:.8,source:'bounds-fixture',decodeMs:0};
                const uploaded=await renderer.upload(scene,0,new AbortController().signal,()=>{});
                const originalGroup=uploaded.drawGroup,referenceGroup=device.createBindGroup({layout:reference.getBindGroupLayout(0),entries:[uploaded.ellipse,uploaded.a,renderer.frame].map((buffer,binding)=>({binding,resource:{buffer}}))});
                try{
                    for(const clip of [false,true]){
                        const frame=camera.frame(scene,192,128,count,0,56,count);if(clip)new Float32Array(frame)[23]=2;
                        const revision=rows.length+1;
                        renderer.draw=reference;uploaded.drawGroup=referenceGroup;const expected=await renderer.capture(uploaded,frame,revision);
                        renderer.draw=optimized;uploaded.drawGroup=originalGroup;const actual=await renderer.capture(uploaded,frame,revision);
                        let maxError=0,different=0;for(let i=0;i<actual.length;i++){const delta=Math.abs(actual[i]-expected[i]);maxError=Math.max(maxError,delta);if(delta)different++;}
                        if(maxError>1)throw Error(`Raster bounds mismatch count=${count}, opacity=${opacity}, clip=${clip}, error=${maxError}`);
                        rows.push({count,opacity,clip,maxError,different});
                    }
                }finally{await renderer.release(uploaded);}
            }
        }finally{const validation=await device.popErrorScope();renderer.draw=optimized;await renderer.dispose();if(validation)throw Error(validation.message);}
        return {candidateDiffers,mode:candidateDiffers?'candidate comparison':'baseline consistency',rows};
    },{baseline,requireDifferent});
    if(errors.length)throw Error(errors.join('\n'));
    await writeFile('docs/verification/evidence/parallel-2026-10-05/raster-bounds-tests.json',JSON.stringify({...result,errors},null,2));console.log(result.mode,':',result.rows.length,'max error',Math.max(...result.rows.map(r=>r.maxError)));
}catch(reason){await writeFile('docs/verification/evidence/parallel-2026-10-05/raster-bounds-failure.json',JSON.stringify({error:String(reason),errors},null,2));throw reason;}finally{await browser.close();}
