"""Rasterize the reference project's own SVG icon paths into local LVGL assets."""
from pathlib import Path
import sys,re,json
sys.path.insert(0,'C:/esp/board-art-tools')
from svg.path import parse_path, Move
from PIL import Image,ImageDraw
root=Path(__file__).resolve().parents[1]
reference_path=Path('D:/BaiduSyncdisk/2026/8月/应用构思/visual-programming-cards/app.js')
if reference_path.exists():
 reference=reference_path.read_text(encoding='utf-8')
 paths=dict(re.findall(r'"([\w-]+)": "(M[^"\n]+)"',reference))
else:
 paths=json.loads((root/'host/icon-paths.json').read_text(encoding='utf-8'))
paths['wait-time']='M21 10h22M21 54h22M24 12v8c0 5 4 9 8 12 4-3 8-7 8-12v-8M24 52v-8c0-5 4-9 8-12 4 3 8 7 8 12v8M27 26h10M27 42h10'
paths['ultrasonic-sensor']='M16 20v24M24 15v34M36 22c4 2 6 6 6 10s-2 8-6 10M45 15c6 4 10 10 10 17s-4 13-10 17'
ids=['motor-forward','wait-time','loop-count','motor-reverse','motor-stop','combo-forward','combo-backward','combo-turn-left','combo-turn-right','combo-stop','ultrasonic-sensor','grayscale-sensor','button-sensor','host-button','loop','play-note']
code='#include "lvgl.h"\n'
for i,name in enumerate(ids,1):
 im=Image.new('RGBA',(160,160));draw=ImageDraw.Draw(im)
 for segment in parse_path(paths[name]):
  if isinstance(segment,Move):continue
  pts=[(segment.point(k/80).real*2.5,segment.point(k/80).imag*2.5) for k in range(81)]
  draw.line(pts,fill='white',width=9)
  for x,y in (pts[0],pts[-1]):draw.ellipse((x-4,y-4,x+4,y+4),fill='white')
 im=im.resize((40,40),Image.Resampling.LANCZOS)
 data=im.tobytes('raw','BGRA')
 code+=f'static const uint8_t pixels_{i}[]={{'+','.join(map(str,data))+'};\n'
 code+=f'static const lv_image_dsc_t image_{i}={{.header={{.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_ARGB8888,.w=40,.h=40,.stride=160}},.data_size=6400,.data=pixels_{i}}};\n'
code+='const lv_image_dsc_t *block_icons[]={NULL,'+','.join(f'&image_{i}' for i in range(1,17))+',NULL};\n'
(root/'main/block_icons.c').write_text(code,encoding='utf-8')
(root/'host/icon-paths.json').write_text(json.dumps(paths,ensure_ascii=False,indent=2),encoding='utf-8')
print('Generated 16 reference icons')
