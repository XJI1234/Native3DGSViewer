"""Fresh-process native SDK continuous browsing, not fenced GPU-stage throughput."""
import datetime,hashlib,json,pathlib,re,statistics,subprocess
ROOT=pathlib.Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/verification/three-engine-2026-10-06'
MODEL_ROOT=pathlib.Path(r'C:\Users\21544\Desktop\zhishan')
BINARY=ROOT/'out/Release/Native3DGSViewer.InteractionBench.exe'
MODELS=['shengyi_v1.ply','spz/shengyi_v1.spz','he_v1.ply','spz/he_v1.spz','jiulonghu_v1.ply','spz/jiulonghu_v1.spz']
manifest={r['name']:r for r in json.loads((ROOT/'ForWeb/docs/verification/evidence/model-manifest.json').read_text(encoding='utf-8'))['models']}
report={'date':datetime.datetime.now(datetime.timezone.utc).isoformat(),'complete':False,'protocol':'Public native model-loader/render-core SDK host, visible HWND DirectComposition swapchain, accepted DXGI Presents,120Hz paced input/render,5s warm+20s sample,full SH3/stride1/no mitigation,fresh process per repetition','sourceCommit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'binarySha256':hashlib.sha256(BINARY.read_bytes()).hexdigest(),'sourceSha256':hashlib.sha256((ROOT/'bench/windows-interaction.cpp').read_bytes()).hexdigest(),'resolution':[1920,1080],'rows':[],'failures':[]}
poses={}
for name in MODELS:
 h=hashlib.sha256()
 with (MODEL_ROOT/name).open('rb') as f:
  for block in iter(lambda:f.read(8*1024*1024),b''):h.update(block)
 assert h.hexdigest()==manifest[name]['sha256'],name
for repetition in range(3):
 for name in MODELS if repetition%2==0 else list(reversed(MODELS)):
  print('Started',name,repetition,flush=True)
  result=subprocess.run([str(BINARY),str(MODEL_ROOT/name)],cwd=BINARY.parent,capture_output=True,timeout=660)
  text=(result.stdout.decode('utf-8',errors='replace')+result.stderr.decode('utf-8',errors='replace')).replace('\r','')
  log=name.replace('/','_')+f'-native-{repetition}.log'
  (OUT/log).write_text(text,encoding='utf-8')
  try:
   assert result.returncode==0,text[-2000:]
   loaded=re.search(r'loaded count=(\d+) degree=(\d+) decode_ms=([\d.eE+-]+) first_frame_ms=([\d.eE+-]+)',text)
   interact=re.search(r'interaction duration_ms=([\d.eE+-]+) accepted_presents=(\d+) render_calls=(\d+) fps=([\d.eE+-]+)',text)
   pose=re.search(r'^pose ([\d.eE+ .-]+)$',text,re.M)
   assert loaded and interact and pose,text[:2000]
   count=int(loaded[1]);degree=int(loaded[2]);assert count==manifest[name]['count'] and degree==3
   points=[float(v) for v in pose[1].split()];poses[name]={'position':points[:3],'target':points[3:],'up':[0,1,0]}
   samples=[[float(v) for v in line.split(',')] for line in text.splitlines() if re.fullmatch(r'[\d.eE+-]+,[\d.eE+-]+,\d+,[\d.eE+-]+',line)]
   assert len(samples)>30 and float(interact[1])>=20000
   intervals=[r[1] for r in samples];slow=sorted(intervals,reverse=True)[:max(1,len(intervals)//100)]
   row={'model':name,'repetition':repetition,'count':count,'degree':degree,'sha256':manifest[name]['sha256'],'inputBytes':manifest[name]['bytes'],'decodeMs':float(loaded[3]),'firstFrameMs':float(loaded[4]),'durationMs':float(interact[1]),'acceptedPresents':int(interact[2]),'renderCalls':int(interact[3]),'fps':float(interact[4]),'frameMs':1000/float(interact[4]),'onePercentLowFps':1000/statistics.mean(slow),'pose':poses[name],'rawFile':log}
   report['rows'].append(row);print(json.dumps(row),flush=True)
  except Exception as error:
   report['failures'].append({'model':name,'repetition':repetition,'error':str(error),'rawFile':log});raise
  finally:
   (OUT/'windows-results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
   (OUT/'reference-poses.json').write_text(json.dumps(poses,indent=2)+'\n',encoding='utf-8')
report['complete']=len(report['rows'])==18 and not report['failures']
(OUT/'windows-results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
