from pathlib import Path
from PIL import Image, ImageDraw

# tools/ 的上一级就是仓库根目录，clone 到任何位置都能跑。
OUT = Path(__file__).resolve().parent.parent / "resources"
C = 1024

def rr(draw, box, radius, fill, outline=None, width=0):
    draw.rounded_rectangle(box, radius=radius, fill=fill, outline=outline, width=width)

def make_md():
    im = Image.new("RGBA", (C, C), (0,0,0,0)); d = ImageDraw.Draw(im)
    # Page and folded corner
    left, top, right, bottom = int(C*.12), int(C*.055), int(C*.88), int(C*.945)
    rr(d, (left, top, right, bottom), int(C*.055), "#FCF8ED", "#183428", int(C*.018))
    fold = int(C*.19)
    d.polygon([(right-fold, top), (right, top+fold), (right-fold, top+fold)], fill="#C59A52")
    d.line([(right-fold, top), (right-fold, top+fold), (right, top+fold)], fill="#183428", width=int(C*.014), joint="curve")
    # Emerald title band
    rr(d, (int(C*.23), int(C*.19), int(C*.73), int(C*.30)), int(C*.025), "#1E7A61")
    # Markdown text lines
    for i, (y, w) in enumerate(((0.40, .52), (0.51, .40), (0.62, .47))):
        x0=int(C*.23); x1=x0+int(C*w)
        rr(d, (x0, int(C*y), x1, int(C*y)+int(C*.026)), int(C*.013), "#7E9B8E")
    # Gold footer rule
    rr(d, (int(C*.23), int(C*.78), int(C*.55), int(C*.815)), int(C*.017), "#C59A52")
    return im

def make_mdge():
    im = Image.new("RGBA", (C, C), (0,0,0,0)); d = ImageDraw.Draw(im)
    # Folder tab
    rr(d, (int(C*.10), int(C*.20), int(C*.46), int(C*.34)), int(C*.045), "#173D31")
    # Two sheets peeking from folder
    rr(d, (int(C*.22), int(C*.12), int(C*.58), int(C*.55)), int(C*.035), "#F2EAD8", "#183428", int(C*.012))
    rr(d, (int(C*.32), int(C*.075), int(C*.70), int(C*.50)), int(C*.035), "#FCF8ED", "#183428", int(C*.012))
    for i, (y, w) in enumerate(((0.19, .26), (0.29, .21), (0.39, .24))):
        x0=int(C*.39); x1=x0+int(C*w)
        rr(d, (x0, int(C*y), x1, int(C*y)+int(C*.024)), int(C*.012), "#1E7A61")
    # Folder body gold with dark green rim
    rr(d, (int(C*.075), int(C*.31), int(C*.925), int(C*.90)), int(C*.075), "#C59A52", "#183428", int(C*.018))
    # Folder lip / dark inner band
    rr(d, (int(C*.075), int(C*.31), int(C*.925), int(C*.44)), int(C*.055), "#A97E3C")
    rr(d, (int(C*.12), int(C*.43), int(C*.88), int(C*.83)), int(C*.045), "#1E7A61")
    # Three small project cards
    for i, (dy, fill) in enumerate(((0.0, "#FCF8ED"), (0.10, "#E8DDC4"), (0.20, "#D6C9AC"))):
        rr(d, (int(C*(.23+dy*.18)), int(C*(.53+dy)), int(C*(.68+dy*.18)), int(C*(.66+dy))), int(C*.025), fill, "#183428", int(C*.010))
    return im

for name, fn in (("md-file.ico", make_md), ("mdge-file.ico", make_mdge)):
    im = fn()
    im.save(OUT / name, sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
    im.resize((256,256), Image.Resampling.LANCZOS).save(OUT / (name.replace('.ico','-preview.png')))
    print(OUT / name)
