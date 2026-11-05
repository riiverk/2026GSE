# 엔진의 DrawSpr / SpriteAtlas 계산을 그대로 흉내 내 마을 한 장면을 합성 (검수용)
import sys, math
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
OUT = sys.argv[1] if len(sys.argv) > 1 else 'mock_scene.png'
import os
SP = os.environ.get('GG_SPRITES', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Data', 'Sprites')) + '/'
atlas = Image.open(SP + 'sprites.png').convert('RGBA')
AW, AH = atlas.size
anims = {}; statics = {}; cur = None; scale = 1
for ln in open(SP + 'sprites.txt', encoding='utf-8'):
    t = ln.split()
    if not t or t[0].startswith('#'): continue
    if t[0] == 'anim':
        cur = dict(name=t[1], dirs=int(t[2]), frames=int(t[3]), fps=float(t[4]), loop=int(t[5]), scale=int(t[6]), f=[]); anims[t[1]] = cur
    elif t[0] == 'sprite':
        cur = dict(name=t[1], scale=int(t[2]), f=[]); statics[t[1]] = cur
    elif t[0] == 'f':
        x, y, w, h, ox, oy = map(int, t[1:]); s = cur['scale']
        cur['f'].append(dict(x=x, y=y, w=w, h=h, ox=ox / s, oy=oy / s, sw=w / s, sh=h / s))

def dir_from_world(fx, fy):
    a = math.atan2(fy, fx); k = int(math.floor(a / 0.785398163 + 0.5))
    m = {1: (0, False), 0: (1, False), -1: (2, False), -2: (3, False), -3: (4, False), 4: (3, True), -4: (3, True), 3: (2, True), 2: (1, True)}
    return m[k]

W, H = 1280, 720
img = Image.new('RGBA', (W, H), (118, 146, 92, 255))
d = ImageDraw.Draw(img)
TW, TH = 72, 36
cam = (64.0, 70.0)
def iso(wx, wy):
    sx = (wx - wy) * TW / 2; sy = (wx + wy) * TH / 2
    cx = (cam[0] - cam[1]) * TW / 2; cy = (cam[0] + cam[1]) * TH / 2
    return sx - cx + W / 2, sy - cy + H / 2
# 길 마름모
for x in range(56, 74):
    for y in range(66, 76):
        if 67 <= y <= 69 or 63 <= x <= 65:
            cx, cy = iso(x + 0.5, y + 0.5)
            d.polygon([(cx, cy - 18), (cx + 36, cy), (cx, cy + 18), (cx - 36, cy)], fill=(186, 168, 135, 255))

def shadow(sx, sy, w, h, a):
    lay = Image.new('RGBA', (W, H), (0, 0, 0, 0)); dd = ImageDraw.Draw(lay)
    dd.ellipse([sx - w / 2, sy - h / 2, sx + w / 2, sy + h / 2], fill=(0, 0, 0, int(255 * a)))
    lay = lay.filter(ImageFilter.GaussianBlur(3)); img.alpha_composite(lay)

def draw(fr, sx, sy, k=1.0, flip=False):
    crop = atlas.crop((fr['x'], fr['y'], fr['x'] + fr['w'], fr['y'] + fr['h']))
    w = max(1, round(fr['sw'] * k)); h = max(1, round(fr['sh'] * k))
    crop = crop.resize((w, h), Image.LANCZOS)
    if flip:
        crop = crop.transpose(Image.FLIP_LEFT_RIGHT); left = sx - (fr['ox'] + fr['sw']) * k
    else:
        left = sx + fr['ox'] * k
    top = sy + fr['oy'] * k
    img.alpha_composite(crop, (int(round(left)), int(round(top))))

items = []
houses = [(57, 65, False), (57, 70, True), (61, 61.5, False), (66, 61.5, True), (72.5, 65.5, False), (72.5, 70.5, True)]
for (x, y, f) in houses: items.append((x + y, 'house', x, y, f))
for (x, y) in [(55, 74), (60, 77), (70, 75), (53, 68), (75, 62)]: items.append((x + y, 'tree', x, y, (x % 2) == 0))
items.append((66.8 + 72.5, 'pot', 66.8, 72.5, False))
items.append((cam[0] + cam[1], 'rino', cam[0], cam[1], (1, 1)))
items.append((cam[0] - 1.1 + cam[1] - 0.3, 'tartu', cam[0] - 1.1, cam[1] - 0.3, (1, 0.2)))
for k, (x, y, dx, dy) in enumerate([(66, 67, 1, 0), (67.5, 68.5, -1, 0.3), (62, 71, 0, 1)]):
    items.append((x + y, 'tom', x, y, (dx, dy, k)))
# 소품·잡몹·사슴 (있을 때만)
extra_props = [('pine', 52, 72), ('bush', 58.5, 75.5), ('rock', 60.5, 74.2), ('well', 69, 66),
	('crate', 70.5, 69), ('barrel', 71.2, 68.2), ('fence', 62, 65.2), ('fence', 62.8, 64.4),
	('vine_grapes', 75, 72), ('vine', 76, 71), ('reed', 70, 77), ('herb', 63, 70.5),
	('tent_canvas', 55, 62), ('tent_red', 57.5, 60.5), ('wagon', 74, 66.5), ('stall_1', 67.5, 63.2),
	('stall_3', 69.5, 62.2), ('campfire', 59, 67.5)]
for (nm, x, y) in extra_props:
	if nm in statics:
		items.append((x + y, 'static', x, y, nm))
for k, (sp, x, y) in enumerate([('potato', 61, 69), ('bean', 65.5, 70.5), ('mush', 60, 71.5)]):
	if sp + '.idle' in anims:
		items.append((x + y, 'mob', x, y, sp))
if 'lakedeer.idle' in anims:
	items.append((72 + 73, 'deer', 72, 73, None))
items.sort(key=lambda t: t[0])
for _, kind, x, y, extra in items:
    sx, sy = iso(x, y)
    if kind == 'house':
        shadow(sx + 6, sy + 6, 128, 50, 0.34); draw(statics['house']['f'][0], sx, sy, 1.0, extra)
    elif kind == 'tree':
        shadow(sx + 5, sy + 2, 74, 30, 0.3); draw(statics['tree']['f'][0], sx, sy, 1.0, extra)
    elif kind == 'pot':
        shadow(sx + 3, sy + 3, 58, 26, 0.3); draw(statics['pot_hearth']['f'][0], sx, sy)
        lay = Image.new('RGBA', (W, H), (0, 0, 0, 0)); ImageDraw.Draw(lay).ellipse([sx - 10, sy - 26, sx + 10, sy - 2], fill=(255, 170, 60, 220)); img.alpha_composite(lay)
        draw(statics['pot_cauldron']['f'][0], sx, sy); draw(statics['dish_icon']['f'][0], sx + 40, sy + 12)
    elif kind == 'rino':
        di, fl = dir_from_world(*extra); a = anims['rino.walk']; shadow(sx, sy, 32, 14, 0.34)
        draw(a['f'][di * a['frames'] + 2], sx, sy, 0.9, fl)
    elif kind == 'tartu':
        di, fl = dir_from_world(*extra); a = anims['tartu.sniff']; shadow(sx, sy, 28, 11, 0.3)
        draw(a['f'][di * a['frames'] + 3], sx, sy, 1.15, fl)
    elif kind == 'static':
        draw(statics[extra]['f'][0], sx, sy)
    elif kind == 'mob':
        a = anims[extra + '.idle']; shadow(sx + 2, sy + 1, 36, 15, 0.28)
        draw(a['f'][0], sx, sy, 1.3)
    elif kind == 'deer':
        a = anims['lakedeer.idle']; shadow(sx + 4, sy + 2, 82, 32, 0.34)
        draw(a['f'][3 * a['frames']], sx, sy, 1.0, True)
    elif kind == 'tom':
        di, fl = dir_from_world(extra[0], extra[1]); a = anims['tomatoma.hop' if extra[2] != 1 else 'tomatoma.idle']; shadow(sx + 2, sy + 1, 30, 13, 0.28)
        draw(a['f'][di * a['frames'] + 2], sx, sy, 1.3, fl)
cl = statics['dish_closeup']['f'][0]
img.save(OUT)
# 클로즈업 별도
c = atlas.crop((cl['x'], cl['y'], cl['x'] + cl['w'], cl['y'] + cl['h']))
bg = Image.new('RGBA', c.size, (40, 34, 30, 255)); bg.alpha_composite(c); bg.save(OUT.replace('.png', '_dish.png'))
print('ok')
