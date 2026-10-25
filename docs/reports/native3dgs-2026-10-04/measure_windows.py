"""Read-only diagnostics of the existing Release SceneBench binary."""
import datetime
import hashlib
import json
import pathlib
import re
import statistics
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[3]
OUT = pathlib.Path(__file__).parent / 'evidence'
OUT.mkdir(parents=True, exist_ok=True)
MODEL_ROOT = pathlib.Path(r'C:\Users\21544\Desktop\zhishan')
BINARY = ROOT / 'out/Release/Native3DGSViewer.SceneBench.exe'
MODELS = ['changjin_v1.ply', 'shengyi_v1.ply', 'spz/shengyi_v1.spz',
          'tumu_v1.ply', 'spz/tumu_v1.spz', 'zhihuizhimen.ply', 'jiulonghu_v1.ply']
assert all((MODEL_ROOT/name).is_file() for name in MODELS)

def percentile(values, q):
    ordered = sorted(values)
    return ordered[min(len(ordered)-1, int(q*(len(ordered)-1)))]

report = {'date': datetime.datetime.now(datetime.timezone.utc).isoformat(),
          'sourceCommit': subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
          'binary': str(BINARY.relative_to(ROOT)),
          'binarySha256': hashlib.sha256(BINARY.read_bytes()).hexdigest(),
          'binaryModifiedAt': datetime.datetime.fromtimestamp(BINARY.stat().st_mtime,datetime.timezone.utc).isoformat(),
          'protocol': 'Existing Release x64 binary;1920x1080;SH3;stride1;fixed fit camera;force projection/sort every frame;60 warmup+120 samples;3 independent processes/model;no Present;load+upload exclude device init and first-frame setup;not the Web moving-pose protocol',
          'rows': [], 'failures': [], 'complete': False}
for model in MODELS:
    path = MODEL_ROOT/model
    sha = hashlib.sha256(path.read_bytes()).hexdigest() if path.stat().st_size < 200_000_000 else None
    if sha is None:
        h = hashlib.sha256()
        with path.open('rb') as f:
            for block in iter(lambda:f.read(8*1024*1024),b''): h.update(block)
        sha = h.hexdigest()
    row = {'model': model, 'sha256': sha, 'inputBytes': path.stat().st_size, 'runs': []}
    report['rows'].append(row)
    for run in range(3):
        command = [str(BINARY),str(path),'force','120','3','1']
        result = subprocess.run(command,cwd=BINARY.parent,capture_output=True,timeout=240)
        output = result.stdout.decode('utf-8',errors='replace') + result.stderr.decode('utf-8',errors='replace')
        log = model.replace('/','_')+f'-windows-{run}.log'
        (OUT/log).write_text(output,encoding='utf-8')
        try:
            assert result.returncode == 0, 'SceneBench failed'
            times = re.search(r'load_ms=([\d.]+) upload_ms=([\d.]+)',output)
            assert times
            frames = [[float(v) for v in line.split(',')] for line in output.splitlines()
                      if re.fullmatch(r'\d+,\d+,[\d.eE+-]+,[\d.eE+-]+,[\d.eE+-]+,[\d.eE+-]+',line)]
            assert len(frames) == 120, f'Incomplete frames: {len(frames)}'
            assert len({int(f[1]) for f in frames})==1
            assert all(all(v >= 0 for v in f[2:]) for f in frames)
            metrics = {name:{'p50':statistics.median([f[index] for f in frames]),
                             'p95':percentile([f[index] for f in frames],.95)}
                       for name,index in [('projectionMs',2),('sortMs',3),('drawMs',4),('gpuMs',5)]}
            measurement = {'run':run,'count':int(frames[0][1]),'loadMs':float(times[1]),
                           'uploadMs':float(times[2]),'metrics':metrics,'samples':frames,'log':log}
            row['runs'].append(measurement)
            print(json.dumps({'model':model,'run':run,'loadMs':measurement['loadMs'],
                              'uploadMs':measurement['uploadMs'],'metrics':metrics}),flush=True)
        except Exception as exc:
            report['failures'].append({'model':model,'run':run,'error':str(exc),'log':log})
            (OUT/'windows-diagnostics.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
            raise
        (OUT/'windows-diagnostics.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
report['complete'] = len(report['rows']) == len(MODELS) and all(len(row['runs'])==3 for row in report['rows'])
(OUT/'windows-diagnostics.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('Complete Windows diagnostics',report['complete'],flush=True)
