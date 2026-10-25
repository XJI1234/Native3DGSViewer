"""Validate and package report artifacts without models or raw RGBA buffers."""
import csv
import hashlib
import json
import pathlib
import re
import zipfile
from pypdf import PdfReader

BASE = pathlib.Path(__file__).resolve().parent
ROOT = BASE.parents[2]
OUTPUT = ROOT / 'output/pdf'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    manifest = json.loads((BASE / 'report-manifest.json').read_text(encoding='utf-8'))
    for entry in manifest['sources'] + manifest['files']:
        path = ROOT / entry['path']
        if digest(path.read_bytes()) != entry['sha256']:
            raise ValueError(f'Source or artifact changed: {path}')
    expected_rows = {'windows-stages.csv': 7, 'web-paired-summary.csv': 12,
                     'model-load-stages.csv': 38}
    for name, count in expected_rows.items():
        with (BASE / 'evidence' / name).open(encoding='utf-8', newline='') as stream:
            assert len(list(csv.DictReader(stream))) == count, name
    reader = PdfReader(ROOT / manifest['pdf'])
    assert len(reader.pages) == 33
    pdf_text = '\n'.join(page.extract_text() for page in reader.pages)
    model_manifest = json.loads((ROOT / 'ForWeb/docs/verification/evidence/model-manifest.json')
                               .read_text(encoding='utf-8'))
    for model in model_manifest['models']:
        assert model['name'].removeprefix('spz/') in pdf_text, model['name']
    markdown = (BASE / 'report.md').read_text(encoding='utf-8')
    for target in re.findall(r'\]\(([^)]+)\)', markdown):
        if not target.startswith(('https://', 'http://', '#')):
            assert (BASE / target).is_file(), target
    files = {path for path in BASE.rglob('*') if path.is_file()
             and path.suffix in {'.md', '.py', '.json', '.csv', '.log', '.png', '.svg'}}
    files.update(ROOT / entry['path'] for entry in manifest['sources'])
    files.add(ROOT / manifest['pdf'])
    files.update(ROOT / name for name in [
        'ForWeb/tools/completed-benchmark.mjs',
        'ForWeb/tools/browser-benchmark-guards.mjs',
        'ForWeb/tools/image-metrics.py'])
    payload = {path.relative_to(ROOT).as_posix(): path.read_bytes() for path in sorted(files)}
    for target in re.findall(r'\]\(([^)]+)\)', markdown):
        if not target.startswith(('https://', 'http://', '#')):
            assert (BASE / target).resolve().relative_to(ROOT).as_posix() in payload, target
    assert all(not name.endswith(('.rgba', '.ply', '.spz')) for name in payload)
    assert sum(name.endswith('.svg') for name in payload) == 11
    checksums = {name: {'bytes': len(data), 'sha256': digest(data)}
                 for name, data in payload.items()}
    bundle = OUTPUT / 'native3dgs-engine-report-2026-10-04-bundle.zip'
    with zipfile.ZipFile(bundle, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name, data in payload.items():
            archive.writestr(name, data)
        archive.writestr('bundle-checksums.json', json.dumps(checksums, ensure_ascii=False,
                                                          indent=2).encode('utf-8'))
    with zipfile.ZipFile(bundle) as archive:
        assert archive.testzip() is None
        for name, expected in checksums.items():
            data = archive.read(name)
            assert len(data) == expected['bytes'] and digest(data) == expected['sha256'], name
    sidecar = bundle.with_suffix('.zip.sha256')
    sidecar.write_text(f'{digest(bundle.read_bytes())}  {bundle.name}\n', encoding='ascii')
    print('Validated 22 sources, 24 report artifacts, 33 PDF pages, all 38 model names,')
    print('CSV rows 7/12/38 and portable Markdown links.')
    print(f'Packaged {len(payload)} files; {bundle.stat().st_size:,} bytes: {bundle}')


if __name__ == '__main__':
    main()
