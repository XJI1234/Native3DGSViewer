import { defineConfig } from 'vite';
import { resolve } from 'node:path';
import { createReadStream, statSync } from 'node:fs';

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
                    const root = resolve(modelRoot);
                    const path = resolve(root, name);
                    if (!path.startsWith(root + '/') && !path.startsWith(root + '\\')) { res.statusCode = 403; res.end(); return; }
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
