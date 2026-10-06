import { createHash } from 'node:crypto';

/** Stream large inputs without retaining a second model in RAM. Never time this as loading. */
export async function hashServed(url) {
    const response = await fetch(url, { signal: AbortSignal.timeout(600000) });
    if (!response.ok || !response.body) throw Error(`Provenance HTTP ${response.status}: ${url}`);
    const hash = createHash('sha256');
    let bytes = 0;
    for await (const chunk of response.body) {
        bytes += chunk.length;
        hash.update(chunk);
    }
    return { url: String(url), bytes, sha256: hash.digest('hex'), verifiedAt: new Date().toISOString() };
}

export async function verifyServedModel(base, model) {
    const result = await hashServed(new URL('/models/' + model.name.split('/').map(encodeURIComponent).join('/'), base));
    if (result.sha256 !== model.sha256 || result.bytes !== model.bytes)
        throw Error(`Served model differs from manifest: ${model.name}`);
    return { model: model.name, ...result };
}

export async function verifyServedAssets(base, localAssets) {
    const results = [];
    for (const asset of localAssets) {
        if (!asset.path.startsWith('public/assets/')) continue;
        const result = await hashServed(
            new URL('/assets/' + asset.path.slice('public/assets/'.length), base),
        );
        if (result.sha256 !== asset.sha256)
            throw Error(`Served decoder differs from local build: ${asset.path}`);
        results.push({ path: asset.path, ...result });
    }
    // Vite transforms these modules: record delivered hashes, without equating them with source hashes.
    for (const path of [
        'apps/benchmark.ts',
        'src/model-io/streaming.ts',
        'src/model-io/wasm.ts',
        'src/model-io/decoder.worker.ts',
        'src/engine/engine.ts',
        'src/render-core/renderer.ts',
        'src/render-core/shaders.ts',
        'src/render-core/sort.ts',
        'src/render-core/sorting-policy.ts',
        'src/model-io/decoder-options.ts',
    ])
        results.push({ path, transformed: true, ...(await hashServed(new URL('/' + path, base))) });
    const spark = localAssets.find((asset) => asset.path.endsWith('/spark/dist/spark.module.js'));
    if (spark) results.push(await verifyServedSpark(base, spark.sha256));
    return results;
}

/** The dev benchmark requires source maps so delivered optimized code can be tied to the pin. */
export async function verifyServedSpark(base, pinnedSha256) {
    const response = await fetch(new URL('/apps/benchmark.ts', base), { signal: AbortSignal.timeout(30000) });
    if (!response.ok) throw Error('Spark benchmark module unavailable');
    const imported = (await response.text()).match(
        /from\s+["']([^"']*\/@sparkjsdev_spark\.js[^"']*)["']/,
    )?.[1];
    if (!imported) throw Error('Cannot resolve served Spark module from benchmark');
    const moduleUrl = new URL(imported, base);
    const module = await hashServed(moduleUrl);
    const mapUrl = new URL(moduleUrl.pathname + '.map', moduleUrl);
    const mapResponse = await fetch(mapUrl, { signal: AbortSignal.timeout(30000) });
    if (!mapResponse.ok) throw Error('Served Spark provenance requires its Vite source map');
    const mapText = await mapResponse.text(),
        map = JSON.parse(mapText);
    const index = map.sources.findIndex((path) => path.endsWith('/spark/dist/spark.module.js'));
    const source = map.sourcesContent[index];
    if (typeof source !== 'string' || createHash('sha256').update(source).digest('hex') !== pinnedSha256)
        throw Error('Served Spark source differs from pinned dependency');
    return {
        path: '@sparkjsdev/spark',
        ...module,
        sourceSha256: pinnedSha256,
        mapSha256: createHash('sha256').update(mapText).digest('hex'),
        mapUrl: mapUrl.href,
        scope: 'Delivered optimized Spark module plus source-map match to pin; source includes embedded decode workers/WASM',
    };
}
