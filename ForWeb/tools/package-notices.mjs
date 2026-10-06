import {mkdir,copyFile,readFile,writeFile} from 'node:fs/promises';
const dir='dist/assets/licenses';await mkdir(dir,{recursive:true});
for(const [name,path] of [['spz','../third_party/spz/LICENSE'],['zlib','../third_party/zlib/LICENSE'],['zstd','../third_party/zstd/LICENSE']])await copyFile(path,dir+'/'+name+'.txt');
await copyFile('THIRD-PARTY-NOTICES.md',dir+'/THIRD-PARTY-NOTICES.md');
const files=['decoder.wasm','decoder.mjs','decoder.worker.js','threaded/decoder.wasm','threaded/decoder.mjs'];
const {createHash}=await import('node:crypto');
await writeFile('dist/assets/manifest.json',JSON.stringify({emscripten:'6.0.11',memoryMaxBytes:1073741824,threads:1,parallel:{directory:'threaded/',maxThreads:8,requiresCrossOriginIsolation:true},simd:false,files:await Promise.all(files.map(async name=>({name,sha256:createHash('sha256').update(await readFile('dist/assets/'+name)).digest('hex')})))},null,2));
