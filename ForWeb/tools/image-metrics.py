"""Equal-camera comparison: report full-frame and foreground ROI, preserve heatmap."""
import argparse,json
from pathlib import Path
import numpy as np
from PIL import Image
from skimage.metrics import structural_similarity
p=argparse.ArgumentParser();p.add_argument('directory',type=Path);args=p.parse_args();rows=[]
for native in args.directory.glob('*-native.rgba'):
    spark=native.with_name(native.name.replace('-native.rgba','-spark.rgba'))
    if not spark.exists():continue
    a=np.fromfile(native,dtype=np.uint8).reshape(1080,1920,4)[:,:,:3];b=np.fromfile(spark,dtype=np.uint8).reshape(1080,1920,4)[:,:,:3]
    mask=np.any(a>5,axis=2)|np.any(b>5,axis=2);ys,xs=np.where(mask)
    if len(xs)==0:raise ValueError('Blank comparison images')
    x0,x1=max(0,xs.min()-8),min(1920,xs.max()+9);y0,y1=max(0,ys.min()-8),min(1080,ys.max()+9)
    diff=np.abs(a.astype(float)-b);roi=(slice(y0,y1),slice(x0,x1))
    row={'model':native.stem.replace('-native',''),'ssim':float(structural_similarity(a,b,channel_axis=2,data_range=255)), 'foregroundSsim':float(structural_similarity(a[roi],b[roi],channel_axis=2,data_range=255)), 'foregroundMae':float(diff[mask].mean()),'roi':[int(x0),int(y0),int(x1),int(y1)],'foregroundPixels':int(mask.sum())};rows.append(row)
    for suffix,array in [('native',a),('spark',b),('difference',np.clip(diff*4,0,255).astype(np.uint8))]:
        Image.fromarray(array).save(args.directory/(row['model']+'-'+suffix+'.png'))
    Image.fromarray(np.concatenate([a,b,np.clip(diff*4,0,255).astype(np.uint8)],axis=1)).resize((1440,270)).save(args.directory/(row['model']+'-comparison.png'))
(args.directory/'image-quality.json').write_text(json.dumps(rows,indent=2),encoding='utf8');print(json.dumps(rows,indent=2))
