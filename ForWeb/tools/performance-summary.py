"""Derive documented frame metrics from raw CSV, without treating GPU timers as FPS."""
import csv,json,math,statistics,sys
from pathlib import Path
root=Path(sys.argv[1] if len(sys.argv)>1 else 'docs/verification/evidence/benchmark-full');data=json.loads((root/'results.json').read_text());rows=[]
def percentile(xs,p):
    s=sorted(xs);return s[math.floor((len(s)-1)*p)]
for r in data['rows']:
    slug=r['model'].replace('/','_').replace('.','_')+'-'+r['mode']
    samples=list(csv.DictReader((root/(slug+'.csv')).open()))
    runs=[]
    for run in range(data['runs']):
        values=[float(s['intervalMs']) for s in samples if int(s['run'])==run];slow=sorted(values)[-math.ceil(len(values)*.01):]
        runs.append({'run':run,'p50':percentile(values,.5),'p95':percentile(values,.95),'p99':percentile(values,.99),'onePercentLowFps':1000/statistics.mean(slow),'meanFps':1000/statistics.mean(values),'maxIntervalMs':max(values),'slowestOnePercentTimeFraction':sum(slow)/sum(values),'longIntervalCounts':{str(threshold):sum(value>threshold for value in values) for threshold in [16.7,50,100,1000]}})
    rows.append({'model':r['model'],'engine':r['mode'],'runs':runs,'medianP95':statistics.median(x['p95'] for x in runs),'medianP99':statistics.median(x['p99'] for x in runs),'medianOnePercentLowFps':statistics.median(x['onePercentLowFps'] for x in runs),'gpuP50':statistics.median(x['gpuP50'] for x in r['perRun'] if x['gpuP50'] is not None),'gpuScope':r['gpuScope']})
(root/'summary.json').write_text(json.dumps(rows,indent=2));print(json.dumps(rows,indent=2))
