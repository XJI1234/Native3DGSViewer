"""Keep the failed attempt and use a complete replacement pair for its model."""
import json
import pathlib
import shutil

ROOT = pathlib.Path(__file__).resolve().parents[3]
DATA = ROOT / 'ForWeb/docs/verification/evidence/interactive-2026-10-04'
RETRY = DATA.parent / 'interactive-retry-2026-10-04'
initial_path = DATA / 'attempt-1-results.json'
if not initial_path.exists():
    shutil.copyfile(DATA / 'results.json', initial_path)
initial = json.loads(initial_path.read_text(encoding='utf-8'))
retry = json.loads((RETRY / 'results.json').read_text(encoding='utf-8'))
assert not initial['complete'] and len(initial['failures']) == 1
assert retry['complete'] and not retry['failures'] and len(retry['rows']) == 2
assert initial['provenance'] == retry['provenance']
assert all(initial[k] == retry[k] for k in ['browser', 'headless', 'browserArgs', 'warmupMs', 'sampleMs', 'runs', 'traced'])
replacement = retry['requested']['models']
assert replacement == ['zhihuizhimen.ply']
for row in retry['rows']:
    original = row['rawFile']
    shutil.copyfile(RETRY / original, DATA / ('retry-' + original))
    shutil.copyfile(RETRY / original.replace('.json', '.png'), DATA / ('retry-' + original.replace('.json', '.png')))
    row['rawFile'] = 'retry-' + original
final = {**initial, 'date': retry['date'],
         'rows': [row for row in initial['rows'] if row['model'] not in replacement] + retry['rows'],
         'failures': [], 'complete': True, 'initialFailures': initial['failures'],
         'recovery': {'policy': 'Replace BOTH engines for failed model with entire untraced three-run retry pair; keep original attempt unchanged.',
                      'initialFile': 'attempt-1-results.json', 'retryFile': '../interactive-retry-2026-10-04/results.json',
                      'rootCause': 'Not isolated; OPFS read returned zero during rebase once. Same code/model fresh-session retry loaded successfully.'}}
assert len(final['rows']) == 12
(DATA / 'results.json').write_text(json.dumps(final, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print('Merged 12 complete groups; initial OPFS failure preserved; both large-model engines replaced.')
