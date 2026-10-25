"""Use browser monotonic NavigationStart to select exactly the sampled trace window."""
import collections
import json
import pathlib
import statistics

ROOT = pathlib.Path(__file__).resolve().parents[3]
BASE = pathlib.Path(__file__).resolve().parent / 'continuous-interaction'
EVIDENCE = ROOT / 'ForWeb/docs/verification/evidence'
source = EVIDENCE / 'interactive-trace-retry-2026-10-04'
if not (source / 'results.json').exists():
    raise ValueError('Complete independent trace before summarizing')
result = json.loads((source / 'results.json').read_text(encoding='utf-8'))
assert result['complete'] and not result['failures'] and result['traced']
rows = []
for row in result['rows']:
    raw = json.loads((source / row['rawFile']).read_text(encoding='utf-8'))
    window = raw['runWindows'][0]
    begin = (raw['traceNavigationStart'] * 1000 + window['start']) * 1000
    end = (raw['traceNavigationStart'] * 1000 + window['end']) * 1000
    events = json.loads((source / row['rawFile'].replace('.json', '-trace.json')).read_text())['traceEvents']
    selected = [e for e in events if begin <= e.get('ts', -1) < end]
    metrics = {}
    for name in ['AnimationFrame::Presentation', 'DrawFrame', 'DCompPresenter::Present']:
        times = sorted(e['ts'] for e in selected if e['name'] == name)
        assert times
        intervals = sorted((b-a)/1000 for a, b in zip(times, times[1:]))
        slow = intervals[-max(1, int(len(intervals) * .01 + .999999)):]
        metrics[name] = {'events': len(times), 'rate': len(times) * 1000 / window['durationMs'],
                         'intervalP50Ms': statistics.median(intervals),
                         'intervalP95Ms': intervals[int((len(intervals)-1)*.95)],
                         'onePercentLowFps': 1000 / statistics.mean(slow),
                         'over33_3Percent': 100 * sum(t > 33.3 for t in intervals) / len(intervals)}
    states = collections.Counter(e['args']['frame_reporter']['state'] for e in selected
                                 if e['name'] == 'PipelineReporter'
                                 and e.get('args', {}).get('frame_reporter', {}).get('has_main_animation'))
    rows.append({'model': row['model'], 'engine': row['mode'], 'durationMs': window['durationMs'],
                 'traceSource': str(source.relative_to(ROOT)), 'metrics': metrics,
                 'pipelineStates': dict(states), 'submissionFps': row['perRun'][0]['submissionFps']})
    print(row['mode'], 'submission', round(row['perRun'][0]['submissionFps'], 2),
          'present', round(metrics['DCompPresenter::Present']['rate'], 2), 'states', dict(states))
BASE.mkdir(exist_ok=True)
(BASE / 'trace-summary.json').write_text(json.dumps({'traced': True, 'warning': 'Independent traced run; browser Present events are not optical scanout or end-to-end mouse latency.', 'rows': rows}, ensure_ascii=False, indent=2), encoding='utf-8')
