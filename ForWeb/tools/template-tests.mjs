import assert from 'node:assert/strict';
import { mkdir, mkdtemp, readFile, stat, writeFile } from 'node:fs/promises';
import { spawn, spawnSync } from 'node:child_process';
import { resolve } from 'node:path';
import { chromium } from '@playwright/test';
import { inputs, walk } from './template-build-inputs.mjs';
await mkdir('.local/template-tests', { recursive: true });
const output = await mkdtemp(resolve('.local/template-tests/run-'));
const modelRoot = process.env.GS_MODEL_ROOT;
if (!modelRoot) throw Error('GS_MODEL_ROOT is required for the external PLY/SPZ acceptance cases');
for (const name of ['changjin_v1.ply', 'spz/shengyi_v1.spz']) if (!(await stat(resolve(modelRoot,name))).isFile()) throw Error('Missing external fixture: '+name);
const names=['x','y','z','f_dc_0','f_dc_1','f_dc_2','opacity','scale_0','scale_1','scale_2','rot_0','rot_1','rot_2','rot_3'];
const header=Buffer.from('ply\nformat binary_little_endian 1.0\nelement vertex 3\n'+names.map(n=>'property float '+n+'\n').join('')+'end_header\n');
const rows=[[0,.6,0,1.5,-1.5,-1.5,3,-2.3,-2.8,-2.8,1,0,0,0],[-.5,-.6,0,-1.5,1.5,-1.5,3,-2.3,-2.8,-2.8,1,0,0,0],[.6,-.3,0,-1.5,-1.5,1.5,3,-2.3,-2.8,-2.8,1,0,0,0]];
const floats=new Float32Array(rows.flat());const fixture=Buffer.concat([header,Buffer.from(floats.buffer)]);
const records=[]; const browser=await chromium.launch({channel:'msedge',headless:true});
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const within=async(promise,ms,label)=>{
    let timer;
    try{return await Promise.race([promise,new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error(label)),ms);})]);}
    finally{clearTimeout(timer);}
};
const port=5185;
try {
for(const framework of ['react','vue']) for(const mode of ['production','development']) {
    const root=resolve('apps',framework+'-viewer');
    const sourceBefore=await inputs(root), outputsBefore=await walk(root+'/dist');
    const build=JSON.parse(await readFile('docs/verification/evidence/template-build-manifest.json','utf8'));
    const expected=build.records.find(record=>record.framework===framework);
    assert.ok(expected,'Missing enforced template build');
    assert.deepEqual(sourceBefore,expected.inputs);
    assert.deepEqual(outputsBefore,expected.outputs);
    const server=spawn(process.execPath,[resolve(root,'node_modules/vite/bin/vite.js'),...(mode==='production'?['preview']:[]),'--host','127.0.0.1','--port',String(port),'--strictPort'],{cwd:root,stdio:'pipe',windowsHide:true});
    let serverError='', serverExited=false;
    const serverClosed=new Promise(resolve=>{
        server.once('exit',()=>{serverExited=true;resolve();});
        server.once('error',error=>{serverExited=true;serverError=String(error);resolve();});
    });
    const serverReady=new Promise((resolve,reject)=>{
        let stdout='';const timer=setTimeout(()=>reject(Error('Vite readiness timeout')),10000);
        server.stdout.on('data',data=>{stdout+=data;if(stdout.includes('Local:')){clearTimeout(timer);resolve();}});
        serverClosed.then(()=>{clearTimeout(timer);reject(Error('Vite exited before readiness: '+serverError));});
    });
    serverReady.catch(()=>{});
    server.stderr.on('data',d=>{serverError+=d;});
    let context;
    try {
    context=await browser.newContext({viewport:{width:1440,height:900},deviceScaleFactor:1,acceptDownloads:true});
    const page=await context.newPage(),errors=[]; page.setDefaultTimeout(30000);
    page.on('pageerror',e=>errors.push(e.message));
    await page.addInitScript(()=>{
        if (!globalThis.GPUAdapter || !globalThis.GPUDevice) return;
        const live=new Set(),request=GPUAdapter.prototype.requestDevice,destroy=GPUDevice.prototype.destroy;
        GPUAdapter.prototype.requestDevice=async function(...args){const d=await request.apply(this,args);live.add(d);return d;};
        GPUDevice.prototype.destroy=function(){live.delete(this);return destroy.call(this);};
        window.templateDevices=()=>live.size;
        window.loseTemplateDevice=()=>{for(const d of live){d.dispatchEvent(new GPUUncapturedErrorEvent('uncapturederror',{error:new GPUValidationError('Injected template recovery fixture')}));d.destroy();}};
    });
        await serverReady;
        if(serverExited)throw Error('Vite exited: '+serverError);
        await page.goto(`http://127.0.0.1:${port}`);
        await page.getByRole('button',{name:'打开模型',exact:true}).waitFor();
        await page.waitForFunction(()=>!document.querySelector('.primary')?.disabled,undefined,{timeout:60000});
        assert.equal(await page.locator('canvas').count(),1);
        assert.equal(await page.evaluate(()=>crossOriginIsolated),true,'Template isolation headers missing');
        await page.screenshot({path:resolve(output,framework+'-'+mode+'-empty.png')});
        const input=page.getByLabel('选择模型文件');
        await input.setInputFiles({name:'asymmetric.ply',mimeType:'application/octet-stream',buffer:fixture});
        await page.waitForFunction(()=>document.querySelector('[data-testid=count]')?.textContent==='3');
        assert.ok((await page.getByTestId('decoder').textContent()).includes('single'),'Small model fallback missing');
        assert.ok((await page.getByTestId('sorting').textContent()).includes('adaptive'),'Adaptive config missing');
        assert.ok(!(await page.getByTestId('sorting').textContent()).includes('strict'),'Strict strategy unexpectedly selected');
        const pose=()=>page.getByLabel('相机位置').textContent();
        await page.waitForFunction(()=>document.querySelector('output')?.textContent?.startsWith('0.050,'));
        const initial=await pose();
        const canvas=page.locator('canvas');
        await canvas.screenshot({style:'.scene-caption{visibility:hidden}',path:resolve(output,framework+'-'+mode+'-normal.png')});
        await page.getByRole('button',{name:'翻转 Y',exact:true}).click();
        await page.waitForFunction(()=>document.querySelector('button[aria-pressed=true]')&&[...document.querySelectorAll('button')].some(b=>b.textContent?.trim()==='翻转 Y'&&b.getAttribute('aria-pressed')==='true'));
        await delay(150);assert.equal(await pose(),initial);
        await canvas.screenshot({style:'.scene-caption{visibility:hidden}',path:resolve(output,framework+'-'+mode+'-flipped.png')});
        const poses={};
        const drag=async(button,dx,dy)=>{
            const box=await canvas.boundingBox();assert.ok(box);
            await page.mouse.move(box.x+box.width/2,box.y+box.height/2);await page.mouse.down({button});
            await page.mouse.move(box.x+box.width/2+dx,box.y+box.height/2+dy,{steps:10});await page.mouse.up({button});await delay(200);
            return pose();
        };
        // SDK handles reflection, raw client deltas must give the same canonical result.
        poses.flippedOrbit=await drag('left',80,35);
        await page.getByRole('button',{name:'重置',exact:true}).click();await delay(150);
        await page.getByRole('button',{name:'翻转 Y',exact:true}).click();await delay(150);
        poses.normalOrbit=await drag('left',80,35);assert.equal(poses.flippedOrbit,poses.normalOrbit);assert.notEqual(poses.normalOrbit,initial);
        await page.getByRole('button',{name:'重置',exact:true}).click();await delay(150);
        poses.normalPan=await drag('right',35,-20);
        await page.getByRole('button',{name:'重置',exact:true}).click();await page.getByRole('button',{name:'翻转 Y',exact:true}).click();await delay(150);
        poses.flippedPan=await drag('right',35,-20);assert.equal(poses.flippedPan,poses.normalPan);
        await page.getByRole('button',{name:'适配',exact:true}).click();await delay(150);assert.equal(await pose(),initial);
        await page.getByRole('button',{name:'放大模型',exact:true}).click();await delay(150);assert.notEqual(await pose(),initial);
        await page.getByRole('button',{name:'重置',exact:true}).click();await delay(150);
        await page.getByRole('button',{name:'自由',exact:true}).click();await canvas.click();
        await page.waitForFunction(()=>document.pointerLockElement?.tagName==='CANVAS');
        await page.keyboard.down('KeyW');await delay(80);await page.keyboard.up('KeyW');await delay(80);
        assert.equal(await page.evaluate(()=>document.pointerLockElement?.tagName),'CANVAS');
        assert.notEqual(await pose(),initial);
        await page.keyboard.press('Escape');await page.waitForFunction(()=>!document.pointerLockElement);
        await page.getByRole('button',{name:'固定',exact:true}).click();
        await page.getByRole('button',{name:'重置',exact:true}).click();
        await delay(150);await canvas.screenshot({style:'.scene-caption{visibility:hidden}',path:resolve(output,framework+'-'+mode+'-before-capture.png')});
        const downloadPromise=page.waitForEvent('download');await page.getByRole('button',{name:'保存截图',exact:true}).click();
        const download=await downloadPromise;await download.saveAs(resolve(output,framework+'-'+mode+'-capture.png'));assert.equal(await download.failure(),null);
        const lateFixture=Buffer.concat([Buffer.from(header.toString().replace('vertex 3','vertex 1')),Buffer.from(new Float32Array(rows[0]).buffer)]);
        let slowComplete,slowStart;const slowDone=new Promise(resolve=>{slowComplete=resolve;});const slowStarted=new Promise(resolve=>{slowStart=resolve;});
        await canvas.screenshot({style:'.scene-caption{visibility:hidden}',path:resolve(output,framework+'-'+mode+'-before-cancel.png')});
        await page.route('**/slow.ply',async route=>{slowStart();await delay(1500);try{await route.fulfill({body:lateFixture,contentType:'application/octet-stream'});}catch{}finally{slowComplete();}});
        await page.getByLabel('远程模型地址').fill(`http://127.0.0.1:${port}/slow.ply`);await page.getByRole('button',{name:'加载地址',exact:true}).click();
        await page.waitForFunction(()=>!Array.from(document.querySelectorAll('button')).find(b=>b.textContent?.trim()==='取消加载')?.disabled);
        await within(slowStarted,10000,'Slow fixture route did not start');
        await page.getByRole('button',{name:'取消加载',exact:true}).click();await within(slowDone,10000,'Slow fixture response did not settle');await delay(200);
        await canvas.screenshot({style:'.scene-caption{visibility:hidden}',path:resolve(output,framework+'-'+mode+'-after-cancel.png')});
        assert.ok((await page.locator('.model-name').textContent()).includes('asymmetric.ply'));
        assert.equal(await page.getByTestId('count').textContent(),'3');
        await page.route('**/missing.ply',r=>r.fulfill({status:404,body:'missing'}));
        await page.getByLabel('远程模型地址').fill(`http://127.0.0.1:${port}/missing.ply`);await page.getByRole('button',{name:'加载地址',exact:true}).click();await page.getByRole('alert').waitFor();
        assert.equal(await page.getByTestId('count').textContent(),'3');
        await page.route('**/valid.ply',r=>r.fulfill({body:fixture,contentType:'application/octet-stream'}));
        await page.getByLabel('远程模型地址').fill(`http://127.0.0.1:${port}/valid.ply`);await page.getByRole('button',{name:'加载地址',exact:true}).click();
        await page.getByRole('alert').waitFor({state:'hidden'});await delay(100);
        await input.setInputFiles({name:'invalid.txt',mimeType:'text/plain',buffer:Buffer.from('invalid')});
        await page.getByRole('alert').waitFor();
        await page.evaluate(()=>window.loseTemplateDevice());await page.getByRole('button',{name:'恢复设备',exact:true}).waitFor();
        assert.ok((await page.getByRole('alert').textContent()).includes('Injected template recovery fixture'));
        await page.getByRole('button',{name:'恢复设备',exact:true}).click();await page.waitForFunction(()=>document.querySelector('[role=status]')?.textContent?.trim()==='可以浏览',undefined,{timeout:60000});
        assert.equal(await page.getByRole('button',{name:'翻转 Y',exact:true}).getAttribute('aria-pressed'),'true');
        assert.equal(await page.getByTestId('count').textContent(),'3');
        await page.getByRole('button',{name:'重置',exact:true}).click();await delay(150);
        await canvas.screenshot({style:'.scene-caption{visibility:hidden}',path:resolve(output,framework+'-'+mode+'-recovered.png')});
        console.log('Recovered',framework,mode);
        if(mode==='production') {
            await input.setInputFiles(resolve(modelRoot,'changjin_v1.ply'));
            await page.waitForFunction(()=>document.querySelector('[data-testid=count]')?.textContent==='170,799',undefined,{timeout:120000});
            await page.screenshot({path:resolve(output,framework+'-loaded-1440.png')});
            await input.setInputFiles(resolve(modelRoot,'spz/shengyi_v1.spz'));
            await page.waitForFunction(()=>document.querySelector('[data-testid=count]')?.textContent==='804,758',undefined,{timeout:120000});
            assert.ok((await page.getByTestId('decoder').textContent()).includes('pthreads'),'Automatic pthreads missing');
        }
        const layouts=[];
        for(const width of [320,768,1024,1440]) {
            await page.setViewportSize({width,height:900});await delay(100);
            const overflow=await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth);assert.equal(overflow,false,`Horizontal overflow at ${width}`);
            layouts.push({width,horizontalOverflow:overflow});await page.screenshot({path:resolve(output,framework+'-'+mode+'-'+width+'.png'),fullPage:true});
        }
        await page.getByRole('button',{name:'隐藏信息',exact:true}).click();assert.equal(await page.locator('#inspector').isVisible(),false);
        await page.getByRole('button',{name:'关闭模型',exact:true}).click();await page.waitForFunction(()=>document.querySelector('[data-testid=count]')?.textContent==='0');
        if(mode==='development') {
            await page.evaluate(async framework=>{const module=await import(framework==='react'?'/src/main.tsx':'/src/main.ts');module.unmount();},framework);
            await page.waitForFunction(()=>window.templateDevices()===0&&document.querySelectorAll('canvas').length===0);
        }
        assert.deepEqual(errors,[]);
        assert.deepEqual(await inputs(root),sourceBefore,'Source changed during browser test');
        assert.deepEqual(await walk(root+'/dist'),outputsBefore,'Dist changed during browser test');
        records.push({framework,mode,sources:sourceBefore,outputs:outputsBefore,poses,layouts,actualYReflection:true,faultInjection:'uncaptured validation error + device destruction, not hardware loss',cancelPreservedScene:true,urlFailurePreservedScene:true,recovery:true,realPlySpz:mode==='production',crossOriginIsolated:true,smallModelSingleFallback:true,adaptiveSorting:true,automaticPthreads:mode==='production',unmount:mode==='development',errors});
        console.log('PASS',framework,mode);
    } finally { try { await context?.close(); } finally { if(!serverExited)server.kill();
        try { await within(serverClosed,5000,'Vite shutdown timeout'); }
        catch { if(!serverExited)server.kill('SIGKILL'); await within(serverClosed,5000,'Vite did not terminate after SIGKILL'); } } }
}
} finally {await browser.close();}
const result=spawnSync('python',['-E','-',output],{encoding:'utf8',input:`import sys,json,pathlib
from PIL import Image,ImageChops
root=pathlib.Path(sys.argv[1]);rows=[]
for p in root.glob('*-normal.png'):
 q=p.with_name(p.name.replace('-normal.png','-flipped.png'))
 a=Image.open(p).convert('RGB');b=Image.open(q).convert('RGB')
 def red(im):
  pts=[(x,y) for y in range(im.height) for x in range(im.width) if (lambda c:c[0]>50 and c[0]>c[1]*1.5 and c[0]>c[2]*1.5)(im.getpixel((x,y)))]
  assert len(pts)>10,'missing red feature'
  return sum(x for x,y in pts)/len(pts),sum(y for x,y in pts)/len(pts)
 x,y=red(a);u,v=red(b)
 assert abs(x-u)<2 and abs(y+v-(a.height-1))<2,(p.name,x,y,u,v,a.size)
 rows.append(dict(file=p.name,redBefore=[x,y],redAfter=[u,v],height=a.height))
for p in root.glob('*-capture.png'):
 if p.name.endswith('-before-capture.png'):continue
 ref=p.with_name(p.name.replace('-capture.png','-before-capture.png'));a=Image.open(ref).convert('RGB');b=Image.open(p).convert('RGB');assert a.size==b.size
 delta=ImageChops.difference(a,b);assert max(hi for lo,hi in delta.getextrema())<=2,(p.name,'capture RGB pixel mismatch')
for suffix in ['-recovered.png','-after-cancel.png']:
 for p in root.glob('*'+suffix):
  ref=p.with_name(p.name.replace(suffix,'-flipped.png' if suffix=='-recovered.png' else '-before-cancel.png'));a=Image.open(ref).convert('RGB');b=Image.open(p).convert('RGB');assert a.size==b.size
  delta=ImageChops.difference(a,b);assert max(hi for lo,hi in delta.getextrema())<=2,(p.name,'recovery/cancel RGB pixel mismatch')
print(json.dumps(rows))
`});
if(result.status!==0)throw Error(result.stderr||result.stdout);
const reflectionPixels=JSON.parse(result.stdout);
await writeFile('docs/verification/evidence/template-tests.json',JSON.stringify({browser:browser.version(),installedSdk:true,screenshotsDirectory:output,records,reflectionPixels},null,2)+'\n');
