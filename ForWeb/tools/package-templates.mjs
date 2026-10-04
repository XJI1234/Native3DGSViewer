import { readFile, writeFile, mkdir, mkdtemp, rename, rm } from 'node:fs/promises';
import { execFileSync, spawnSync } from 'node:child_process';
import { resolve, sep } from 'node:path';
import { hash, inputs, walk } from './template-build-inputs.mjs';
const git=(...args)=>execFileSync('git',args,{encoding:'utf8'}).trim();
const build=JSON.parse(await readFile('docs/verification/evidence/template-build-manifest.json','utf8'));
const sdkArchive=resolve('.local/release/Native3DGS-SDK-0.2.2-Web-WebGPU-preview.2.zip');
const sdkRead=spawnSync('python',['-E','-',sdkArchive],{encoding:'utf8',input:`import json,sys,zipfile,hashlib
with zipfile.ZipFile(sys.argv[1]) as z:
 assert z.testzip() is None
 m=json.loads(z.read('MANIFEST.json'))
 for f in m['files']:
  assert hashlib.sha256(z.read(f['name'])).hexdigest()==f['sha256']
 print(json.dumps(dict(manifest=m,tarballSha256=hashlib.sha256(z.read('sdk/native3dgs-web-0.2.2-preview.2.tgz')).hexdigest())))
`});
if(sdkRead.status!==0)throw Error(sdkRead.stderr);
const sdk=JSON.parse(sdkRead.stdout);
if(sdk.manifest.version!=='0.2.2-preview.2')throw Error('Unexpected SDK version');
await mkdir('.local/release',{recursive:true});
for(const record of build.records) {
    if(git('status','--porcelain','--',record.root))throw Error('Commit template before packaging: '+record.root);
    for (const [current, expected, kind] of [[await inputs(record.root), record.inputs, 'inputs'], [await walk(record.root + '/dist'), record.outputs, 'outputs']]) {
        const sorted = expected.slice().sort((a,b) => a.path.localeCompare(b.path));
        if (JSON.stringify(current) !== JSON.stringify(sorted)) throw Error('Template build inventory/hash mismatch: ' + kind);
    }
    const pkg=JSON.parse(await readFile(record.root+'/package.json','utf8'));
    if(pkg.version!==sdk.manifest.version||pkg.dependencies['@native3dgs/web']!=='file:vendor/native3dgs-web-0.2.2-preview.2.tgz')throw Error('Template/SDK version mismatch');
    if(hash(await readFile(record.root+'/vendor/native3dgs-web-0.2.2-preview.2.tgz'))!==sdk.tarballSha256)throw Error('Template SDK differs from released SDK');
    const files=git('ls-files','--',record.root).split('\n').filter(Boolean);
    const relativeFiles = files.map(path => path.slice(record.root.length + 1));
    for (const required of ['vendor/native3dgs-web-0.2.2-preview.2.tgz', 'pnpm-lock.yaml']) {
        if (!relativeFiles.includes(required)) throw Error('Required tracked source missing: ' + required);
    }
    const rootFiles = new Set(['.gitignore', '.gitattributes', 'README.md', 'package.json', 'pnpm-lock.yaml', 'tsconfig.json', 'vite.config.ts', 'index.html']);
    for (const path of relativeFiles) {
        if (rootFiles.has(path) || /^src\/.*\.(ts|tsx|vue|css)$/.test(path) || path === 'scripts/copy-sdk-assets.mjs' || path === 'vendor/native3dgs-web-0.2.2-preview.2.tgz') continue;
        throw Error('File outside template source allowlist: ' + path);
    }
    const manifest={schema:1,framework:record.framework,version:pkg.version,attachedRelease:'v0.2.2',sourceCommit:git('rev-parse','HEAD'),sdkSourceCommit:sdk.manifest.sourceCommit,sdkArchiveSha256:hash(await readFile(sdkArchive)),sdkTarballSha256:sdk.tarballSha256,files:await Promise.all(files.map(async path=>({path:path.slice(record.root.length+1),sha256:hash(await readFile(path))})))};
    const title=record.framework==='react'?'React':'Vue';
    const archive=resolve('.local/release',`Native3DGS-Template-0.2.2-Web-${title}-preview.2.zip`);
    const releaseRoot=resolve('.local/release');
    const staging=await mkdtemp(resolve(releaseRoot,'template-stage-'));
    if (!resolve(staging).startsWith(releaseRoot+sep)) throw Error('Staging path escaped release directory');
    const stagedArchive=resolve(staging,'template.zip');
    try {
    const script=`import json,sys,pathlib,zipfile,hashlib
m=json.loads(sys.argv[3]);root=pathlib.Path(sys.argv[1])
with zipfile.ZipFile(sys.argv[2],'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for f in m['files']:
  p=root/f['path'];data=p.read_bytes();assert hashlib.sha256(data).hexdigest()==f['sha256'];z.writestr(f['path'],data)
 z.writestr('MANIFEST.json',json.dumps(m,indent=2)+chr(10))
with zipfile.ZipFile(sys.argv[2]) as z: assert z.testzip() is None
`;
    const packed=spawnSync('python',['-E','-',resolve(record.root),stagedArchive,JSON.stringify(manifest)],{encoding:'utf8',input:script});
    if(packed.status!==0)throw Error(packed.stderr);
    const sha256=hash(await readFile(stagedArchive));await writeFile(stagedArchive+'.sha256',`${sha256}  ${archive.split(/[\\/]/).pop()}\n`);
    await rename(stagedArchive,archive);
    await rename(stagedArchive+'.sha256',archive+'.sha256');
    console.log(JSON.stringify({archive,sha256,sourceCommit:manifest.sourceCommit,sdkSourceCommit:manifest.sdkSourceCommit}));
    } finally { await rm(staging,{recursive:true,force:true}); }
}
