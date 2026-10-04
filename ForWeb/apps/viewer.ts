import {createEngine} from '../src/index';
import {bindCanvas} from '../src/web-adapters/dom';
import {testSort,benchmarkSort} from '../src/render-core/sort';
import {Renderer} from '../src/render-core/renderer';
const canvas=document.querySelector<HTMLCanvasElement>('#canvas')!;
const status=document.querySelector<HTMLElement>('#status')!;
const initialized=await createEngine({canvas,assets:{baseUrl:new URL('/assets/',location.href)}});
if(!initialized.ok){status.textContent=JSON.stringify(initialized.error);throw Error(initialized.error.diagnostic);}
const engine=initialized.value;bindCanvas(engine,canvas,canvas,1);
engine.subscribe(()=>{status.textContent=JSON.stringify(engine.getSnapshot(),null,0);});
status.textContent=JSON.stringify(engine.capabilities);
let operation:ReturnType<typeof engine.open>|undefined;
document.querySelector<HTMLInputElement>('#file')!.onchange=async event=>{const file=(event.target as HTMLInputElement).files?.[0];if(file){operation=engine.open({kind:'blob',blob:file,name:file.name});await operation.result;}};
document.querySelector<HTMLButtonElement>('#cancel')!.onclick=()=>operation?.cancel();
document.querySelector<HTMLButtonElement>('#reset')!.onclick=()=>{engine.camera.reset();engine.requestFrame();};
document.querySelector<HTMLButtonElement>('#close')!.onclick=()=>void engine.closeScene();
const global=window as unknown as {gs:unknown};
global.gs={engine,createEngine,Renderer,testSort,benchmarkSort};
