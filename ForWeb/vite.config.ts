import { defineConfig } from 'vite';
import { resolve, relative, isAbsolute } from 'node:path';
import { createReadStream, statSync, realpathSync } from 'node:fs';

export default defineConfig({
    publicDir: 'public',
    worker: {rollupOptions:{output:{entryFileNames:'assets/decoder.worker.js'}}},
    server: {watch:{ignored:['**/.local/**','**/docs/verification/evidence/**','**/test-results/**']}},
    build: {
        lib: { entry: { index: 'src/index.ts', react: 'src/react.ts', vue: 'src/vue.ts' }, formats: ['es'] },
        rollupOptions: { external: ['react', 'vue'] },
    },
    plugins: [{
        name: 'local-model-fixtures',
        configureServer(server) {
            const modelRoot = process.env.GS_MODEL_ROOT;
            if (!modelRoot) return;
            server.middlewares.use('/models', (req, res, next) => {
                try {
                    const name = decodeURIComponent((req.url ?? '').split('?')[0]!.replace(/^\//, ''));
                    const root = realpathSync(modelRoot);
                    if (name.includes('\\')) { res.statusCode = 403; res.end(); return; }
                    const path = realpathSync(resolve(root, name));
                    const child = relative(root, path);
                    if (!child || child === '..' || child.startsWith('..' + '/') || child.startsWith('..' + '\\') || isAbsolute(child)) { res.statusCode = 403; res.end(); return; }
                    if (!/\.(ply|spz)$/.test(path)) { res.statusCode = 403; res.end(); return; }
                    res.setHeader('Content-Length', statSync(path).size);
                    res.setHeader('Content-Type', 'application/octet-stream');
                    const stream = createReadStream(path);
                    stream.on('error', () => { res.destroy(); });
                    res.on('close', () => stream.destroy());
                    stream.pipe(res);
                } catch { next(); }
            });
        },
    }],
});
