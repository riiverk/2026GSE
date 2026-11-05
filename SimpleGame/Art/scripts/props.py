# -----------------------------------------------------------------------------
# props.py — 고향 마을·숲·호숫가 소품 키트
#
#   python3 props.py [이름 ...]        이름을 주면 그것만 (기본: 전부)
#   GG_STAGE=model                    굽기 전 형태만 한 장에 모아 미리보기
#
# 소나무, 덤불, 바위, 우물, 나무 상자, 술통, 울타리, 포도나무(+송이), 갈대,
# 햇살풀, 천막(생천·적갈), 포장마차, 노점 5색, 모닥불 자리.
# 소품마다 텍스처 세트를 따로 굽고(512), 원점(발)에 둔 채 스프라이트를 찍은 뒤
# props.blend 에는 격자로 늘어놓아 저장한다.
# 불꽃·반짝임·흔들림은 엔진이 맡는다 (모닥불 불꽃, 햇살풀 후광, 갈대·소나무 흔들림).
# -----------------------------------------------------------------------------
import sys, os, math, random
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy, bmesh
from mathutils import Vector
import ggcommon as G
from ggcommon import mball, mell, mcap, lin
import env as E

STAGE = os.environ.get('GG_STAGE', 'all')
ONLY = [a for a in sys.argv[1:]]
random.seed(11)

G.reset(samples=48)
G.light_rig()

# 노점 차양 색과 진열품 (Game.cpp DrawStall 과 같은 순서)
AWNING = [(0.80, 0.36, 0.30), (0.36, 0.52, 0.66), (0.84, 0.70, 0.30), (0.46, 0.62, 0.40),
	(0.64, 0.44, 0.66)]
GOODS = [(0.90, 0.40, 0.26), (0.95, 0.82, 0.36), (0.60, 0.72, 0.36), (0.72, 0.46, 0.30),
	(0.86, 0.84, 0.78)]


def lumpy(ob, amp, freq=9.0, seed=0.0):
	c = Vector((0, 0, 0))
	for v in ob.data.vertices:
		c += v.co
	c /= max(1, len(ob.data.vertices))
	G.deform(ob, lambda p: p + (p - c).normalized() * amp *
		(math.sin(p.x * freq + seed) * math.sin(p.y * freq * 1.1 + seed * 2) *
		math.sin(p.z * freq * 0.9 + seed * 3)))


def mat(name, base, base2, **kw):
	return G.proc_mat(name, lin(base), lin(base2), **kw)


# =============================================================================
# 소품 정의: 각 함수는 (오브젝트 목록) 을 돌려준다. 머티리얼은 소품마다 새로 만든다
# (베이크가 머티리얼을 이미지 기반으로 바꾸므로 소품끼리 공유하면 안 된다)
# =============================================================================

def p_pine(n):
	M = E.mats_common(n)
	ND = mat(n + '_needle', (0.22, 0.38, 0.24), (0.15, 0.28, 0.19), var_scale=4, var_amt=0.8,
		rough=0.75, macro=('wave', 40.0, 6.0, 'Z'), macro_amt=0.9, cavity=0.4,
		normal_strength=1.2, micro=('noise', 160.0), bump_strength=0.2, sheen=0.2,
		height_scale=0.015)
	tr = G.cylinder(n + '_trunk', 0.075, 0.05, 0.62, (0, 0, 0.31), seg=12)
	tr.data.materials.append(M['bark'])
	obs = [tr]
	for k, (r, z, h) in enumerate(((0.44, 0.38, 0.62), (0.35, 0.82, 0.55), (0.24, 1.22, 0.50),
			(0.12, 1.55, 0.32))):
		c = G.cylinder(n + '_tier', r, 0.0, h, (0, 0, z + h / 2), seg=28, caps=True)
		G.subsurf(c, 1)
		# 아래 단 가장자리를 톱니처럼 늘어뜨린다 (바늘잎 뭉치)
		G.deform(c, lambda p, z=z: p + Vector((0, 0, -0.06 * max(0.0,
			math.sin(math.atan2(p.y, p.x) * 9 + k)) * (1.0 if p.z < z + 0.05 else 0.0))))
		lumpy(c, 0.025, 14.0, k)
		c.data.materials.append(ND)
		obs.append(c)
	return obs


def p_bush(n):
	M = E.mats_common(n)
	BER = G.simple_mat(n + '_berry', lin((0.70, 0.12, 0.16)), rough=0.4)
	obs = []
	for (x, y, z, r) in ((-0.10, 0.04, 0.15, 0.20), (0.12, -0.04, 0.17, 0.22),
			(0.02, 0.10, 0.27, 0.19), (0.0, -0.14, 0.12, 0.15)):
		b = G.ico(n + '_leaf', r, (x, y, z), (1, 1, 0.85), sub=3)
		lumpy(b, 0.03, 18.0, x * 10)
		b.data.materials.append(M['leaf'])
		obs.append(b)
	for k in range(7):
		a = random.uniform(-1.2, 2.2); z = random.uniform(0.12, 0.32)
		be = G.sphere(n + '_berry', 0.022, (0.26 * math.cos(a), -0.24 * abs(math.sin(a)) - 0.02, z),
			seg=10, rings=6)
		be.data.materials.append(BER)
		obs.append(be)
	return obs


def p_rock(n):
	M = E.mats_common(n)
	MOSS = mat(n + '_moss', (0.40, 0.50, 0.26), (0.30, 0.40, 0.20), var_scale=8, var_amt=0.6,
		rough=0.9, macro=('pebbles', 60.0), macro_amt=1.0, cavity=0.3, normal_strength=0.8,
		height_scale=0.006)
	a = G.ico(n + '_a', 1.0, (0.0, 0.0, 0.10), (0.30, 0.25, 0.20), sub=3)
	lumpy(a, 0.035, 8.0, 1.0)
	b = G.ico(n + '_b', 1.0, (-0.14, 0.12, 0.17), (0.17, 0.15, 0.15), (0, 0, 30), sub=3)
	lumpy(b, 0.025, 10.0, 3.0)
	for o in (a, b):
		o.data.materials.append(M['stone'])
		o.data.materials.append(MOSS)
		for pl in o.data.polygons:
			if pl.normal.z > 0.65 and pl.center.y > -0.05:
				pl.material_index = 1
	return [a, b]


def p_well(n):
	M = E.mats_common(n)
	WATER = G.simple_mat(n + '_water', lin((0.10, 0.22, 0.28)), rough=0.08, coat=0.5)
	ROPE = mat(n + '_rope', (0.70, 0.60, 0.42), (0.58, 0.48, 0.32), var_scale=20, var_amt=0.5,
		rough=0.9, macro=('wave', 120.0, 1.0, 'Z'), macro_amt=0.8, height_scale=0.003)
	prof = [(0.40, 0.0), (0.41, 0.30), (0.42, 0.33), (0.30, 0.34), (0.29, 0.10), (0.0, 0.10)]
	ring = G.lathe(n + '_ring', prof, seg=40, close_bottom=False)
	ring.data.materials.append(M['stone'])
	water = G.cylinder(n + '_water', 0.295, 0.295, 0.02, (0, 0, 0.24), seg=32)
	water.data.materials.append(WATER)
	obs = [ring, water]
	for s in (1, -1):
		p = G.box(n + '_post', (0.07, 0.07, 0.86), (s * 0.34, 0.0, 0.62))
		G.bevel(p, 0.012, 2)
		p.data.materials.append(M['wood'])
		obs.append(p)
	beam = G.cylinder(n + '_beam', 0.035, 0.035, 0.74, (0, 0, 0.92), (0, 90, 0), seg=12)
	beam.data.materials.append(M['wood'])
	rope = G.cylinder(n + '_rope', 0.010, 0.010, 0.42, (0.0, 0.0, 0.70), seg=8)
	rope.data.materials.append(ROPE)
	buck = G.lathe(n + '_bucket', [(0.06, 0.0), (0.075, 0.10), (0.07, 0.10), (0.0, 0.10)],
		seg=16, loc=(0, 0, 0.40))
	buck.data.materials.append(M['wood'])
	obs += [beam, rope, buck]
	ang = math.atan2(0.30, 0.28)
	for s in (1, -1):
		sl = G.box(n + '_roof', (0.92, 0.42, 0.04), (0, s * 0.15, 1.13),
			(-s * math.degrees(ang), 0, 0))
		G.bevel(sl, 0.012, 2)
		sl.data.materials.append(M['terracotta'])
		obs.append(sl)
	return obs


def p_crate(n):
	M = E.mats_common(n)
	s = 0.44
	b = G.box(n + '_box', (s, s, s), (0, 0, s / 2))
	G.bevel(b, 0.015, 2)
	b.data.materials.append(M['wood'])
	obs = [b]
	# 모서리 덧댄 각목
	for (x, y) in ((1, 1), (1, -1), (-1, 1), (-1, -1)):
		e = G.box(n + '_edge', (0.05, 0.05, s + 0.01), (x * s / 2, y * s / 2, s / 2))
		G.bevel(e, 0.008, 2)
		e.data.materials.append(M['wood'])
		obs.append(e)
	for z in (0.03, s - 0.03):
		for (sx, sy, rot) in ((0, -s / 2, 0), (0, s / 2, 0), (s / 2, 0, 90), (-s / 2, 0, 90)):
			e = G.box(n + '_band', (s, 0.04, 0.05), (sx, sy, z), (0, 0, rot))
			G.bevel(e, 0.008, 2)
			e.data.materials.append(M['wood'])
			obs.append(e)
	return obs


def p_barrel(n):
	M = E.mats_common(n)
	prof = [(0.0, 0.0), (0.19, 0.0), (0.22, 0.10), (0.245, 0.29), (0.22, 0.48), (0.19, 0.58),
		(0.18, 0.58), (0.18, 0.56), (0.0, 0.56)]
	b = G.lathe(n + '_body', prof, seg=32, close_bottom=False)
	b.data.materials.append(M['wood'])
	obs = [b]
	for z, r in ((0.12, 0.226), (0.46, 0.226)):
		h = G.torus(n + '_hoop', r, 0.013, (0, 0, z), (0, 0, 0), seg=32, ring=6)
		h.data.materials.append(M['iron'])
		obs.append(h)
	return obs


def p_fence(n):
	M = E.mats_common(n)
	obs = []
	post = G.box(n + '_post', (0.08, 0.08, 0.62), (0, 0, 0.31))
	G.bevel(post, 0.015, 2)
	obs.append(post)
	for z in (0.42, 0.22):
		r = G.box(n + '_rail', (0.80, 0.05, 0.06), (0, 0, z), (0, random.uniform(-3, 3), 45))
		G.bevel(r, 0.012, 2)
		obs.append(r)
	for o in obs:
		o.data.materials.append(M['wood'])
	return obs


def p_vine(n, grapes):
	M = E.mats_common(n)
	GR = mat(n + '_grape', (0.40, 0.20, 0.44), (0.28, 0.12, 0.32), var_scale=30, var_amt=0.6,
		rough=0.35, coat=0.2, sss=0.2, sss_radius=(0.4, 0.1, 0.4), macro=('noise', 40.0),
		macro_amt=0.3, height_scale=0.002)
	stake = G.cylinder(n + '_stake', 0.02, 0.016, 0.85, (-0.05, 0.09, 0.42), seg=8)
	stake.data.materials.append(M['wood'])
	trunk = G.metaball(n + '_trunk', [mcap((0, 0, 0), (0.03, -0.02, 0.30), 0.04),
		mcap((0.03, -0.02, 0.30), (-0.06, 0.0, 0.50), 0.03),
		mcap((0.03, -0.02, 0.30), (0.12, -0.03, 0.52), 0.028)], resolution=0.012)
	trunk.data.materials.append(M['bark'])
	obs = [stake, trunk]
	for (x, y, z, r) in ((-0.10, 0.02, 0.62, 0.20), (0.12, -0.02, 0.60, 0.20), (0.0, 0.06, 0.74, 0.18),
			(0.02, -0.12, 0.56, 0.15)):
		b = G.ico(n + '_leaf', r, (x, y, z), (1.1, 1.0, 0.8), sub=3)
		lumpy(b, 0.035, 15.0, x * 7)
		b.data.materials.append(M['leaf'])
		obs.append(b)
	if grapes:
		for (cx, cy, cz) in ((0.10, -0.20, 0.50), (-0.12, -0.16, 0.48)):
			for k in range(14):
				lv = k // 4
				a = k * 2.3
				r = 0.035 * (1.0 - lv * 0.2)
				g = G.sphere(n + '_grape', 0.026, (cx + r * math.cos(a), cy + r * math.sin(a),
					cz - lv * 0.035), seg=10, rings=6)
				g.data.materials.append(GR)
				obs.append(g)
	return obs


def p_reed(n):
	ST = mat(n + '_stalk', (0.60, 0.62, 0.38), (0.48, 0.52, 0.30), var_scale=10, var_amt=0.6,
		rough=0.6, macro=('wave', 60.0, 1.0, 'Z'), macro_amt=0.6, height_scale=0.002)
	HD = mat(n + '_head', (0.50, 0.36, 0.24), (0.40, 0.28, 0.18), var_scale=30, var_amt=0.5,
		rough=0.9, macro=('noise', 80.0, 4.0), macro_amt=0.8, cavity=0.3, normal_strength=1.0,
		height_scale=0.004, sheen=0.4)
	obs = []
	for k, (x, y, h, lean) in enumerate(((-0.08, 0.02, 0.86, -6), (0.0, -0.04, 0.98, 2),
			(0.08, 0.03, 0.80, 7), (0.03, 0.08, 0.70, 4), (-0.04, -0.09, 0.62, -3))):
		st = G.cylinder(n + '_stalk', 0.012, 0.008, h, (x, y, h / 2), (lean, lean * 0.5, 0), seg=8)
		st.data.materials.append(ST)
		obs.append(st)
		if k < 3:
			top = Vector((x + math.sin(math.radians(lean * 0.5)) * h * 0.85, y - math.sin(math.radians(lean)) * h * 0.85, h * 0.85))
			hd = G.metaball(n + '_head%d' % k, [mcap(tuple(top - Vector((0, 0, 0.07))),
				tuple(top + Vector((0, 0, 0.07))), 0.028)], resolution=0.008)
			hd.data.materials.append(HD)
			obs.append(hd)
	# 잎 몇 장: 납작하게 휜 띠
	for k in range(4):
		lf = G.sphere(n + '_blade', 1.0, (0, 0, 0), (0.02, 0.006, 0.34), seg=8, rings=12)
		a = k * 1.6
		lf.rotation_euler = (math.radians(18 * math.cos(a)), math.radians(18 * math.sin(a)), a)
		lf.location = (0.05 * math.cos(a), 0.05 * math.sin(a), 0.30)
		G.bake_transform(lf)
		lf.data.materials.append(ST)
		obs.append(lf)
	return obs


def p_herb(n):
	M = E.mats_common(n)
	PET = G.proc_mat(n + '_petal', lin((1.0, 0.86, 0.38)), lin((0.98, 0.70, 0.22)),
		var_scale=40, var_amt=0.6, rough=0.5, sss=0.3, sss_radius=(0.6, 0.4, 0.1),
		emission=(lin((1.0, 0.80, 0.35)), 0.6), macro=('wave', 90.0, 0.5, 'Z'), macro_amt=0.5,
		height_scale=0.002)
	CEN = G.simple_mat(n + '_center', lin((0.80, 0.48, 0.12)), rough=0.7,
		emission=(lin((1.0, 0.60, 0.20)), 0.4))
	stem = G.cylinder(n + '_stem', 0.012, 0.008, 0.30, (0, 0, 0.15), seg=8)
	stem.data.materials.append(M['leaf'])
	obs = [stem]
	for s in (1, -1):
		lf = G.sphere(n + '_leaf', 1.0, (s * 0.08, 0, 0.12), (0.09, 0.035, 0.012),
			(0, -s * 25, 0), seg=12, rings=6)
		lf.data.materials.append(M['leaf'])
		obs.append(lf)
	for k in range(9):
		a = k * 2 * math.pi / 9
		pt = G.sphere(n + '_petal', 1.0, (0.075 * math.cos(a), 0.075 * math.sin(a), 0.34),
			(0.06, 0.028, 0.012), (0, 12, math.degrees(a)), seg=12, rings=6)
		pt.data.materials.append(PET)
		obs.append(pt)
	c = G.sphere(n + '_center', 0.045, (0, 0, 0.35), (1, 1, 0.55), seg=16, rings=8)
	c.data.materials.append(CEN)
	obs.append(c)
	# 꽃을 카메라 쪽으로 살짝 기울이고, 게임에서 눈에 띄게 전체를 1.6배
	for o in obs[3:]:
		G.deform(o, lambda p: Vector((p.x, p.y - (p.z - 0.30) * 0.5, p.z)))
	for o in obs:
		G.deform(o, lambda p: p * 1.6)
	return obs


def p_tent(n, red):
	M = E.mats_common(n)
	col = (0.58, 0.30, 0.24) if red else (0.88, 0.82, 0.68)
	CAN = mat(n + '_canvas', col, tuple(c * 0.88 for c in col), var_scale=3, var_amt=0.6,
		rough=0.85, macro=('noise', 5.0, 2.0), macro_amt=0.6, cavity=0.15, normal_strength=0.8,
		micro=('cells', 220.0, 3.0), bump_strength=0.15, sheen=0.3, height_scale=0.012)
	DARK = G.simple_mat(n + '_inside', lin((0.12, 0.10, 0.09)), rough=0.9)
	L, Wd, H = 0.95, 0.85, 0.80          # 길이(x), 폭(y), 높이
	bm = bmesh.new()
	v = [bm.verts.new(p) for p in ((-L / 2, -Wd / 2, 0), (L / 2, -Wd / 2, 0), (L / 2, 0, H), (-L / 2, 0, H),
		(-L / 2, Wd / 2, 0), (L / 2, Wd / 2, 0))]
	bm.faces.new((v[0], v[1], v[2], v[3]))
	bm.faces.new((v[3], v[2], v[5], v[4]))
	bm.faces.new((v[1], v[5], v[2]))                 # +X 박공 (카메라 쪽)
	bm.faces.new((v[4], v[0], v[3]))
	bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=8, use_grid_fill=True)
	tent = G.obj_from_bm(n + '_tent', bm, smooth=True)
	# 천이 처지게: 능선 사이를 조금 누른다
	G.deform(tent, lambda p: Vector((p.x, p.y, p.z - 0.04 * math.sin(max(0.0, min(1.0, (p.x + L / 2) / L)) * math.pi) * (p.z / H))))
	G.solidify(tent, 0.015)
	tent.data.materials.append(CAN)
	# +X 박공의 입구: 어두운 삼각형
	bm = bmesh.new()
	q = [bm.verts.new(p) for p in ((L / 2 + 0.012, -0.22, 0.0), (L / 2 + 0.012, 0.22, 0.0),
		(L / 2 + 0.012, 0.0, 0.52))]
	bm.faces.new(q)
	door = G.obj_from_bm(n + '_door', bm, smooth=False)
	door.data.materials.append(DARK)
	pole = G.cylinder(n + '_pole', 0.018, 0.018, H + 0.16, (L / 2 + 0.02, 0, (H + 0.16) / 2), seg=8)
	pole.data.materials.append(M['wood'])
	return [tent, door, pole]


def p_wagon(n):
	M = E.mats_common(n)
	COV = mat(n + '_cover', (0.88, 0.82, 0.68), (0.74, 0.62, 0.46), var_scale=3, var_amt=0.7,
		rough=0.85, macro=('noise', 5.0, 2.0), macro_amt=0.6, cavity=0.15,
		micro=('cells', 220.0, 3.0), bump_strength=0.15, sheen=0.3, height_scale=0.012)
	obs = []
	bed = G.box(n + '_bed', (1.15, 0.62, 0.22), (0, 0, 0.42))
	G.bevel(bed, 0.02, 2)
	bed.data.materials.append(M['wood'])
	obs.append(bed)
	for (x, y) in ((0.36, 0.36), (0.36, -0.36), (-0.36, 0.36), (-0.36, -0.36)):
		rim = G.torus(n + '_rim', 0.20, 0.025, (x, y, 0.22), (90, 0, 0), seg=28, ring=8)
		rim.data.materials.append(M['wood'])
		hub = G.cylinder(n + '_hub', 0.04, 0.04, 0.07, (x, y, 0.22), (90, 0, 0), seg=12)
		hub.data.materials.append(M['iron'])
		obs += [rim, hub]
		for k in range(6):
			# 바퀴 면(xz)에 놓인 살: Y 축으로 돌린 각목
			sp = G.box(n + '_spoke', (0.022, 0.022, 0.37), (x, y, 0.22), (0, k * 30, 0))
			sp.data.materials.append(M['wood'])
			obs.append(sp)
	cov = G.cylinder(n + '_cover', 0.36, 0.36, 1.05, (0, 0, 0.53), (0, 90, 0), seg=32, caps=True)
	G.deform(cov, lambda p: Vector((p.x, p.y, 0.53 + max(0.0, p.z - 0.53) * 1.35)))
	cov.data.materials.append(COV)
	obs.append(cov)
	shaft = G.cylinder(n + '_shaft', 0.025, 0.025, 0.6, (0.82, 0, 0.30), (0, 75, 0), seg=8)
	shaft.data.materials.append(M['wood'])
	obs.append(shaft)
	return obs


def p_stall(n, k):
	M = E.mats_common(n)
	AW = mat(n + '_awning', AWNING[k], tuple(c * 0.9 for c in AWNING[k]), var_scale=4,
		var_amt=0.5, rough=0.85, macro=('noise', 6.0, 2.0), macro_amt=0.5,
		micro=('cells', 220.0, 3.0), bump_strength=0.15, sheen=0.3, height_scale=0.008)
	CR = mat(n + '_cream', (0.97, 0.94, 0.88), (0.90, 0.86, 0.78), var_scale=4, var_amt=0.5,
		rough=0.85, macro=('noise', 6.0, 2.0), macro_amt=0.5,
		micro=('cells', 220.0, 3.0), bump_strength=0.15, sheen=0.3, height_scale=0.008)
	obs = []
	for (x, y) in ((0.48, 0.30), (0.48, -0.30), (-0.48, 0.30), (-0.48, -0.30)):
		p = G.box(n + '_post', (0.05, 0.05, 1.25), (x, y, 0.625))
		G.bevel(p, 0.01, 2)
		p.data.materials.append(M['wood'])
		obs.append(p)
	ct = G.box(n + '_counter', (1.05, 0.55, 0.40), (0, -0.02, 0.20))
	G.bevel(ct, 0.02, 2)
	ct.data.materials.append(M['wood'])
	obs.append(ct)
	# 진열품: 바구니 셋에 과일·채소 무더기
	for i in range(3):
		cx = -0.32 + i * 0.32
		bk = G.lathe(n + '_basket', [(0.10, 0.0), (0.13, 0.07), (0.12, 0.07), (0.0, 0.07)], seg=16,
			loc=(cx, -0.08, 0.40))
		bk.data.materials.append(M['wood'])
		obs.append(bk)
		gc = GOODS[(k + i) % 5]
		GM = G.simple_mat(n + '_good%d' % i, lin(gc), rough=0.45, sss=0.1)
		for j in range(6):
			a = j * 2.1
			g = G.sphere(n + '_g', 0.045, (cx + 0.06 * math.cos(a), -0.08 + 0.06 * math.sin(a),
				0.49 + (0.04 if j > 3 else 0.0)), seg=12, rings=8)
			g.data.materials.append(GM)
			obs.append(g)
	# 줄무늬 차양: 기울어진 판을 여섯 띠로
	nstrip = 6
	for i in range(nstrip):
		x0 = -0.56 + i * 1.12 / nstrip
		sl = G.box(n + '_strip', (1.12 / nstrip + 0.002, 0.78, 0.03),
			(x0 + 0.56 / nstrip, -0.05, 1.28), (-14, 0, 0))
		sl.data.materials.append(AW if i % 2 == 0 else CR)
		obs.append(sl)
		# 앞쪽 물결 끝단
		fl = G.cylinder(n + '_flap', 0.055, 0.055, 1.12 / nstrip, (x0 + 0.56 / nstrip, -0.45, 1.16),
			(0, 90, 0), seg=12)
		G.deform(fl, lambda p: Vector((p.x, p.y, min(p.z, 1.16))))
		fl.data.materials.append(AW if i % 2 == 0 else CR)
		obs.append(fl)
	return obs


def p_campfire(n):
	M = E.mats_common(n)
	EMB = G.simple_mat(n + '_ember', lin((1.0, 0.45, 0.12)), rough=0.8,
		emission=(lin((1.0, 0.42, 0.10)), 4.0))
	ASH = mat(n + '_ash', (0.32, 0.30, 0.28), (0.16, 0.15, 0.14), var_scale=10, var_amt=0.8,
		rough=0.95, macro=('noise', 25.0, 5.0), macro_amt=0.8, height_scale=0.008)
	obs = []
	for i in range(7):
		a = i * 2 * math.pi / 7
		st = G.ico(n + '_stone', 0.075, (0.30 * math.cos(a), 0.30 * math.sin(a), 0.045),
			(1.15, 0.85, 0.75), (0, 0, math.degrees(a)), sub=3)
		lumpy(st, 0.01, 30.0, i)
		st.data.materials.append(M['stone'])
		obs.append(st)
	ash = G.sphere(n + '_ash', 1.0, (0, 0, 0), (0.24, 0.24, 0.03), seg=24, rings=8)
	ash.data.materials.append(ASH)
	obs.append(ash)
	for (a, z) in ((25, 0.05), (-35, 0.08), (80, 0.065)):
		lg = G.cylinder(n + '_log', 0.035, 0.032, 0.38, (0, 0, z), (90, 0, a), seg=12)
		lg.data.materials.append(M['bark'])
		obs.append(lg)
	for k in range(6):
		e = G.ico(n + '_ember', 0.02, (random.uniform(-0.1, 0.1), random.uniform(-0.1, 0.1), 0.03),
			sub=2)
		e.data.materials.append(EMB)
		obs.append(e)
	return obs


PROPS = [
	('pine', p_pine), ('bush', p_bush), ('rock', p_rock), ('well', p_well),
	('crate', p_crate), ('barrel', p_barrel), ('fence', p_fence),
	('vine', lambda n: p_vine(n, False)), ('vine_grapes', lambda n: p_vine(n, True)),
	('reed', p_reed), ('herb', p_herb),
	('tent_canvas', lambda n: p_tent(n, False)), ('tent_red', lambda n: p_tent(n, True)),
	('wagon', p_wagon),
] + [('stall_%d' % k, (lambda k: (lambda n: p_stall(n, k)))(k)) for k in range(5)] + [
	('campfire', p_campfire),
]

if ONLY:
	PROPS = [p for p in PROPS if p[0] in ONLY]

out_dir = G.ensure_dir(os.path.join(G.ROOT, 'renders', 'props'))
meta_path = os.path.join(out_dir, 'meta.json')
import json
meta = json.load(open(meta_path)) if (ONLY and os.path.exists(meta_path)) else \
	dict(asset='props', scale=G.SCALE, statics=[])

built = []
for name, fn in PROPS:
	obs = fn(name)
	ob = G.join(obs, name)
	built.append((name, ob))

if STAGE == 'model':
	# 한 줄로 늘어놓고 한 장에
	for i, (name, ob) in enumerate(built):
		c, r = i % 7, i // 7
		ob.location = (c * 1.1 + r * 2.6, c * 1.1 - r * 2.6, 0)
	G.iso_camera(980, 600, 60, 200, scale=1.5)
	G.render_sprite(os.path.join(G.ROOT, 'preview'), 'props_model')
	sys.exit(0)

for name, ob in built:
	ob.location = (0, 0, 0)
	G.smart_uv(ob, margin=0.004)
	G.bake_asset_textures(ob, name, res=512)

for name, ob in built:
	for _, o in built:
		o.hide_render = (o is not ob)
	canvas, pivot = G.auto_canvas([ob])
	G.iso_camera(canvas[0], canvas[1], pivot[0], pivot[1])
	bpy.context.scene.frame_set(1)
	G.render_sprite(out_dir, name)
	meta['statics'] = [s for s in meta['statics'] if s['name'] != name]
	meta['statics'].append(dict(name=name, canvas=list(canvas), pivot=list(pivot)))
	print('[props]', name, canvas, pivot)
for _, o in built:
	o.hide_render = False
G.write_sprite_meta(out_dir, meta)

# 블렌드 파일에는 격자로 펼쳐 둔다
for i, (name, ob) in enumerate(built):
	ob.location = ((i % 5) * 1.6, -(i // 5) * 1.6, 0)
if not ONLY:
	G.save_blend('props')
print('[props] done', len(built))
