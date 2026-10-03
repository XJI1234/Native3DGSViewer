import {mkdir,writeFile,readFile,cp} from 'node:fs/promises';
import {spawn,spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
import assert from 'node:assert/strict';
import {chromium} from '@playwright/test';
const root=resolve('.local/sdk-consumer');await mkdir(root,{recursive:true});
const run=(cmd,args,cwd)=>{const r=spawnSync(cmd,args,{cwd,stdio:'inherit',shell:process.platform==='win32'});if(r.status!==0)throw Error(cmd+' failed');};
const archive='sdk-'+Date.now()+'.tgz';
run('pnpm',['pack','--out','.local/'+archive],resolve('.'));
await writeFile(root+'/package.json',JSON.stringify({private:true,type:'module',dependencies:{'@native3dgs/web':'file:../'+archive,react:'19.3.0','react-dom':'19.3.0',vue:'3.5.43'},devDependencies:{vite:'8.3.2',typescript:'6.0.3','@webgpu/types':'0.1.74','@types/react':'19.3.0','@types/react-dom':'19.3.0','@vitejs/plugin-vue':'6.0.9','vue-tsc':'3.3.12'}},null,2));
run('pnpm',['install','--ignore-scripts'],root);
await cp('dist/assets',root+'/public/gs-assets',{recursive:true});
for(const [document,language,target] of [['React-integration.md','tsx','DocumentedReact.tsx'],['Vue-integration.md','vue','DocumentedVue.vue']]){
    const markdown=await readFile('docs/'+document,'utf8');
    const code=markdown.split('```'+language+'\n')[1]?.split('\n```')[0];
    if(!code)throw Error('Missing documented component: '+document);
    await writeFile(root+'/'+target,code);
}
await writeFile(root+'/vite.config.ts',"import {defineConfig} from 'vite';import vue from '@vitejs/plugin-vue';export default defineConfig({plugins:[vue()]});");
await writeFile(root+'/index.html','<html><link rel="icon" href="data:,"><div id="react"></div><div id="vue"></div><script type="module" src="/main.ts"></script></html>');
await writeFile(root+'/main.ts',`
import {createElement as h,StrictMode,useRef} from 'react';
import {createRoot} from 'react-dom/client';
import {createApp,defineComponent,h as vh,ref} from 'vue';
import {createEngine} from '@native3dgs/web';
import {useNative3DGS as useReact} from '@native3dgs/web/react';
import {useNative3DGS as useVue} from '@native3dgs/web/vue';
import {Native3DGSViewer as DocumentedReact} from './DocumentedReact';
import DocumentedVue from './DocumentedVue.vue';
const options={assets:{baseUrl:new URL('/gs-assets/',location.href),workerUrl:new URL('/gs-assets/decoder.worker.js',location.href)},pixelRatio:1};
const w=window as any;
w.documentedComponents=[DocumentedReact,DocumentedVue];
function ReactViewer(){const host=useRef<HTMLDivElement>(null);const state=useReact(host,options);w.reactState=state;return h('div',{ref:host,style:{width:'320px',height:'240px'}});}
const root=createRoot(document.querySelector('#react')!);root.render(h(StrictMode,null,h(ReactViewer)));
const app=createApp(defineComponent({setup(){const host=ref<HTMLElement|null>(null),state=useVue(host,options);w.vueState=state;return()=>vh('div',{ref:host,style:{width:'320px',height:'240px'}});}}));app.mount('#vue');
w.unmount=()=>{root.unmount();app.unmount();};w.createEngine=createEngine;
`);
await writeFile(root+'/tsconfig.json',JSON.stringify({compilerOptions:{target:'ES2022',module:'ESNext',moduleResolution:'Bundler',jsx:'react-jsx',strict:true,skipLibCheck:true,noEmit:true,types:['@webgpu/types','vite/client']},include:['main.ts','*.tsx','*.vue']}));
run('pnpm',['exec','vue-tsc','--noEmit'],root);
// Verify server imports do not touch window/document/navigator.
await writeFile(root+'/ssr.mjs',"await import('@native3dgs/web');await import('@native3dgs/web/react');await import('@native3dgs/web/vue');console.log('SSR imports passed');");run('node',['ssr.mjs'],root);
run('pnpm',['exec','vite','build'],root);
const browser=await chromium.launch({channel:'msedge',headless:true});const records=[];
try{
for(const mode of ['preview','development']){
const server=spawn(process.execPath,[resolve(root,'node_modules/vite/bin/vite.js'),...(mode==='preview'?['preview']:[]),'--host','127.0.0.1','--port','5174','--strictPort'],{cwd:root,stdio:'pipe',windowsHide:true});
server.stderr.on('data',d=>process.stderr.write(d));
const page=await browser.newPage();const errors=[];page.on('pageerror',e=>errors.push(e.message));
await page.addInitScript(()=>{
    if(!globalThis.GPUAdapter||!globalThis.GPUDevice)return;
    const devices=new Set(),request=GPUAdapter.prototype.requestDevice,destroy=GPUDevice.prototype.destroy;let created=0;
    GPUAdapter.prototype.requestDevice=async function(...args){const device=await request.apply(this,args);created++;devices.add(device);return device;};
    GPUDevice.prototype.destroy=function(){devices.delete(this);return destroy.call(this);};
    window.deviceCounts=()=>({created,active:devices.size});
});
try{
    let connected=false;for(let attempt=0;attempt<30&&!connected;attempt++){try{await page.goto('http://127.0.0.1:5174');connected=true;}catch{await new Promise(r=>setTimeout(r,100));}}
    if(!connected)throw Error('Consumer server unavailable');
    await page.waitForFunction(()=>window.reactState?.engine&&window.vueState?.engine.value,{timeout:60000});
    await page.waitForFunction(expected=>window.deviceCounts().created===expected&&window.deviceCounts().active===2,mode==='preview'?2:3);
    const result=await page.evaluate(async()=>{
        const r=window.reactState.engine,v=window.vueState.engine.value;
        const names=['x','y','z','f_dc_0','f_dc_1','f_dc_2','opacity','scale_0','scale_1','scale_2','rot_0','rot_1','rot_2','rot_3'];const header=new TextEncoder().encode('ply\nformat binary_little_endian 1.0\nelement vertex 1\n'+names.map(n=>'property float '+n+'\n').join('')+'end_header\n');
        const source={kind:'blob',blob:new Blob([header,new Float32Array([0,0,0,0,0,0,0,-2,-2,-2,1,0,0,0])])};
        const results=await Promise.all([r.open(source).result,v.open(source).result]);if(results.some(r=>!r.ok))throw Error(JSON.stringify(results));
        const canvasCount=document.querySelectorAll('canvas').length;window.unmount();await Promise.all([r.dispose(),v.dispose()]);
        return {results,canvasCount,remainingCanvases:document.querySelectorAll('canvas').length,react:r.getSnapshot().phase,vue:v.getSnapshot().phase,devices:window.deviceCounts()};
    });
    assert.equal(result.canvasCount,2);assert.equal(result.remainingCanvases,0);assert.equal(result.react,'Stopped');assert.equal(result.vue,'Stopped');assert.deepEqual(errors,[]);
    assert.equal(result.devices.active,0);records.push({mode,result,errors});console.log(JSON.stringify({mode,result}));
}finally{await page.close();server.kill();await new Promise(resolve=>server.once('exit',resolve));}
}
await writeFile('docs/verification/evidence/sdk-consumer.json',JSON.stringify({records,browser:browser.version(),ssr:true,installedTarball:true,productionBundle:true,developmentStrictMode:true,documentedTsxAndSfcCompiled:true},null,2));
}finally{await browser.close();}
