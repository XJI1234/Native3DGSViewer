import {chromium} from '@playwright/test';
import {mkdtemp,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';

/** An isolated disk-backed profile models ordinary browsing, not private OPFS. */
export async function launchBenchmarkBrowser(headless){
    if(process.env.GS_BENCH_PERSISTENT!=='1')return chromium.launch({channel:'msedge',headless});
    const profile=await mkdtemp(join(tmpdir(),'gs-benchmark-'));
    try{
        const context=await chromium.launchPersistentContext(profile,{channel:'msedge',headless,viewport:{width:1920,height:1080},deviceScaleFactor:1});
        return {newPage:()=>context.newPage(),version:()=>context.browser().version(),
            close:async()=>{try{await context.close();}finally{await rm(profile,{recursive:true,force:true});}}};
    }catch(e){await rm(profile,{recursive:true,force:true});throw e;}
}
