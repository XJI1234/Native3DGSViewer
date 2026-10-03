import {readdir,readFile} from 'node:fs/promises';
import {join} from 'node:path';
async function files(path){const entries=await readdir(path,{withFileTypes:true});return (await Promise.all(entries.map(e=>e.isDirectory()?files(join(path,e.name)):[join(path,e.name)]))).flat();}
for(const path of await files('src')){
    const source=await readFile(path,'utf8');
    if(/(?:model-io|render-core|splat-types)[\\/]/.test(path)&&/from ['"](?:react|vue)|from ['"].*engine\//.test(source))throw Error(`Forbidden dependency ${path}`);
    if(/model-io[\\/]/.test(path)&&/from ['"].*render-core/.test(source))throw Error(`Decoder depends on renderer ${path}`);
    if(/render-core[\\/]/.test(path)&&/from ['"].*model-io/.test(source))throw Error(`Renderer depends on decoder ${path}`);
}
console.log('PASS: provider dependency boundaries');
