# -----------------------------------------------------------------------------
# pack_atlas.py — renders/*/ 의 스프라이트를 아틀라스 한 장으로 묶는다.
#
#   python3 pack_atlas.py [renders_dir] [out_dir]
#
# 출력 (out_dir, 기본 SimpleGame/Data/Sprites):
#   sprites.png     컬러 (스트레이트 알파, 투명 픽셀에 색 번짐 처리)
#   sprites_n.png   노멀 (카메라 공간, 같은 배치) — 엔진은 아직 읽지 않는다. 나중에
#                   스프라이트 동적 조명(횃불, 낮밤)을 붙일 때 쓴다
#   sprites.txt     프레임 목록. 형식은 SpriteAtlas.cpp 와 맞춘다
#
# 프레임마다 투명 테두리를 잘라내고 (ox, oy) = 잘린 사각형 좌상단 - 피벗 을 적는다.
# 프레임 사이 여백 4px — 밉맵 레벨 2 까지 이웃 프레임이 번지지 않는다.
# -----------------------------------------------------------------------------
import os, sys, json
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
RENDERS = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', 'renders')
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, '..', '..', 'Data', 'Sprites')
PAD = 4
ATLAS_W = 4096
DIRS = ['s', 'se', 'e', 'ne', 'n']

# 아틀라스에 넣는 순서 (sprites.txt 순서). 엔진은 이름으로 찾으므로 순서는 자유
ORDER = ['rino', 'rino_ladle', 'tartu', 'tomatoma', 'potato', 'bean', 'mush', 'boar', 'lakedeer',
	'villager', 'villager_cloth', 'villager_hat', 'house', 'landmarks', 'tree', 'pot', 'dish',
	'items', 'props']


def load(path):
	return np.array(Image.open(path).convert('RGBA'))


def trim(c):
	a = c[..., 3]
	ys, xs = np.nonzero(a > 1)
	if len(xs) == 0:
		return 0, 0, 1, 1
	x0 = max(0, xs.min() - 1); x1 = min(c.shape[1], xs.max() + 2)
	y0 = max(0, ys.min() - 1); y1 = min(c.shape[0], ys.max() + 2)
	return x0, y0, x1, y1


blocks = []      # ('anim'|'sprite', header_tuple, [frame dicts])
frames = []      # 모든 프레임 (패킹 대상)


def add_frame(asset_dir, base, pivot):
	c = load(os.path.join(asset_dir, base + '.png'))
	n = load(os.path.join(asset_dir, base + '_n.png'))
	x0, y0, x1, y1 = trim(c)
	fr = dict(name=base, c=c[y0:y1, x0:x1], n=n[y0:y1, x0:x1],
		ox=x0 - pivot[0], oy=y0 - pivot[1], w=x1 - x0, h=y1 - y0)
	frames.append(fr)
	return fr


for asset in ORDER:
	d = os.path.join(RENDERS, asset)
	mp = os.path.join(d, 'meta.json')
	if not os.path.exists(mp):
		print('skip (no meta):', asset)
		continue
	meta = json.load(open(mp))
	scale = meta['scale']
	if 'anims' in meta:
		pivot = meta['pivot']
		for an in meta['anims']:
			fl = []
			anchors = an.get('anchors')
			for di, dn in enumerate(DIRS):
				for fi in range(an['frames']):
					fr = add_frame(d, '%s.%s.%s.%d' % (asset, an['name'], dn, fi), pivot)
					if anchors:
						fr['anchor'] = anchors[di * an['frames'] + fi]
					fl.append(fr)
			blocks.append(('anim', ('%s.%s' % (asset, an['name']), len(DIRS), an['frames'],
				an['fps'], 1 if an['loop'] else 0, scale), fl))
	else:
		for st in meta['statics']:
			fr = add_frame(d, st['name'], st['pivot'])
			blocks.append(('sprite', (st['name'], scale), [fr]))

# ---- MaxRects 패킹 (가장 짧은 변 맞춤) ----
# 선반 패킹은 채움률이 55% 남짓이라 4096x4096 이 필요했다. MaxRects 는 80% 안팎이라
# 세로를 필요한 만큼만 (16 의 배수) 잡는다. GL 은 2의 거듭제곱이 아닌 텍스처도 밉맵된다.
W = ATLAS_W


def pack(height):
	free = [(0, 0, W, height)]
	place = {}
	order = sorted(range(len(frames)), key=lambda i: -(max(frames[i]['w'], frames[i]['h']) * 4096
		+ min(frames[i]['w'], frames[i]['h'])))
	for i in order:
		fw = frames[i]['w'] + PAD
		fh = frames[i]['h'] + PAD
		best = None
		for (fx, fy, fW, fH) in free:
			if fw <= fW and fh <= fH:
				short = min(fW - fw, fH - fh)
				key = (fy + fh, short, fx)       # 위쪽부터 채운다 -> 아래가 비어 세로를 줄일 수 있다
				if best is None or key < best[0]:
					best = (key, fx, fy)
		if best is None:
			return None
		_, px, py = best
		place[i] = (px, py)
		# 겹치는 빈 사각형을 쪼갠다
		nf = []
		for (fx, fy, fW, fH) in free:
			if px >= fx + fW or px + fw <= fx or py >= fy + fH or py + fh <= fy:
				nf.append((fx, fy, fW, fH))
				continue
			if px > fx:
				nf.append((fx, fy, px - fx, fH))
			if px + fw < fx + fW:
				nf.append((px + fw, fy, fx + fW - (px + fw), fH))
			if py > fy:
				nf.append((fx, fy, fW, py - fy))
			if py + fh < fy + fH:
				nf.append((fx, py + fh, fW, fy + fH - (py + fh)))
		# 다른 사각형에 완전히 들어가는 것은 버린다
		nf.sort(key=lambda r: -r[2] * r[3])
		kept = []
		for r in nf:
			if r[2] <= 0 or r[3] <= 0:
				continue
			if any(r[0] >= k[0] and r[1] >= k[1] and r[0] + r[2] <= k[0] + k[2] and r[1] + r[3] <= k[1] + k[3]
					for k in kept):
				continue
			kept.append(r)
		free = kept
	return place


area = sum((f['w'] + PAD) * (f['h'] + PAD) for f in frames)
H = max(256, int(area / W / 0.92) // 16 * 16)
while True:
	pl = pack(H - PAD)
	if pl is not None:
		break
	H += 64
for i, (px, py) in pl.items():
	frames[i]['x'] = px + PAD
	frames[i]['y'] = py + PAD
used = max(f['y'] + f['h'] for f in frames) + PAD
H = (used + 15) // 16 * 16

col = np.zeros((H, W, 4), np.uint8)
nrm = np.zeros((H, W, 4), np.uint8)
nrm[..., 0] = 128; nrm[..., 1] = 128; nrm[..., 2] = 255
for f in frames:
	col[f['y']:f['y'] + f['h'], f['x']:f['x'] + f['w']] = f['c']
	nrm[f['y']:f['y'] + f['h'], f['x']:f['x'] + f['w']] = f['n']


def bleed(img, iters=8):
	"""알파 0 인 픽셀에 이웃 색을 번지게 한다. 선형 필터·밉맵이 검은 테두리를
	끌어오지 않게 (스트레이트 알파의 고전적 문제)."""
	rgb = img[..., :3].astype(np.float32)
	filled = img[..., 3] > 0
	for _ in range(iters):
		acc = np.zeros_like(rgb)
		cnt = np.zeros(filled.shape, np.float32)
		for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (1, 1), (-1, 1), (1, -1)):
			sf = np.roll(np.roll(filled, dy, 0), dx, 1)
			sc = np.roll(np.roll(rgb, dy, 0), dx, 1)
			acc += sc * sf[..., None]
			cnt += sf
		new = (~filled) & (cnt > 0)
		rgb[new] = acc[new] / cnt[new][:, None]
		filled = filled | new
	img[..., :3] = np.clip(rgb + 0.5, 0, 255).astype(np.uint8)
	return img


col = bleed(col)
os.makedirs(OUT, exist_ok=True)
Image.fromarray(col, 'RGBA').save(os.path.join(OUT, 'sprites.png'), optimize=True)
Image.fromarray(nrm, 'RGBA').save(os.path.join(OUT, 'sprites_n.png'), optimize=True)

with open(os.path.join(OUT, 'sprites.txt'), 'w', encoding='utf-8', newline='\n') as fo:
	fo.write('# gg-sprites/1 — Art/scripts/pack_atlas.py 가 생성. 손으로 고치지 말 것\n')
	fo.write('#\n')
	fo.write('# atlas  <png> <w> <h>\n')
	fo.write('# anim   <name> <dirs> <frames> <fps> <loop> <scale>   그 뒤 dirs*frames 개의 f 줄\n')
	fo.write('#        방향 순서 s se e ne n (화면 기준), 방향마다 frames 장\n')
	fo.write('# sprite <name> <scale>                                 그 뒤 f 줄 하나\n')
	fo.write('# f      <x> <y> <w> <h> <ox> <oy>   아틀라스 px. (ox, oy) = 피벗(발) 기준 좌상단\n')
	fo.write('#        scale 로 나누면 게임 1배 px\n')
	fo.write('#        뒤에 <ax> <ay> 가 붙으면 피벗 기준 앵커 (주민 몸 = 머리, 모자 = 모자 자리)\n')
	fo.write('atlas sprites.png %d %d\n' % (W, H))
	for kind, hdr, fl in blocks:
		if kind == 'anim':
			fo.write('anim %s %d %d %g %d %d\n' % hdr)
		else:
			fo.write('sprite %s %d\n' % hdr)
		for f in fl:
			fo.write('f %d %d %d %d %d %d' % (f['x'], f['y'], f['w'], f['h'], f['ox'], f['oy']))
			if 'anchor' in f:
				fo.write(' %d %d' % (round(f['anchor'][0]), round(f['anchor'][1])))
			fo.write('\n')

print('atlas %dx%d, %d frames, %d blocks, fill %.0f%%' % (W, H, len(frames), len(blocks),
	100.0 * sum(f['w'] * f['h'] for f in frames) / (W * H)))
