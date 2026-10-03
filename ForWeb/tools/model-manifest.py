"""Hash only authorized PLY/SPZ fixtures; never copy the model files into the repository."""
import argparse, gzip, hashlib, json, struct, time
from pathlib import Path
parser=argparse.ArgumentParser()
parser.add_argument('--root',type=Path,required=True)
parser.add_argument('--output',type=Path,default=Path('docs/verification/evidence/model-manifest.json'))
args=parser.parse_args()
results=[]
for path in sorted(args.root.rglob('*')):
    if path.suffix.lower() not in ('.ply','.spz'): continue
    start=time.perf_counter()
    with path.open('rb') as source: prefix=source.read(65536)
    count=degree=None
    if path.suffix.lower()=='.ply':
        header=prefix.split(b'end_header')[0].decode('ascii',errors='replace')
        for line in header.splitlines():
            if line.startswith('element vertex '): count=int(line.split()[-1])
        rest=sum(line.startswith('property float f_rest_') for line in header.splitlines())
        degree={0:0,9:1,24:2,45:3}.get(rest)
    else:
        if prefix[:2]==b'\x1f\x8b':
            with gzip.open(path,'rb') as compressed: header=compressed.read(32)
        else: header=prefix[:32]
        if header[:4]==b'NGSP': count=struct.unpack_from('<I',header,8)[0];degree=header[12]
    digest=hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda:source.read(8*1024*1024),b''): digest.update(block)
    record={'name':path.relative_to(args.root).as_posix(),'bytes':path.stat().st_size,'count':count,'degree':degree,'sha256':digest.hexdigest(),'hashSeconds':round(time.perf_counter()-start,3)}
    results.append(record);print(json.dumps(record),flush=True)
args.output.parent.mkdir(parents=True,exist_ok=True)
args.output.write_text(json.dumps({'rootSource':'user-supplied local fixtures; license not redistributed','models':results},indent=2),encoding='utf8')
