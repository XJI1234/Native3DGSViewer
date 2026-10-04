import {execFileSync, spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {cp, mkdir, mkdtemp, readFile, readdir, writeFile} from 'node:fs/promises';
import {resolve} from 'node:path';

// Release assets attached to an older native tag identify their actual Web source commit.
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const packageInfo = JSON.parse(await readFile('package.json', 'utf8'));
const tag = process.env.GS_RELEASE_TAG ?? 'v0.2.2';
if (!/^v\d+\.\d+\.\d+$/.test(tag)) throw Error('Invalid native release tag');
const git = (...args) => execFileSync('git', args, {encoding: 'utf8'}).trim();
if (git('-C', '..', 'status', '--porcelain', '--', 'ForWeb')) throw Error('Commit ForWeb before packaging');
if (git('-C','..','status','--porcelain','--','src/model-io','include/splat-types','include/model-io','third_party')) throw Error('Dirty shared native inputs');
const dependencyState=git('submodule','status','--recursive');
if (dependencyState.split('\n').filter(line=>/third_party\/(spz|zlib|zstd)(?:\s|$)/.test(line)).some(line=>/^[+-U]/.test(line))) throw Error('Dependency differs from committed gitlink');
if (git('submodule', 'foreach', '--recursive', '--quiet', 'git status --porcelain')) throw Error('Dirty dependency source');
const build = JSON.parse(await readFile('docs/verification/evidence/build-manifest.json', 'utf8'));
if (build.git.dependencySourcesClean !== true) throw Error('Dependency build cleanliness not verified');
const inventory=await walk('dist');
const expectedOutputs=build.outputs.map(file=>file.path.slice(5)).sort();
if(JSON.stringify(inventory.map(file=>file.name).sort())!==JSON.stringify(expectedOutputs))throw Error('Unrecorded/missing build output');
if (build.git.dependencies.trim() !== dependencyState) throw Error('Build dependency revision mismatch');
for (const input of [...build.files,...build.outputs]) {
    const current = createHash('sha256').update(await readFile(input.path)).digest('hex');
    if (current !== input.sha256) throw Error(`Stale build input: ${input.path}`);
}
const commit = git('rev-parse', 'HEAD');
const archive = `Native3DGS-SDK-${tag.slice(1)}-Web-WebGPU-preview.zip`;
const output = resolve('.local/release');
await mkdir(output,{recursive:true});
const contents = await mkdtemp(output + '/web-');
await mkdir(contents + '/sdk');
const tarball = `native3dgs-web-${packageInfo.version}.tgz`;
const pnpmScript=process.env.GS_PNPM_CLI ?? resolve(process.env.APPDATA ?? '', 'npm/node_modules/pnpm/bin/pnpm.mjs');
const pack = spawnSync(process.execPath,[pnpmScript,'pack','--out',contents+'/sdk/'+tarball],
    {stdio:'inherit',windowsHide:true});
if (pack.status !== 0) throw Error('pnpm pack failed');
await cp('dist/assets', contents + '/assets', {recursive: true});
await cp('docs', contents + '/docs', {recursive: true, filter: path => !path.includes('verification/evidence') && !path.includes('verification\\evidence')});
for (const name of ['CHANGELOG.md', 'THIRD-PARTY-NOTICES.md']) await cp(name, contents + '/' + name);
await writeFile(contents + '/README.md', `# Native3DGS Web SDK preview\n\nPackage: ${packageInfo.name}@${packageInfo.version}\nSource: https://github.com/XJI1234/Native3DGSViewer/commit/${commit}\nAttached native release: ${tag}\n\nThis Web preview is built from the source commit above, not from the older native release tag. The Web PR has not been merged by this packaging operation.\n\nInstall sdk/${tarball} with pnpm add <local-path>. Copy assets/ into your application's public/gs-assets/ and configure both assets.baseUrl and assets.workerUrl. See docs/SDK-guide.md, docs/React-integration.md and docs/Vue-integration.md. Models and SparkJS are not distributed.\n\nRequires HTTPS or localhost, WebGPU, sufficient device limits and OPFS storage. Full precision scenes preserve all points and SH; resource limits return explicit errors. Only Windows/Edge/RTX3080 has been measured for this preview.\n\nThird-party decoder licenses are in assets/licenses/. No additional license for repository-owned code is granted by this archive; repository owner terms apply.\n`);
async function walk(directory, prefix = '') {
    const files = [];
    for (const entry of await readdir(directory, {withFileTypes: true})) {
        const name = prefix + entry.name;
        if (entry.isDirectory()) files.push(...await walk(directory + '/' + entry.name, name + '/'));
        else files.push({name, sha256: hash(await readFile(directory + '/' + entry.name))});
    }
    return files.sort((a, b) => a.name.localeCompare(b.name));
}
const manifest = {schema: 1, package: packageInfo.name, version: packageInfo.version, preview: true,
    attachedRelease: tag, sourceCommit: commit, sourceTree: git('rev-parse', 'HEAD:ForWeb'),
    sourceUrl: `https://github.com/XJI1234/Native3DGSViewer/commit/${commit}`,
    toolchain: {node: process.versions.node, emscripten: '6.0.11'},
    dependencies: git('submodule', 'status', '--recursive'),
    assetManifest: JSON.parse(await readFile('dist/assets/manifest.json', 'utf8')),
    files: await walk(contents)};
await writeFile(contents + '/MANIFEST.json', JSON.stringify(manifest, null, 2) + '\n');
// Zip with Python's standard library; no additional packaging dependency or shell expansion.
const result = spawnSync('python', ['-', contents, output + '/' + archive], {encoding: 'utf8', input:
`import pathlib,sys,zipfile
root=pathlib.Path(sys.argv[1])
with zipfile.ZipFile(sys.argv[2],'w',zipfile.ZIP_DEFLATED,compresslevel=9) as out:
    for path in sorted(root.rglob('*')):
        if path.is_file(): out.write(path,path.relative_to(root).as_posix())
with zipfile.ZipFile(sys.argv[2]) as check:
    assert check.testzip() is None
`});
if (result.status !== 0) throw Error(result.stderr);
const digest = hash(await readFile(output + '/' + archive));
await writeFile(output + '/' + archive + '.sha256', `${digest}  ${archive}\n`);
console.log(JSON.stringify({archive: output + '/' + archive, sha256: digest, sourceCommit: commit, version: packageInfo.version}));
