import { readFile, readdir } from 'node:fs/promises';
import { createHash } from 'node:crypto';
export const hash = bytes => createHash('sha256').update(bytes).digest('hex');
export async function walk(root, prefix = '') {
    const result = [];
    for (const entry of await readdir(root, { withFileTypes: true })) {
        const path = prefix + entry.name;
        if (entry.isDirectory()) result.push(...await walk(root + '/' + entry.name, path + '/'));
        else result.push({ path, sha256: hash(await readFile(root + '/' + entry.name)) });
    }
    return result.sort((a, b) => a.path.localeCompare(b.path));
}
export async function inputs(root) {
    const files = [];
    for (const directory of ['src', 'scripts', 'vendor']) files.push(...(await walk(root + '/' + directory)).map(file => ({ ...file, path: directory + '/' + file.path })));
    for (const path of ['package.json', 'pnpm-lock.yaml', 'tsconfig.json', 'vite.config.ts', 'index.html']) files.push({ path, sha256: hash(await readFile(root + '/' + path)) });
    return files.sort((a, b) => a.path.localeCompare(b.path));
}
