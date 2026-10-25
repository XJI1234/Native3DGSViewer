"""Make independent continuous-interaction charts from the validated primary data."""
import json
import pathlib
import statistics
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.font_manager import FontProperties

ROOT = pathlib.Path(__file__).resolve().parents[3]
BASE = pathlib.Path(__file__).resolve().parent / 'continuous-interaction'
DATA = ROOT / 'ForWeb/docs/verification/evidence/interactive-2026-10-04'
result = json.loads((DATA / 'results.json').read_text(encoding='utf-8'))
assert result['complete'] and not result['failures'] and not result['traced']
font = FontProperties(fname='C:/Windows/Fonts/msyh.ttc')
plt.rcParams.update({'font.family': font.get_name(), 'svg.fonttype': 'none',
                     'axes.spines.top': False, 'axes.spines.right': False,
                     'axes.labelcolor': '#18304A', 'text.color': '#18304A'})
names = result['requested']['models']
pairs = {name: {r['mode']: r for r in result['rows'] if r['model'] == name} for name in names}
fig, axes = plt.subplots(1, 2, figsize=(14, 7), gridspec_kw={'width_ratios': [1, 1]})
for ax, key, title in zip(axes, ['submissionFps', 'onePercentLowFps'],
                           ['连续交互平均绘制提交率', '最慢 1% 间隔对应的帧率']):
    for mode, delta, color, label in [('native', -.18, '#008A8F', 'Web SDK'),
                                      ('spark', .18, '#7661B3', 'SparkJS 2.3.1')]:
        values = [statistics.median(r[key] for r in pairs[name][mode]['perRun']) for name in names]
        lower = [v - min(r[key] for r in pairs[name][mode]['perRun']) for name, v in zip(names, values)]
        upper = [max(r[key] for r in pairs[name][mode]['perRun']) - v for name, v in zip(names, values)]
        bars = ax.barh([i+delta for i in range(len(names))], values, height=.33,
                       color=color, label=label, xerr=[lower, upper], capsize=3)
        for bar, value, error in zip(bars, values, upper):
            ax.text(value+error+1.8, bar.get_y()+bar.get_height()/2, f'{value:.1f}', va='center', fontsize=9)
    ax.set_yticks(range(len(names)), [name.replace('spz/', '') for name in names]); ax.invert_yaxis()
    ax.set_title(title, fontproperties=font, fontsize=15, pad=15)
    ax.set_xlim(0, 137); ax.set_xlabel('次/秒（非物理扫描帧率）', fontproperties=font)
    ax.grid(axis='x', color='#E3EAF0', linewidth=.6); ax.set_axisbelow(True)
handles, labels = axes[0].get_legend_handles_labels()
fig.legend(handles, labels, loc='upper center', bbox_to_anchor=(.5, .93), ncol=2, frameon=False)
fig.suptitle('Web / Spark：前台浏览器连续旋转、缩放、平移', fontproperties=font, fontsize=20, y=.98)
fig.text(.05, .035, 'RTX3080 · 1920×1080 · 完整SH3 / 无LoD · 默认垂直同步 · 10s预热 + 20s×3\n'
         '条形为三轮中位数，误差线为三轮范围；提交率与串行GPU完成帧是不同指标。',
         fontproperties=font, fontsize=10, color='#526C82')
fig.tight_layout(rect=[0, .11, 1, .92]); BASE.mkdir(exist_ok=True)
fig.savefig(BASE / 'continuous-interaction.png', dpi=160, facecolor='white')
fig.savefig(BASE / 'continuous-interaction.svg', facecolor='white')
print('Generated continuous-interaction PNG/SVG')
