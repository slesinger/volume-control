# Usage: python3 gen_volume_font.py 92 600 ../volctrl/custom_components/vol_ctrl/volume_font.h  (size, weight; needs Orbitron[wght].ttf as orb.ttf, SIL OFL)
from PIL import ImageFont, Image
import sys
SIZE=int(sys.argv[1]); WGHT=int(sys.argv[2])
f=ImageFont.truetype('orb.ttf',SIZE)
try: f.set_variation_by_axes([WGHT])
except Exception as e: print('variation failed',e,file=sys.stderr)
first,last=0x2D,0x39
bitmap=bytearray(); glyphs=[]
asc,desc=f.getmetrics()
for c in range(first,last+1):
    ch=chr(c)
    adv=int(round(f.getlength(ch)))
    # draw on canvas with baseline at y=asc
    im=Image.new('L',(adv+SIZE,asc+desc+10),0)
    from PIL import ImageDraw
    d=ImageDraw.Draw(im); d.text((SIZE//2,0),ch,font=f,fill=255)
    bb=im.point(lambda v:255 if v>=128 else 0).getbbox()
    if bb is None:
        glyphs.append((len(bitmap),0,0,adv,0,0)); continue
    x0,y0,x1,y1=bb; w,h=x1-x0,y1-y0
    px=im.load(); bits=[]
    for y in range(y0,y1):
        for x in range(x0,x1): bits.append(1 if px[x,y]>=128 else 0)
    off=len(bitmap)
    for i in range(0,len(bits),8):
        b=0
        for k in range(8):
            b=(b<<1)|(bits[i+k] if i+k<len(bits) else 0)
        bitmap.append(b)
    glyphs.append((off,w,h,adv,x0-SIZE//2,y0-asc))
out=open(sys.argv[3],'w')
out.write('#pragma once\n// Orbitron (SIL Open Font License) digits rendered to Adafruit-GFX format by a script; chars 0x2D..0x39\n#include <pgmspace.h>\n#include <TFT_eSPI.h>  // brings in GFXfont\n')
out.write('static const uint8_t OrbitronDigitsBitmaps[] PROGMEM = {\n'+','.join('0x%02X'%b for b in bitmap)+'};\n')
out.write('static const GFXglyph OrbitronDigitsGlyphs[] PROGMEM = {\n'+',\n'.join('{%d,%d,%d,%d,%d,%d}'%g for g in glyphs)+'};\n')
out.write('static const GFXfont OrbitronDigits PROGMEM = {(uint8_t *)OrbitronDigitsBitmaps, (GFXglyph *)OrbitronDigitsGlyphs, 0x%X, 0x%X, %d};\n'%(first,last,asc+desc))
print(len(bitmap),'bytes', [g[1:3] for g in glyphs[3:5]])
