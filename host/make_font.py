"""Generate the local prototype glyph subset from an installed font (Pillow)."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import re
root=Path(__file__).resolve().parents[1]
source=''.join((root/f'main/{name}.c').read_text(encoding='utf-8') for name in ['editor','model'])
source+=(root/'main/home.inc').read_text(encoding='utf-8')
chars=set(''.join(re.findall(r'"([^"\n]*)"', source))) | set(chr(i) for i in range(32,127))
chars=sorted(c for c in chars if ord(c)>=32)
font=ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',18)
bitmap=[]; desc=['{0}']
for char in chars:
    left,top,right,bottom=font.getbbox(char,anchor='ls');w=right-left;h=bottom-top
    im=Image.new('L',(max(w,1),max(h,1)));ImageDraw.Draw(im).text((-left,-top),char,font=font,fill=255,anchor='ls')
    data=[round(v/17) for v in im.getdata()] if w*h else []
    offset=len(bitmap)
    bitmap.extend((data[i]<<4)|(data[i+1] if i+1<len(data) else 0) for i in range(0,len(data),2))
    desc.append(f'{{.bitmap_index={offset},.adv_w={round(font.getlength(char)*16)},.box_w={w},.box_h={h},.ofs_x={left},.ofs_y={-bottom}}}')
code='#include "lvgl.h"\n'
code+='static const uint8_t bitmap[]={'+','.join(map(str,bitmap))+'};\n'
code+='static const lv_font_fmt_txt_glyph_dsc_t glyphs[]={'+','.join(desc)+'};\n'
code+='static const uint16_t unicode[]={'+','.join(str(ord(c)-32) for c in chars)+'};\n'
code+='static const lv_font_fmt_txt_cmap_t maps[]={{.range_start=32,.range_length='+str(ord(chars[-1])-31)+',.glyph_id_start=1,.unicode_list=unicode,.list_length='+str(len(chars))+',.type=LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}};\n'
code+='static const lv_font_fmt_txt_dsc_t dsc={.glyph_bitmap=bitmap,.glyph_dsc=glyphs,.cmaps=maps,.cmap_num=1,.bpp=4};\n'
code+='const lv_font_t board_font={.get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt,.get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,.line_height=24,.base_line=5,.dsc=&dsc,.fallback=&lv_font_montserrat_14};\n'
(root/'main/board_font.c').write_text(code,encoding='utf-8')
print(f'Generated {len(chars)} glyphs, {len(bitmap)} bitmap bytes')
