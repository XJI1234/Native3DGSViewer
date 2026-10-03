import {readFile,writeFile,readdir,mkdir} from 'node:fs/promises';
import {createHash} from 'node:crypto';
async function walk(directory){const entries=await readdir(directory,{withFileTypes:true});return (await Promise.all(entries.map(entry=>entry.isDirectory()?walk(directory+'/'+entry.name):directory+'/'+entry.name))).flat();}
const inputs=[...await walk('src'),...await walk('native'),'pnpm-lock.yaml','package.json','vite.config.ts','tsconfig.json','../src/model-io/common/probe.cpp','../src/model-io/common/probe.h','../src/model-io/normalize/normalize.cpp','../src/model-io/normalize/normalize.h'];
const assets=JSON.parse(await readFile('dist/assets/manifest.json','utf8'));
const files=await Promise.all(inputs.map(async path=>({path,sha256:createHash('sha256').update(await readFile(path)).digest('hex')})));
await mkdir('docs/verification/evidence',{recursive:true});
await writeFile('docs/verification/evidence/build-manifest.json',JSON.stringify({recordedAt:new Date().toISOString(),node:process.versions.node,package:JSON.parse(await readFile('package.json','utf8')),assets,files},null,2));
console.log('Recorded '+files.length+' source/config hashes and '+assets.files.length+' deployed asset hashes');
