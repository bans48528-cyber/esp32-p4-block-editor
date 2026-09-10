"""Rasterize supplied filled SVG paths; redo is the exact mirror of undo."""
from pathlib import Path
import sys,xml.etree.ElementTree as ET
sys.path.insert(0,'C:/esp/board-art-tools')
from svg.path import parse_path,Move,Close,Line
from PIL import Image,ImageDraw,ImageOps
root=Path(__file__).resolve().parents[1]
images={}
for name in ('home','undo','save','clear'):
 tree=ET.parse(root/'assets/toolbar'/f'{name}.svg').getroot()
 im=Image.new('RGBA',(128,128));draw=ImageDraw.Draw(im)
 for el in tree.iter('{http://www.w3.org/2000/svg}path'):
  contour=[]
  def paint():
   if len(contour)>2:draw.polygon(contour,fill=el.get('fill','#000000'))
  for seg in parse_path(el.get('d')):
   if isinstance(seg,Move):
    paint();contour=[]
    contour.append((seg.end.real/8,seg.end.imag/8))
   else:
    count=1 if isinstance(seg,(Line,Close)) else 48
    for j in range(1,count+1):
     p=seg.point(j/count);contour.append((p.real/8,p.imag/8))
  paint()
 images[name]=im.resize((32,32),Image.Resampling.LANCZOS)
images['redo']=ImageOps.mirror(images['undo'])
code='#include "toolbar_icons.h"\n'
for name,im in images.items():
 data=im.tobytes('raw','BGRA')
 code+=f'static const uint8_t pixels_{name}[]={{'+','.join(map(str,data))+'};\n'
 code+=f'const lv_image_dsc_t toolbar_{name}={{.header={{.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_ARGB8888,.w=32,.h=32,.stride=128}},.data_size=4096,.data=pixels_{name}}};\n'
(root/'main/toolbar_icons.c').write_text(code)
(root/'main/toolbar_icons.h').write_text('#pragma once\n#include "lvgl.h"\n'+''.join(f'extern const lv_image_dsc_t toolbar_{name};\n' for name in images))
