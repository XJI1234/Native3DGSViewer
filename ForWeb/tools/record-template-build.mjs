import { writeFile } from 'node:fs/promises';
import { execFileSync, spawnSync } from 'node:child_process';
import { inputs, walk } from './template-build-inputs.mjs';
import { resolve } from 'node:path';
const pnpmScript = process.env.GS_PNPM_CLI ?? resolve(process.env.APPDATA ?? '', 'npm/node_modules/pnpm/bin/pnpm.mjs');
const records=[];
for(const framework of ['react','vue']) {
    const root='apps/'+framework+'-viewer';
    const before = await inputs(root);
    for (const args of [['install','--frozen-lockfile','--ignore-scripts'],['run','test'],['run','format:check'],['run','build']]) {
        const result = spawnSync(process.execPath,[pnpmScript,...args],{cwd:resolve(root),stdio:'inherit',windowsHide:true});
        if (result.status !== 0) throw Error('Template verification failed: ' + framework + ' ' + args.join(' '));
    }
    const after = await inputs(root);
    if (JSON.stringify(before) !== JSON.stringify(after)) throw Error('Template input changed during verification: '+framework);
    records.push({framework,root,inputs:after,outputs:await walk(root+'/dist')});
}
await writeFile('docs/verification/evidence/template-build-manifest.json',JSON.stringify({schema:1,inputState:'working tree hashes; packaging requires matching clean committed inputs',baseCommit:execFileSync('git',['rev-parse','HEAD'],{encoding:'utf8'}).trim(),node:process.versions.node,records},null,2)+'\n');
console.log('Recorded both installed-SDK template input and production output hashes');
