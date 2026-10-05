import {evaluateBenchmark} from './browser-benchmark-guards.mjs';
import {chromium} from '@playwright/test';
import {writeFile} from 'node:fs/promises';
const browser=await chromium.launch({channel:'msedge',headless:true});const page=await browser.newPage();const errors=[];page.on('pageerror',e=>errors.push(e.message));page.on('console',m=>{if(m.type()==='error')errors.push(m.text());});
try{
await page.goto('http://127.0.0.1:5173');await page.waitForFunction(()=>window.gs);
const result=await evaluateBenchmark(page, async()=>{
    const canvas=document.createElement('canvas');canvas.width=64;canvas.height=64;document.body.append(canvas);
    const renderer=await window.gs.Renderer.create(canvas,16*2**20),Camera=window.gs.engine.camera.constructor,camera=new Camera();
    const x=3/Math.sqrt(50),y=4/Math.sqrt(50),z=5/Math.sqrt(50);
    const basis=[-.4886025119*y,.4886025119*z,-.4886025119*x,1.0925484306*x*y,-1.0925484306*y*z,.3153915653*(2*z*z-x*x-y*y),-1.0925484306*x*z,.5462742153*(x*x-y*y),-.5900435899*y*(3*x*x-y*y),2.8906114426*x*y*z,-.4570457995*y*(4*z*z-x*x-y*y),.3731763326*z*(2*z*z-3*x*x-3*y*y),-.4570457995*x*(4*z*z-x*x-y*y),1.4453057213*z*(x*x-y*y),-.5900435899*x*(x*x-3*y*y)];
    camera.setPose({position:[-3,-4,-5],target:[0,0,0],up:[0,1,0]});
    const rows=[];
    async function capture(point,degree=0){
        const stride=point.byteLength,scene={count:1,degree,stride,pageCapacity:1,pages:[point.buffer],origin:[0,0,0],min:[0,0,0],max:[0,0,0],maxScale:Math.max(...point.slice(4,7)),source:'pixel-fixture',decodeMs:0};
        const uploaded=await renderer.upload(scene,0,new AbortController().signal,()=>{});
        try{return await renderer.capture(uploaded,camera.frame(scene,64,64,1,degree,stride,1),camera.revision);}finally{renderer.release(uploaded);}
    }
    const make=stride=>{const p=new Float32Array(stride/4);p.set([.8,.8,.8],4);p[11]=1;p.set([.5,.4,.3,.5],12);return p;};
    for(let j=-1;j<15;j++){
        const degree=j<0?0:j<3?1:j<8?2:3,stride=[64,112,160,256][degree],point=make(stride);
        if(j>=0)point.set([.15,.10,-.12],16+3*j);
        const rgba=await capture(point,degree),center=Array.from(rgba.slice((32*64+32)*4,(32*64+32)*4+4));
        const variance=(64/(2*Math.tan(Math.PI/6))*.8/Math.sqrt(50))**2+.3,alpha=.5*Math.exp(-.25/variance);
        const expected=[.5,.4,.3].map((v,c)=>Math.round(255*alpha*(v+(j<0?0:[.15,.10,-.12][c]*basis[j]))));
        if(expected.some((v,i)=>Math.abs(v-center[i])>2)||center[3]!==255)throw Error(`SH ${j}, expected ${expected}, got ${center}`);
        rows.push({degree,coefficient:j,expected,actual:center});
    }
    camera.setPose({position:[0,0,5],target:[0,0,0],up:[0,1,0]});
    const point=make(64);point.set([.5,.1,.1],4);const angle=Math.PI/8;point.set([0,0,Math.sin(angle),Math.cos(angle)],8);
    const rgba=await capture(point);let sx=0,sy=0,sxx=0,syy=0,sxy=0,sum=0;
    for(let y=0;y<64;y++)for(let x=0;x<64;x++){const weight=rgba[(y*64+x)*4],dx=x+.5-32,dy=y+.5-32;sum+=weight;sx+=dx*weight;sy+=dy*weight;sxx+=dx*dx*weight;syy+=dy*dy*weight;sxy+=dx*dy*weight;}
    if(!Number.isFinite(sum)||sum<=0)throw Error('Blank anisotropic fixture');
    const cov={xx:sxx/sum-(sx/sum)**2,yy:syy/sum-(sy/sum)**2,xy:sxy/sum-sx*sy/(sum*sum)};
    if(!Object.values(cov).every(Number.isFinite) || Math.abs(cov.xx-cov.yy)>1 || cov.xy>-5)throw Error('Anisotropic rotated covariance failed '+JSON.stringify(cov));
    const clipped=make(64);clipped[2]=8;const black=await capture(clipped);if(black.some((v,i)=>i%4!==3&&v!==0))throw Error('Behind-camera culling failed');
    const singular=make(64);singular.set([1e-8,1e-8,1e-8],4);const small=await capture(singular);if(!small.some((v,i)=>i%4!==3&&v>0))throw Error('Subpixel preblur support missing');
    camera.setPose({position:[2,1,5],target:[0,0,0],up:[0,1,0]});
    const asym=make(64);asym.set([.35,.45,0],0);asym.set([.4,.15,.1],4);asym.set([0,0,Math.sin(angle),Math.cos(angle)],8);
    const reflected=asym.slice();reflected[1]=-reflected[1];reflected[8]=-reflected[8];reflected[10]=-reflected[10];
    const explicit=await capture(reflected);
    const canonical=camera.getPose();camera.setFlipY(true);
    const implicit=await capture(asym);
    const maxMirrorDifference=Math.max(...implicit.map((v,i)=>Math.abs(v-explicit[i])));
    if(maxMirrorDifference>1||JSON.stringify(camera.getPose())!==JSON.stringify(canonical))throw Error('Y reflection projection mismatch '+maxMirrorDifference);
    camera.orbit(20,10);camera.pan(4,3);if(!camera.flipY)throw Error('Interaction lost reflection');
    camera.setFlipY(false);
    await renderer.dispose();canvas.remove();return {sh:rows,anisotropy:cov,behindCamera:true,subpixelPreblur:true,yReflection:{maxMirrorDifference,canonicalPosePreserved:true}};
});
if(errors.length)throw Error(errors.join('\n'));await writeFile('docs/verification/evidence/image-tests.json',JSON.stringify({result,errors},null,2));console.log(JSON.stringify(result));
}finally{await browser.close();}
