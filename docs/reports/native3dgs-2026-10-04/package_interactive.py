"""Archive the continuous tests, including failures, trace evidence and exact driver."""
import hashlib
import json
import pathlib
import re
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[3]
BASE = pathlib.Path(__file__).resolve().parent
DATA = ROOT / 'ForWeb/docs/verification/evidence'
REPORT = BASE / 'continuous-interaction'
primary = json.loads((DATA / 'interactive-2026-10-04/results.json').read_text(encoding='utf-8'))
assert primary['complete'] and len(primary['rows']) == 12 and not primary['failures']
files = {p for p in REPORT.rglob('*') if p.is_file()}
for folder in ['interactive-2026-10-04', 'interactive-retry-2026-10-04',
               'interactive-trace-retry-2026-10-04', 'interactive-trace-2026-10-04']:
    files.update(p for p in (DATA / folder).glob('*') if p.is_file()
                 and (folder != 'interactive-trace-2026-10-04' or not p.name.endswith('-trace.json')))
files.update(BASE / name for name in ['interactive_summary.py', 'interactive_figures.py',
                                     'interactive_trace_summary.py', 'merge_interactive_retry.py',
                                     'package_interactive.py'])
files.update(ROOT / 'ForWeb' / name for name in [
    'apps/benchmark.ts', 'benchmark.html', 'package.json', 'pnpm-lock.yaml',
    'tools/interactive-benchmark.mjs', 'tools/browser-benchmark-guards.mjs',
    'src/engine/engine.ts', 'src/render-core/renderer.ts', 'src/model-io/streaming.ts'])
for entry in primary['provenance']['files']:
    if entry['path'] != 'tools/interactive-benchmark.mjs':
        assert hashlib.sha256((ROOT / 'ForWeb' / entry['path']).read_bytes()).hexdigest() == entry['sha256'], entry['path']
runner = (ROOT / 'ForWeb/tools/interactive-benchmark.mjs').read_text(encoding='utf-8')
# Only trace clock mapping was added after the formal run. Preserve its exact
# measured driver separately, and verify the recorded hash before packaging.
runner = re.sub(r'            let navigationStart;\n            if\(traced\)\{cdp=.*?\}\n',
                "            if(traced){cdp=await page.context().newCDPSession(page);await cdp.send('Tracing.start',{categories:'benchmark,cc,viz,gpu,devtools.timeline,disabled-by-default-devtools.timeline.frame',transferMode:'ReturnAsStream'});}\n", runner)
runner = runner.replace('            if(traced)result.traceNavigationStart=navigationStart;\n', '')
runner_bytes = runner.encode('utf-8')
expected = next(e['sha256'] for e in primary['provenance']['files'] if e['path'] == 'tools/interactive-benchmark.mjs')
assert hashlib.sha256(runner_bytes).hexdigest() == expected
payload = {p.relative_to(ROOT).as_posix(): p.read_bytes() for p in sorted(files)}
payload['measurement-snapshots/interactive-benchmark-primary.mjs'] = runner_bytes
for target in re.findall(r'\]\(([^)]+)\)', (REPORT / 'report.md').read_text(encoding='utf-8')):
    if not target.startswith(('http://', 'https://', '#')):
        assert (REPORT / target).resolve().relative_to(ROOT).as_posix() in payload, target
assert all(not name.endswith(('.rgba', '.ply', '.spz')) for name in payload)
checksums = {name: {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
             for name, data in payload.items()}
output = ROOT / 'output/reports'
output.mkdir(parents=True, exist_ok=True)
archive_path = output / 'web-spark-continuous-interaction-2026-10-04.zip'
with zipfile.ZipFile(archive_path, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
    for name, data in payload.items():
        archive.writestr(name, data)
    archive.writestr('bundle-checksums.json', json.dumps(checksums, ensure_ascii=False, indent=2).encode('utf-8'))
with zipfile.ZipFile(archive_path) as archive:
    assert archive.testzip() is None
    for name, expected in checksums.items():
        assert hashlib.sha256(archive.read(name)).hexdigest() == expected['sha256'], name
archive_path.with_suffix('.zip.sha256').write_text(
    f'{hashlib.sha256(archive_path.read_bytes()).hexdigest()}  {archive_path.name}\n', encoding='ascii')
print(f'Validated {len(payload)} files and exact measured driver. ZIP: {archive_path.stat().st_size:,} bytes')
