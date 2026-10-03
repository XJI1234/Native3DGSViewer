import {mkdir,copyFile,readFile,writeFile} from 'node:fs/promises';
const dir='dist/assets/licenses';await mkdir(dir,{recursive:true});
for(const [name,path] of [['spz','../third_party/spz/LICENSE'],['zlib','../third_party/zlib/LICENSE'],['zstd','../third_party/zstd/LICENSE']])await copyFile(path,dir+'/'+name+'.txt');
await copyFile('THIRD-PARTY-NOTICES.md',dir+'/THIRD-PARTY-NOTICES.md');
const files=['dist/assets/decoder.wasm','dist/assets/decoder.mjs','dist/assets/decoder.worker.js'];
const {createHash}=await import('node:crypto');
await writeFile('dist/assets/manifest.json',JSON.stringify({emscripten:'6.0.11',memoryMaxBytes:1073741824,threads:1,simd:false,files:await Promise.all(files.map(async path=>({name:path.split('/').at(-1),sha256:createHash('sha256').update(await readFile(path)).digest('hex')})))},null,2));
