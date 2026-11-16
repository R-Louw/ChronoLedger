"""Inspect deterministic RGBA readbacks, optionally adding previous-study crops."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageChops
import sys
root = Path(sys.argv[1] if len(sys.argv)>1 else '/tmp/chronoledger-cursor-after')
before = Path(sys.argv[2]) if len(sys.argv)>2 else None

def frame(path, hz, t):
    with path.open('rb') as f:
        f.seek(round(t*hz)*800*360*4)
        return Image.frombytes('RGBA',(800,360),f.read(800*360*4)).convert('RGB')

for persistence in ['.24','1']:
    for t in [5/6,17/12,2.5,8]:
        a=frame(root/f'60-{persistence}.rgba',60,t)
        b=frame(root/f'144-{persistence}.rgba',144,t)
        assert ImageChops.difference(a,b).getbbox() is None, (persistence,t)
for t in [2.5,3.2,8]:
    assert ImageChops.difference(frame(root/'60-1.rgba',60,t),frame(root/'stall.rgba',60,t)).getbbox() is None,t
for persistence in ['.24','1']:
    a=frame(root/f'60-{persistence}.rgba',60,8).crop((70,120,600,180))
    assert ImageChops.difference(a,Image.new('RGB',a.size,a.getpixel((0,0)))).getbbox() is None
rows=[]
for label,stem in [('default','.24'),('maximum','1')]:
    if before:
        rows.append((f'Before {label}',before/('default.rgba' if stem=='.24' else 'high.rgba'),60,5/6))
    rows.append((f'After {label}',root/f'60-{stem}.rgba',60,5/6))
rows += [('After maximum 144 Hz',root/'144-1.rgba',144,5/6),
         ('Stopped +0.5 s',root/'60-1.rgba',60,1.7),
         ('Stopped +2 s',root/'60-1.rgba',60,3.2),
         ('Clean after expiry',root/'60-1.rgba',60,8),
         ('Effects bypass',root/'bypass.rgba',60,5/6)]
image=Image.new('RGB',(800,len(rows)*145),(10,15,12));draw=ImageDraw.Draw(image)
for i,(label,path,hz,t) in enumerate(rows):
    image.paste(frame(path,hz,t).crop((70,115,620,245)),(200,i*145+15))
    draw.text((8,i*145+25),label,fill='white')
image.save(root/'comparison.png')
print('Matched-time 60/144 Hz: identical at both persistence settings; stall resume identical; old path clean at expiry.')
