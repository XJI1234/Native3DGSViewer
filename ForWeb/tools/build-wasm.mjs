import { spawnSync } from 'node:child_process';
import { resolve, join } from 'node:path';
import { mkdirSync, copyFileSync, existsSync,readFileSync } from 'node:fs';
const root = resolve(import.meta.dirname, '..');
const sdk = process.env.GS_EMSDK ?? join(process.env.USERPROFILE ?? '', '.codex/tools/emsdk');
if (!existsSync(join(sdk, 'upstream/emscripten/emcc.py'))) throw Error('Install/activate emsdk 6.0.11; see docs/development-environment.md');
if(readFileSync(join(sdk,'upstream/emscripten/emscripten-version.txt'),'utf8').trim().replaceAll('"','')!=='6.0.11')throw Error('Expected pinned Emscripten 6.0.11');
for (const threaded of [false, true]) {
const build = join(root, threaded ? 'build/wasm-threaded' : 'build/wasm');
const cmake = ['-S', join(root, 'native'), '-B', build, '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release', `-DGS_WASM_THREADS=${threaded ? 'ON' : 'OFF'}`,
    `-DCMAKE_TOOLCHAIN_FILE=${sdk}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake`];
for (const args of [cmake, ['--build', build, '--parallel', '8']]) {
    const result = spawnSync('cmake', args, { stdio: 'inherit', env: { ...process.env, EMSDK: sdk } });
    if (result.status !== 0) process.exit(result.status ?? 1);
}
const target = join(root, threaded ? 'public/assets/threaded' : 'public/assets');
mkdirSync(target, { recursive: true });
for (const name of ['decoder.mjs', 'decoder.wasm']) copyFileSync(join(build, name), join(target, name));
}
