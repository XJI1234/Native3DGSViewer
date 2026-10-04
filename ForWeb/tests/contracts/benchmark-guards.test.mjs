import {test} from 'node:test';
import assert from 'node:assert/strict';
import {withBenchmarkDeadline, validateTimingSamples,disposeBenchmark} from '../../tools/browser-benchmark-guards.mjs';
test('host watchdog rejects a hung evaluation and closes only its testing page', async()=>{
    let closed=0;const page={async close(){closed++}};
    await assert.rejects(withBenchmarkDeadline(page,new Promise(()=>{}),'fixture',10),/BenchmarkTimeout: fixture/);
    assert.equal(closed,1);
});
test('successful evaluation clears its watchdog and cleanup preserves errors',async()=>{
    const page={isClosed:()=>false,async close(){throw Error('unexpected close')},async evaluate(){throw Error('backing cleanup failed')}};
    assert.equal(await withBenchmarkDeadline(page,Promise.resolve(42),'fixture',10),42);
    await assert.rejects(disposeBenchmark(page),/backing cleanup failed/);
});
test('timing admission rejects absent, nonfinite, stale and incomplete observations',()=>{
    const valid={frameId:2,gpuFrameId:2,gpuMs:1};
    validateTimingSamples([valid],1,['gpuMs']);
    for(const invalid of [{...valid,gpuMs:null},{...valid,gpuMs:NaN},{...valid,gpuMs:-1},{...valid,gpuFrameId:1}])assert.throws(()=>validateTimingSamples([invalid],1,['gpuMs']));
    assert.throws(()=>validateTimingSamples([],1,['gpuMs']));
});


test('a resolving cleanup error still fails verification', async()=>{
 const page={isClosed:()=>false,async evaluate(){return {error:'StorageCleanup: fixture'};}};
 await assert.rejects(disposeBenchmark(page),/StorageCleanup/);
});
