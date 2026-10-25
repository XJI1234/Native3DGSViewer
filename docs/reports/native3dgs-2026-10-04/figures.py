"""Generate editable vector architecture diagrams and evidence-backed charts."""
import json
import pathlib
import statistics
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.font_manager import FontProperties
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch
from matplotlib import rcParams

BASE = pathlib.Path(__file__).parent
ROOT = BASE.parents[2]
ASSETS = BASE/'figures'
ASSETS.mkdir(exist_ok=True)
FONT = FontProperties(fname='C:/Windows/Fonts/msyh.ttc')
rcParams.update({'font.family':FONT.get_name(),'svg.fonttype':'none','axes.unicode_minus':False,
                 'axes.spines.top':False,'axes.spines.right':False,'font.size':11})
from matplotlib import font_manager
font_manager.fontManager.addfont('C:/Windows/Fonts/msyh.ttc')
COLORS = {'ink':'#16283B','cpu':'#E7F4F2','gpu':'#ECECFA','host':'#EAF1FB',
          'storage':'#FFF3DF','line':'#637B90','accent':'#0B7E83','muted':'#526B80'}

def canvas(title, sub):
    fig,ax=plt.subplots(figsize=(16,10))
    ax.set_xlim(0,100);ax.set_ylim(68,0);ax.axis('off');fig.patch.set_facecolor('#FFFFFF')
    ax.text(3,2,title,fontproperties=FONT,fontsize=23,color=COLORS['ink'],weight='bold')
    ax.text(3,5,sub,fontproperties=FONT,fontsize=12,color=COLORS['muted'])
    return fig,ax

def box(ax,x,y,w,h,title,detail='',kind='cpu',size=14):
    ax.add_patch(FancyBboxPatch((x,y),w,h,boxstyle='round,pad=0.25,rounding_size=0.85',
                               facecolor=COLORS[kind],edgecolor='#C5D4DF',linewidth=1))
    ax.text(x+1.4,y+2.4,title,fontproperties=FONT,fontsize=size,color=COLORS['ink'],weight='bold')
    if detail: ax.text(x+1.4,y+4.5,detail,fontproperties=FONT,fontsize=11.7,
                      va='top',linespacing=1.7,color=COLORS['muted'])

def arrow(ax,a,b,label='',dashed=False):
    ax.add_patch(FancyArrowPatch(a,b,arrowstyle='-|>',mutation_scale=14,
                   linewidth=1.35,color=COLORS['line'],linestyle='--' if dashed else '-'))
    if label: ax.text((a[0]+b[0])/2,(a[1]+b[1])/2-0.7,label,
                     fontproperties=FONT,fontsize=10.5,color=COLORS['muted'],ha='center',
                     bbox={'facecolor':'white','edgecolor':'none','pad':1.5})

def save(fig,name):
    fig.savefig(ASSETS/(name+'.png'),dpi=170,bbox_inches='tight',facecolor='white')
    fig.savefig(ASSETS/(name+'.svg'),bbox_inches='tight',facecolor='white')
    plt.close(fig)

fig,ax=canvas('Windows / 原生 D3D12 引擎','进程隔离解码、只读场景、独占 GPU 线程与事务激活；实线表示调用 / 数据流')
box(ax,3,8,28,8,'WinUI 3 宿主','文件 / 鼠标键盘 / SwapChainPanel','host')
box(ax,36,8,29,8,'Native3DGS::Engine SDK','IEngine / snapshot / poll_events','host')
box(ax,71,8,26,8,'统计与质量策略','DXGI budget / GPU timestamp','storage')
arrow(ax,(31.5,12),(35.5,12));arrow(ax,(50.5,16.5),(50.5,23))
box(ax,33,23,36,8,'engine 协调与相机','RequestId / Ticket / SurfaceGeneration')
arrow(ax,(34,31.5),(18,37));arrow(ax,(65,31.5),(68,37))
box(ax,3,38,29,18,'model-io / 子进程','probe → PLY / SPZ → normalize\nJob Object：取消 / 超时 / 峰值\n完整校验后只读共享映射')
box(ax,38,38,59,18,'render-core / 独占 D3D12 device','GPU 私有属性 / SH / 投影 / 排序缓冲\nHLSL：投影 + SH → 稳定 radix → Gaussian\n64MiB copy 页 / fence 安全释放','gpu')
arrow(ax,(32.5,47),(37.5,47),'scene')
box(ax,3,61,29,5,'splat-types：不可变 SceneHandle',kind='cpu',size=12)
box(ax,38,61,59,5,'D3D12 direct queue → DXGI swapchain → WinUI 合成',kind='host',size=12)
arrow(ax,(68,56.5),(68,60.5));save(fig,'architecture-windows')

fig,ax=canvas('Android / 原生 Vulkan 引擎','Kotlin 与 JNI 薄桥接；Service 进程解码；Surface 代际隔离；无 WebView 渲染')
box(ax,3,8,28,8,'Kotlin Android 宿主','SAF URI / 手势 / SurfaceView','host')
box(ax,36,8,29,8,'android-bridge / C ABI','dup FD / JNI / native handle','host')
box(ax,71,8,26,8,'engine / 协调线程','请求 / 相机 / 票据 / 状态')
arrow(ax,(31.5,12),(35.5,12));arrow(ax,(65.5,12),(70.5,12))
box(ax,36,24,29,8,'共享场景接收','FD / 布局版本 / 内容复核')
box(ax,71,24,26,8,'Surface 所有权','ANativeWindow / generation','host')
arrow(ax,(85,16.5),(85,23.5));arrow(ax,(50.5,16.5),(50.5,23.5))
box(ax,3,38,28,21,'隔离 Service / model-io','Binder 传状态与 FD\nASharedMemory 只读句柄\n共享 native PLY / SPZ 语义\n非 seek FD：有界临时缓存')
arrow(ax,(17,16.5),(17,37.5),'只读 FD')
arrow(ax,(31.5,43),(35,29),'共享映射')
box(ax,38,38,59,21,'render-core / Vulkan 1.1','GLSL → SPIR-V；投影 + SH → radix → quad\nsubgroup 路径自检 / 稳定后备路径\n属性分段 / storage range / memory budget\nswapchain 重建 / fence / device-loss','gpu')
arrow(ax,(50.5,32.5),(50.5,37.5));arrow(ax,(85,32.5),(85,37.5))
box(ax,3,63,94,4,'SDK：Kotlin AAR + NDK C ABI / Android 10+ arm64-v8a；真机能力与热态单独验收',kind='host',size=12)
save(fig,'architecture-android')

fig,ax=canvas('Web / WebGPU + WASM 引擎','ESM 核心与可选框架适配；解码在 Worker，GPU 提交在主线程；保留完整点数与 SH')
box(ax,3,8,28,9,'DOM / React 19 / Vue 3.5','可选入口 / callback host / 生命周期','host')
box(ax,37,8,28,9,'WebEngine / TypeScript','open / Result / camera / snapshot')
box(ax,71,8,26,9,'WebGPU 能力与预算','adapter limits / 排序自检','storage')
arrow(ax,(31.5,12.5),(36.5,12.5));arrow(ax,(65.5,12.5),(70.5,12.5))
box(ax,3,29,28,15,'独立 Worker / C++20 WASM','Emscripten / PLY / SPZ 1-4\n65,536 点且 ≤4MiB 输入 / 批次\ngzip CRC / 全局数学规范化')
arrow(ax,(46,17.5),(18,28.5),'操作身份 / cancel')
box(ax,3,51,28,10,'OPFS / 场景 backing','gzip 属性列 / tiled float32\n完整 bounds / 一次全局 rebase','storage')
arrow(ax,(17,44.5),(17,50.5))
box(ax,37,29,60,32,'render-core / WGSL / main thread','64 点属性布局；4MiB 上传读取 / 32MiB fence 背压','gpu')
box(ax,39,41,17,14,'投影 + SH','专门化 shader\n分页 / float32','gpu',12)
box(ax,59,41,17,14,'稳定 radix','bitmap rank\n2D / 分段 prefix','gpu',12)
box(ax,79,41,16,14,'Gaussian','远到近 alpha\nRGBA capture','gpu',12)
arrow(ax,(56.5,48),(58.5,48));arrow(ax,(76.5,48),(78.5,48))
arrow(ax,(51,17.5),(51,28.5));arrow(ax,(31.5,57),(36.5,57),'paged')
box(ax,37,64,60,3,'离屏验证 → 实际 Canvas 呈现 → 首帧完成才激活 / 失败回滚',kind='host',size=11)
arrow(ax,(86,61.5),(86,63.5));save(fig,'architecture-web')

fig,ax=canvas('Cloud / 服务端 CUDA + 图像缓存','当前 API v3 / NGSREQ3 / NGSFRM02；客户端消费图像，不执行高斯投影或排序')
box(ax,3,8,29,11,'WinUI 3 云客户端','离散相机：2° / 81 个距离档\nlease 心跳 / latest-wins','host')
box(ax,37,8,28,11,'端侧压缩帧 LRU','64MiB / 256 项 / 邻域预测\n前台 + 单后台下载','storage')
box(ax,71,8,26,11,'解码与显示','RGBA+Zstd / JPEG85-95\nBGRA / D3D12 合成','host')
arrow(ax,(32.5,13),(36.5,13));arrow(ax,(65.5,13),(70.5,13))
ax.plot([3,97],[25,25],color='#B9C8D5',ls='--',lw=1);ax.text(4,24,'客户端 / 网络 / 服务端边界',fontproperties=FONT,fontsize=10,color=COLORS['muted'])
box(ax,3,31,29,15,'Go net/http 网关','认证 / 模型版本 / 租约\n实例容量 / 前台优先\n共享视图 single-flight')
box(ax,38,31,27,11,'磁盘图像缓存','24h TTL / 10GiB LRU\n模型 + renderer hash / CRC','storage')
box(ax,71,31,26,11,'GPU0 渲染门控','同模型固定 GPU / 进程\n多卡扩展默认关闭','cpu')
arrow(ax,(18,19.5),(18,30.5),'HTTP 请求')
arrow(ax,(28,30.5),(52,19.5),'图像包响应')
arrow(ax,(32.5,37),(37.5,37),'hit');arrow(ax,(65.5,37),(70.5,37),'miss')
box(ax,3,53,29,10,'SQLite / WAL NORMAL','缓存索引 / lease\n绑定参数 / 可重建缓存','storage')
box(ax,38,50,59,13,'C++20 / CUDA / CUB 模型实例','共享 codec → 投影 + SH → 径向 / tile rank 排序\nCUDA 透明合成 → RGBA8 → Zstd / libjpeg-turbo','gpu')
arrow(ax,(18,46.5),(18,52.5));arrow(ax,(84,42.5),(84,49.5));arrow(ax,(51,49.5),(51,42.5),'写缓存')
save(fig,'architecture-cloud')

fig,ax=canvas('主要模块 / 依赖与资源边界','跨平台共享数学和格式语义；图形后端、宿主和传输各自独立')
box(ax,3,9,26,10,'splat-types / 公共值契约','RUB / SH0-3 / SceneHandle\ncount / bounds / 受检尺寸')
box(ax,37,9,26,10,'model-io / 输入提供者','probe / decode / normalize\n完整校验 / 进度 / 错误')
box(ax,71,9,26,10,'render-core / GPU 提供者','私有 packing / upload\nprojection / sort / draw','gpu')
arrow(ax,(29.5,14),(36.5,14));arrow(ax,(29,20),(71,20),'共同消费场景语义',True)
box(ax,37,32,26,11,'engine / 状态协调','请求、票据、设备 / 视口代际\n相机 / 事务替换 / 恢复 / 释放')
arrow(ax,(50,19.5),(50,31.5));arrow(ax,(84,19.5),(64,36))
for x,title,detail in [(3,'Windows 宿主','WinUI / SwapChainPanel'),(27,'Android 宿主','JNI / Kotlin / SurfaceView'),(51,'Web 宿主','DOM / React / Vue'),(75,'Cloud 网关与客户端','Go / 缓存 / 图像传输')]:
    box(ax,x,54,22,9,title,detail,'host',12)
    if x != 75: arrow(ax,(50,43.5),(x+11,53.5))
arrow(ax,(84,19.5),(86,53.5),'独立 CUDA 后端',True)
ax.text(3,28,'禁止 model-io 依赖图形 API；禁止 render-core 解析文件或依赖 UI。',fontproperties=FONT,fontsize=12,color=COLORS['accent'])
ax.text(3,67,'Cloud 的 CUDA worker 是独立后端实现，不能按图理解为复用桌面 IEngine 实例。',fontproperties=FONT,fontsize=11,color=COLORS['muted'])
save(fig,'module-boundaries')

fig,ax=canvas('计算阶段 / 数据布局与性能收益','格式读取、规范化和 GPU 帧分别计时；每个平台保留自己的执行与同步模型')
rows=[('Windows', '隔离进程 decode / validate','base + SH / 64MiB copy','D3D12 SH 展开 / stable radix','4 顶点带 / alpha / DXGI'),
      ('Android', 'Service / FD / shared memory','属性分段 / 128MiB range','Vulkan / subgroup 稳定路径','Gaussian quad / Surface'),
      ('Web', 'Worker / bounded WASM / OPFS','64 点 tiled / paged upload','WGSL 专门化 / bitmap radix','Canvas 验证 / alpha / capture'),
      ('Cloud', '实例内 native model decode','CUDA 属性 / GPU0 instance','SH / CUB / tile rank 排序','CUDA 合成 / readback / 编码')]
for i,(platform,*cells) in enumerate(rows):
    y=12+i*13
    ax.text(2,y+4,platform,fontproperties=FONT,fontsize=14,color=COLORS['ink'],weight='bold')
    for j,label in enumerate(cells):
        x=16+j*21
        pieces=label.split(' / ')
        box(ax,x,y,19,8,pieces[0], '\n'.join(pieces[1:]),'gpu' if j>=2 else 'cpu',11)
        if j<3:arrow(ax,(x+19.5,y+4),(x+20.5,y+4))
save(fig,'compute-pipeline')

def load(name): return json.loads((BASE/'evidence'/name).read_text(encoding='utf-8'))
def median_metric(row,key):return statistics.median(r['metrics'][key]['p50'] for r in row['perRun'])
def polish(ax,title,ylabel):
    ax.set_title(title,fontproperties=FONT,fontsize=14,loc='left',pad=18,color=COLORS['ink'])
    ax.set_ylabel(ylabel,fontproperties=FONT);ax.grid(axis='y',alpha=.15);ax.set_axisbelow(True)
    ax.tick_params(axis='x',labelsize=9)

if (BASE/'evidence/web-paired/results.json').exists():
    web=load('web-paired/results.json')
    if web.get('complete'):
        names=web['requested']['models'];labels=[n.replace('spz/','').replace('_v1','') for n in names]
        pairs={n:{r['mode']:r for r in web['rows'] if r['model']==n} for n in names}
        for kind,title,ylabel in [('firstFrameMs','Web 首个完成帧：加载协议相同','毫秒；每模型 / 引擎仅一次加载'),('completedMs','Web 完成帧墙钟：本帧排序与 GPU 完成','毫秒；三轮 p50 的中位值与范围')]:
            fig,ax=plt.subplots(figsize=(11.5,5.5));xs=list(range(len(names)))
            for offset,mode,color in [(-.18,'native','#137F88'),(.18,'spark','#BE773A')]:
                vals=[pairs[n][mode]['loaded']['firstFrameMs'] if kind=='firstFrameMs' else median_metric(pairs[n][mode],kind) for n in names]
                bars=ax.bar([x+offset for x in xs],vals,.34,label='WebGPU SDK' if mode=='native' else 'SparkJS 2.3.1',color=color)
                if kind=='completedMs':
                    lows=[v-min(r['metrics'][kind]['p50'] for r in pairs[n][mode]['perRun']) for n,v in zip(names,vals)]
                    highs=[max(r['metrics'][kind]['p50'] for r in pairs[n][mode]['perRun'])-v for n,v in zip(names,vals)]
                    ax.errorbar([x+offset for x in xs],vals,yerr=[lows,highs],fmt='none',ecolor='#263B4D',capsize=3)
                for bar,v in zip(bars,vals):ax.annotate(f'{v:.0f}' if kind=='firstFrameMs' else f'{v:.1f}',(bar.get_x()+bar.get_width()/2,v),xytext=(0,5),textcoords='offset points',ha='center',fontsize=8)
            ax.set_xticks(xs,labels);polish(ax,title,ylabel);ax.legend(frameon=False)
            fig.text(.08,.015,'1920×1080 / 全点 / SH3。完成帧包含 Worker update、timer 观察和同步等待；不是物理显示 FPS。',fontproperties=FONT,fontsize=9,color=COLORS['muted'])
            fig.tight_layout(rect=(0,.06,1,1));save(fig,'web-load-comparison' if kind=='firstFrameMs' else 'web-completed-comparison')
        fig,axes=plt.subplots(1,2,figsize=(13,5.6))
        # Distinct panels intentionally prevent interpreting unequal query scopes as speedup.
        bottom=[0.]*len(names)
        for key,label,color in [('projectionMs','投影 + SH','#137F88'),('sortMs','完整 GPU 排序','#6274BE'),('drawMs','Gaussian 绘制','#BE773A')]:
            v=[median_metric(pairs[n]['native'],key) for n in names];axes[0].bar(range(len(names)),v,bottom=bottom,label=label,color=color);bottom=[a+b for a,b in zip(bottom,v)]
        for ax in axes:ax.set_xticks(range(len(names)),labels,rotation=22,ha='right');ax.grid(axis='y',alpha=.15);ax.set_axisbelow(True)
        polish(axes[0],'WebGPU：完整 GPU 阶段','ms / 三轮阶段 p50 中位值');axes[0].legend(frameon=False,fontsize=9)
        axes[1].bar(range(len(names)),[median_metric(pairs[n]['spark'],'gpuMs') for n in names],color='#BE773A',alpha=.8)
        polish(axes[1],'Spark：同步 WebGL query 范围','ms / 不含异步 Worker 排序')
        fig.text(.06,.015,'两面板 query scope 不同。阶段 p50 的和不一定等于整帧 p50；Spark 投影 / 排序未独立计时。',fontproperties=FONT,fontsize=9,color=COLORS['muted'])
        fig.tight_layout(rect=(0,.06,1,1));save(fig,'web-gpu-scopes')

if (BASE/'evidence/windows-diagnostics.json').exists():
    windows=load('windows-diagnostics.json')
    if windows.get('complete'):
        fig,ax=plt.subplots(figsize=(12,5.5));rows=windows['rows'];bottom=[0.]*len(rows)
        for key,label,color in [('projectionMs','投影 + SH','#137F88'),('sortMs','稳定排序','#6274BE'),('drawMs','绘制','#BE773A')]:
            vals=[statistics.median(r['metrics'][key]['p50'] for r in row['runs']) for row in rows]
            ax.bar(range(len(rows)),vals,bottom=bottom,label=label,color=color);bottom=[a+b for a,b in zip(bottom,vals)]
        ax.set_xticks(range(len(rows)),[r['model'].replace('spz/','').replace('_v1','') for r in rows],rotation=20,ha='right')
        polish(ax,'Windows D3D12：强制完整投影 / 排序的 GPU 诊断','ms / 每组 60 预热 + 120 采样 × 3');ax.legend(frameon=False)
        fig.text(.07,.015,'固定 fit 相机、SH3、stride1、1920×1080、无 Present；不能与 Web 运动轨迹或桌面呈现 FPS 混用。',fontproperties=FONT,fontsize=9,color=COLORS['muted'])
        fig.tight_layout(rect=(0,.06,1,1));save(fig,'windows-gpu-stages')

# Final-release full-scene stage evidence is retained independently of the six paired reruns.
models=json.loads((ROOT/'ForWeb/docs/verification/evidence/model-tests-release.json').read_text(encoding='utf-8'))['rows']
big=[r for r in models if r['count']==22480361]
fig,ax=plt.subplots(figsize=(11.5,4.8));left=[0.,0.]
for stage,label,color in [('Inspecting','预检','#ABBECD'),('Downloading','下载','#397DA6'),('Inflating','解压','#96B9D1'),('Decoding','解码 / 规范化','#137F88'),('Rebasing','全局 rebase','#6274BE'),('Uploading','上传 / 首帧','#BE773A')]:
    vals=[r['stages'].get(stage,0)/1000 for r in big];ax.barh(range(2),vals,left=left,label=label,color=color);left=[a+b for a,b in zip(left,vals)]
ax.set_yticks(range(2),['最大 PLY / 5.31GB','最大 SPZ / 压缩输入']);ax.invert_yaxis();ax.set_xlabel('秒 / 状态边界墙钟');ax.set_title('22,480,361 点：完整加载各阶段',fontproperties=FONT,loc='left',pad=16,color=COLORS['ink']);ax.legend(ncol=3,frameon=False,loc='upper center',bbox_to_anchor=(.5,-.2),fontsize=10)
ax.grid(axis='x',alpha=.15);ax.set_axisbelow(True);fig.tight_layout(rect=(0,.08,1,1));save(fig,'maximum-load-stages')
print('Generated architecture and measured figures in',ASSETS)
