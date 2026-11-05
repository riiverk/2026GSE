# -----------------------------------------------------------------------------
# env.py — 고향 마을 환경 키트 + 요리
#
#   python3 env.py house | tree | pot | dish   [GG_STAGE=model 이면 검수 렌더만]
#
# house  프로방스 시골집. 회벽, 석재 기단, 테라코타 기와(실제 지오메트리),
#        라벤더빛 덧창, 등불이 비치는 창(블룸 임계값 위), 라벤더 화분, 굴뚝.
# tree   올리브 계열 활엽수. 뒤틀린 줄기 + 잎 뭉치. 바람 흔들림 리그 포함.
# pot    화덕(돌 링 + 장작)과 무쇠 솥(토마토 스튜, 국자)을 두 장으로 나눠 굽는다.
#        엔진이 그 사이에 불꽃 셰이더를 그린다.
# dish   토마토마와 호수사슴 스튜. 테라코타 그릇, 빵, 로즈마리.
#        쿼터뷰 아이콘 + 음식 사진식 클로즈업 (조리 완료 화면용).
# -----------------------------------------------------------------------------
import sys, os, math, random
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy
from mathutils import Vector, Euler
import ggcommon as G
from ggcommon import mball, mell, mcap, lin

WHAT = sys.argv[1] if len(sys.argv) > 1 else 'house'
STAGE = os.environ.get('GG_STAGE', 'all')
random.seed(7)


def mats_common():
	M = {}
	M['plaster'] = G.proc_mat(WHAT + '_plaster', lin((0.93, 0.87, 0.75)), lin((0.86, 0.76, 0.62)),
		var_scale=2.2, var_amt=0.7, rough=0.9, rough_var=0.05,
		macro=('noise', 7.0, 4.0, 0.6), macro_amt=0.8, cavity=0.18, normal_strength=0.8,
		micro=('noise', 120.0, 6.0), bump_strength=0.25, height_scale=0.012)
	M['stone'] = G.proc_mat(WHAT + '_stone', lin((0.72, 0.68, 0.60)), lin((0.58, 0.54, 0.47)),
		var_scale=6, var_amt=0.7, rough=0.85,
		macro=('cells', 9.0, 7.0), macro_amt=1.0, cavity=0.35, normal_strength=1.0,
		micro=('noise', 90.0, 5.0), bump_strength=0.3, height_scale=0.02)
	M['terracotta'] = G.proc_mat(WHAT + '_terracotta', lin((0.80, 0.40, 0.25)),
		lin((0.66, 0.30, 0.18)), var_scale=5, var_amt=0.8, rough=0.7, rough_var=0.12,
		macro=('noise', 14.0, 3.0), macro_amt=0.5, cavity=0.2, normal_strength=0.5,
		micro=('noise', 160.0, 4.0), bump_strength=0.2, height_scale=0.008)
	M['wood'] = G.proc_mat(WHAT + '_wood', lin((0.55, 0.39, 0.25)), lin((0.42, 0.29, 0.18)),
		var_scale=7, var_amt=0.6, rough=0.65, macro=('grain', 22.0, 'Z'), macro_amt=0.8,
		cavity=0.25, normal_strength=0.8, micro=('noise', 150.0), bump_strength=0.15,
		height_scale=0.008)
	M['iron'] = G.proc_mat(WHAT + '_iron', lin((0.17, 0.16, 0.17)), lin((0.24, 0.20, 0.18)),
		var_scale=8, var_amt=0.5, rough=0.55, rough_var=0.15, metallic=0.85,
		macro=('noise', 30.0, 4.0), macro_amt=0.6, cavity=0.2, normal_strength=0.6,
		micro=('noise', 220.0, 3.0), bump_strength=0.15, height_scale=0.006)
	M['leaf'] = G.proc_mat(WHAT + '_leaf', lin((0.42, 0.54, 0.28)), lin((0.30, 0.40, 0.24)),
		var_scale=3, var_amt=0.8, rough=0.6, rough_var=0.1,
		macro=('pebbles', 16.0), macro_amt=1.0, cavity=0.45, normal_strength=1.4,
		micro=('noise', 120.0, 3.0), bump_strength=0.2, sheen=0.4, sss=0.12,
		sss_radius=(0.3, 0.6, 0.2), height_scale=0.03)
	M['bark'] = G.proc_mat(WHAT + '_bark', lin((0.42, 0.33, 0.25)), lin((0.30, 0.24, 0.19)),
		var_scale=6, var_amt=0.7, rough=0.9, macro=('wave', 10.0, 8.0, 'Z'), macro_amt=0.9,
		cavity=0.35, normal_strength=1.0, micro=('noise', 120.0, 5.0), bump_strength=0.25,
		height_scale=0.02)
	return M


def render_set(asset, items, scale=2):
	"""items: [(name, canvas, pivot, visible_objects or None)]"""
	out_dir = G.ensure_dir(os.path.join(G.ROOT, 'renders', asset))
	meta = dict(asset=asset, scale=scale, statics=[])
	allobs = [o for o in bpy.data.objects if o.type in ('MESH', 'CURVE')]
	for (nm, canvas, pivot, vis) in items:
		for o in allobs:
			o.hide_render = (vis is not None and o not in vis)
		G.iso_camera(canvas[0], canvas[1], pivot[0], pivot[1], scale=scale)
		bpy.context.scene.frame_set(1)
		G.render_sprite(out_dir, nm)
		meta['statics'].append(dict(name=nm, canvas=list(canvas), pivot=list(pivot)))
	for o in allobs:
		o.hide_render = False
	G.write_sprite_meta(out_dir, meta)


def model_preview(name, canvas=(512, 512), pivot=(256, 400), scale=4):
	G.iso_camera(canvas[0], canvas[1], pivot[0], pivot[1], scale=scale)
	G.render_sprite(os.path.join(G.ROOT, 'preview', 'env_model'), name)


# =============================================================================
# 집
# =============================================================================
def build_house():
	M = mats_common()
	SHUT = G.proc_mat('house_shutter', lin((0.55, 0.60, 0.74)), lin((0.48, 0.52, 0.66)),
		var_scale=6, var_amt=0.5, rough=0.6, macro=('wave', 40.0, 0.5, 'X'), macro_amt=0.8,
		cavity=0.3, normal_strength=0.8, micro=('noise', 160.0), bump_strength=0.15,
		height_scale=0.006)
	LAV = G.proc_mat('house_lavender', lin((0.55, 0.45, 0.78)), lin((0.42, 0.33, 0.66)),
		var_scale=20, var_amt=0.7, rough=0.7, macro=('pebbles', 90.0), macro_amt=1.0,
		cavity=0.3, normal_strength=0.8, height_scale=0.006, sheen=0.3)
	GLASS = G.simple_mat('house_glass', lin((1.0, 0.80, 0.46)), rough=0.25,
		emission=(lin((1.0, 0.76, 0.42)), 2.6))
	DARK = G.simple_mat('house_recess', lin((0.20, 0.17, 0.15)), rough=0.8)

	W, D, HW = 1.20, 1.00, 0.86      # 벽 x, y, 높이
	parts = []

	def add(ob, mat):
		ob.data.materials.append(mat)
		parts.append(ob)
		return ob

	# 벽: 모서리를 둥글린 상자 + 손으로 빚은 듯 살짝 기울인다
	wall = G.box('h_wall', (W, D, HW), (0, 0, HW / 2 + 0.10))
	G.bevel(wall, 0.035, 3)
	G.subsurf(wall, 1)
	G.deform(wall, lambda p: Vector((p.x + 0.012 * p.z, p.y - 0.008 * p.z * p.x,
		p.z + 0.01 * math.sin(p.x * 4.0))))
	add(wall, M['plaster'])

	# 기단 석재
	base = G.box('h_base', (W + 0.08, D + 0.08, 0.14), (0, 0, 0.07))
	G.bevel(base, 0.03, 3)
	add(base, M['stone'])

	# 박공벽 (±X 쪽 삼각형)
	ridge_h = 0.50
	for sx in (1, -1):
		bm = __import__('bmesh').new()
		v = [bm.verts.new((sx * W / 2 * 0.985, -D / 2, HW + 0.10)),
			bm.verts.new((sx * W / 2 * 0.985, D / 2, HW + 0.10)),
			bm.verts.new((sx * W / 2 * 0.985, 0.0, HW + 0.10 + ridge_h))]
		bm.faces.new(v)
		ob = G.obj_from_bm('h_gable', bm, smooth=False)
		G.solidify(ob, 0.06)
		G.bevel(ob, 0.015, 2)
		add(ob, M['plaster'])

	# 지붕 판 + 기와 (반원통 줄)
	eave = 0.09
	slope_len = math.hypot(D / 2 + eave, ridge_h + eave * ridge_h / (D / 2))
	ang = math.atan2(ridge_h, D / 2)
	for sy in (1, -1):
		cy = sy * (D / 2 + eave) / 2
		slab = G.box('h_roofslab', (W + 0.22, slope_len, 0.05),
			(0, cy, HW + 0.10 + ridge_h / 2 + 0.02), (math.degrees(ang) * -sy, 0, 0))
		add(slab, M['terracotta'])
		n_tiles = 13
		for i in range(n_tiles):
			x = -((W + 0.18) / 2) + (i + 0.5) * (W + 0.18) / n_tiles
			t = G.cylinder('h_tile', 0.052, 0.046, slope_len, (0, 0, 0), (90, 0, 0), seg=12,
				caps=True)
			# 반원통: 아래쪽 절반을 눌러 판에 붙인다
			G.deform(t, lambda p: Vector((p.x, p.y, max(p.z, 0.0) * 0.9)))
			t.rotation_euler = (math.radians(-sy * math.degrees(ang)) + (0 if sy > 0 else 0), 0, 0)
			t.location = (x + random.uniform(-0.006, 0.006), cy,
				HW + 0.10 + ridge_h / 2 + 0.05)
			G.bake_transform(t)
			add(t, M['terracotta'])
	# 용마루
	ridge = G.cylinder('h_ridge', 0.065, 0.065, W + 0.26, (0, 0, HW + 0.10 + ridge_h + 0.03),
		(0, 90, 0), seg=16)
	add(ridge, M['terracotta'])

	# 굴뚝
	ch = G.box('h_chimney', (0.20, 0.20, 0.55), (-0.32, 0.20, HW + 0.10 + 0.50))
	G.bevel(ch, 0.025, 3)
	add(ch, M['stone'])
	cap = G.box('h_chimcap', (0.27, 0.27, 0.05), (-0.32, 0.20, HW + 0.10 + 0.80))
	G.bevel(cap, 0.02, 2)
	add(cap, M['terracotta'])

	# 문 (-Y 면, 왼쪽 앞) — 아치형 나무 문 + 돌 계단
	dx = 0.22
	door = G.box('h_door', (0.30, 0.06, 0.50), (dx, -D / 2 - 0.005, 0.10 + 0.25))
	G.bevel(door, 0.02, 3)
	add(door, M['wood'])
	# 아치: 반원 프리즘 (위쪽 반만)
	import bmesh
	bm = bmesh.new()
	ring_f, ring_b = [], []
	for i in range(17):
		a = math.pi * i / 16
		x = dx + 0.15 * math.cos(a); z = 0.60 + 0.15 * math.sin(a)
		ring_f.append(bm.verts.new((x, -D / 2 - 0.035, z)))
		ring_b.append(bm.verts.new((x, -D / 2 + 0.025, z)))
	bm.faces.new(ring_f)
	bm.faces.new(list(reversed(ring_b)))
	for i in range(16):
		bm.faces.new((ring_f[i], ring_b[i], ring_b[i + 1], ring_f[i + 1]))
	bm.faces.new((ring_f[16], ring_b[16], ring_b[0], ring_f[0]))
	bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
	arch = G.obj_from_bm('h_doorarch', bm, smooth=False)
	G.bevel(arch, 0.012, 2, angle=60)
	add(arch, M['wood'])
	# 돌 아치 테두리: 반원을 따라 작은 석재 블록
	for i in range(9):
		a = math.pi * i / 8
		blk = G.box('h_archstone', (0.06, 0.05, 0.05),
			(dx + 0.185 * math.cos(a), -D / 2 - 0.02, 0.60 + 0.185 * math.sin(a)),
			(0, -math.degrees(a) + 90, 0))
		G.bevel(blk, 0.012, 2)
		add(blk, M['stone'])
	knob = G.sphere('h_knob', 0.018, (dx + 0.10, -D / 2 - 0.045, 0.40))
	add(knob, M['iron'])
	step = G.box('h_step', (0.42, 0.18, 0.06), (dx, -D / 2 - 0.10, 0.03))
	G.bevel(step, 0.02, 2)
	add(step, M['stone'])

	# 창: 오목한 틀 + 등불 유리 + 덧창 + 창턱
	def window(cx, cy, cz, face):
		# face: '-Y' 또는 '+X'
		if face == '-Y':
			rot = (0, 0, 0); n = Vector((0, -1, 0)); side = Vector((1, 0, 0))
		else:
			rot = (0, 0, 90); n = Vector((1, 0, 0)); side = Vector((0, 1, 0))
		c = Vector((cx, cy, cz))
		rec = G.box('h_winrec', (0.22, 0.05, 0.26), c + n * 0.005, rot)
		add(rec, DARK)
		gl = G.box('h_glass', (0.16, 0.02, 0.20), c + n * 0.02, rot)
		add(gl, GLASS)
		mul = G.box('h_mullion', (0.02, 0.03, 0.22), c + n * 0.035, rot)
		add(mul, M['wood'])
		mul2 = G.box('h_mullion2', (0.18, 0.03, 0.02), c + n * 0.035, rot)
		add(mul2, M['wood'])
		for s in (1, -1):
			sh = G.box('h_shutter', (0.12, 0.025, 0.28), c + side * s * 0.19 + n * 0.035, rot)
			G.bevel(sh, 0.008, 2)
			add(sh, SHUT)
		sill = G.box('h_sill', (0.30, 0.09, 0.035), c + n * 0.045 + Vector((0, 0, -0.15)), rot)
		G.bevel(sill, 0.01, 2)
		add(sill, M['stone'])
		return c, n, side

	window(-0.22, -D / 2, 0.66, '-Y')
	c, n, side = window(W / 2, -0.12, 0.66, '+X')
	window(W / 2, 0.30, 0.40, '+X')
	# 라벤더 화분 (+X 위 창 아래)
	box_ = G.box('h_flowerbox', (0.08, 0.30, 0.08), c + n * 0.07 + Vector((0, 0, -0.21)))
	G.bevel(box_, 0.01, 2)
	add(box_, M['terracotta'])
	lav_el = []
	for k in range(9):
		y = -0.13 + k * 0.032
		top = Vector((c.x + 0.07 + random.uniform(-0.02, 0.02), c.y + y,
			c.z - 0.10 + random.uniform(0.0, 0.06)))
		bot = Vector((c.x + 0.07, c.y + y, c.z - 0.18))
		lav_el.append(mcap(tuple(bot), tuple(top), 0.016))
	lav = G.metaball('h_lavender', lav_el, resolution=0.008)
	add(lav, LAV)
	# 벽 아래 잡초 덤불 몇 개
	for (x, y) in ((W / 2 + 0.02, -D / 2 + 0.05), (-W / 2 + 0.05, -D / 2 - 0.02)):
		bush = G.ico('h_weed', 0.09, (x, y, 0.05), (1, 1, 0.7), sub=3)
		add(bush, M['leaf'])

	house = G.join(parts, 'House')
	return house, M


# =============================================================================
# 나무
# =============================================================================
def build_tree():
	M = mats_common()
	trunk = G.metaball('t_trunk', [
		mcap((0, 0, 0.0), (0.04, -0.02, 0.30), 0.105),
		mcap((0.04, -0.02, 0.30), (-0.04, 0.02, 0.62), 0.085),
		mcap((-0.04, 0.02, 0.62), (-0.30, 0.10, 0.98), 0.055),
		mcap((-0.02, 0.0, 0.55), (0.32, -0.14, 0.92), 0.055),
		mcap((-0.02, 0.02, 0.70), (0.06, 0.26, 1.12), 0.045),
		mcap((0.0, 0.0, 0.70), (0.02, -0.06, 1.25), 0.05),
		mell((0, 0, 0.04), (0.19, 0.17, 0.06)),       # 뿌리 퍼짐
		mcap((0, 0, 0.03), (0.20, -0.05, 0.0), 0.04),
		mcap((0, 0, 0.03), (-0.17, -0.10, 0.0), 0.04),
	], resolution=0.014)
	trunk.data.materials.append(M['bark'])
	clumps = []
	spots = [(-0.34, 0.10, 1.10, 0.36), (0.36, -0.14, 1.04, 0.36), (0.06, 0.30, 1.22, 0.38),
		(0.02, -0.10, 1.38, 0.42), (-0.16, -0.28, 1.02, 0.30), (0.22, 0.16, 1.48, 0.32),
		(-0.20, 0.06, 1.55, 0.30), (0.16, -0.32, 1.20, 0.28)]
	for (x, y, z, r) in spots:
		c = G.ico('t_leaves', r, (x, y, z), (1, 1, 0.80), sub=4)
		rs = random.random() * 10
		# 둥근 덩어리 표면에 작은 잎 뭉치 혹을 낸다 (실루엣이 브로콜리처럼 매끈하지 않게)
		G.deform(c, lambda p, rs=rs, cz=z, cx=x, cy=y: p + (p - Vector((cx, cy, cz))).normalized() *
			(0.045 * math.sin(p.x * 17 + rs) * math.sin(p.y * 15 + rs) * math.sin(p.z * 16 + rs)
			+ 0.02 * math.sin(p.x * 41) * math.sin(p.y * 37)))
		c.data.materials.append(M['leaf'])
		clumps.append(c)
	tree = G.join([trunk] + clumps, 'Tree')

	# 바람 리그: 줄기 2 + 가지 3. 잎 뭉치는 가장 가까운 가지를 따른다
	arm = G.make_armature('Tree_Rig', [
		('root', (0, 0, 0), (0, 0.2, 0), None, False, 0, False),
		('trunk', (0, 0, 0.0), (0.0, 0.0, 0.62), 'root', True, 0, False),
		('top', (0.0, 0.0, 0.62), (0.02, -0.04, 1.55), 'trunk', True, 0, True),
		('branch.L', (-0.04, 0.02, 0.62), (-0.32, 0.10, 1.10), 'trunk', True, 0, False),
		('branch.R', (0.0, 0.0, 0.58), (0.36, -0.14, 1.04), 'trunk', True, 0, False),
	])
	G.skin(tree, arm, ['trunk', 'top', 'branch.L', 'branch.R'], power=4)
	G.bind(tree, arm)
	TAU = 2 * math.pi
	G.anim_fn(arm, 'tree_wind', 1, 49, 2, {
		'trunk': lambda t: dict(rot=(1.2 * math.sin(t * TAU), 0, 0.8 * math.sin(t * TAU * 2))),
		'top': lambda t: dict(rot=(2.0 * math.sin(t * TAU - 0.6), 0, 1.5 * math.sin(t * TAU * 2 - 0.4))),
		'branch.L': lambda t: dict(rot=(3.0 * math.sin(t * TAU * 2 + 1), 0, 0)),
		'branch.R': lambda t: dict(rot=(3.0 * math.sin(t * TAU * 2 + 2), 0, 0)),
	})
	arm.animation_data.action = None
	G.reset_pose(arm)
	return tree, M


# =============================================================================
# 화덕 + 솥
# =============================================================================
def stew_mat(name, ns=1.0, scale=30.0, cav=0.3):
	return G.proc_mat(name, lin((0.80, 0.26, 0.10)), lin((0.92, 0.45, 0.16)),
		var_scale=9, var_amt=0.8, rough=0.18, rough_var=0.08,
		macro=('pebbles', scale), macro_amt=1.0, cavity=cav, normal_strength=ns,
		micro=('noise', 150.0, 3.0), bump_strength=0.1, sss=0.35, sss_radius=(0.6, 0.2, 0.1),
		coat=0.6, coat_rough=0.05, height_scale=0.012)


def build_pot():
	M = mats_common()
	STEW = stew_mat('pot_stew')
	ASH = G.proc_mat('pot_ash', lin((0.35, 0.33, 0.31)), lin((0.18, 0.16, 0.15)),
		var_scale=10, var_amt=0.8, rough=0.95, macro=('noise', 25.0, 5.0), macro_amt=0.8,
		cavity=0.3, normal_strength=0.6, height_scale=0.01)
	EMBER = G.simple_mat('pot_ember', lin((1.0, 0.45, 0.12)), rough=0.8,
		emission=(lin((1.0, 0.42, 0.10)), 4.0))

	base_parts = []
	# 돌 링
	for i in range(9):
		a = i * 2 * math.pi / 9 + random.uniform(-0.1, 0.1)
		r = 0.40
		st = G.ico('p_stone', 0.10, (r * math.cos(a), r * math.sin(a), 0.06),
			(1.0 + random.uniform(-0.1, 0.2), 0.85, 0.75), (0, 0, math.degrees(a)), sub=3)
		G.deform(st, lambda p: p + Vector((0, 0, 0.01 * math.sin(p.x * 30))))
		st.data.materials.append(M['stone'])
		base_parts.append(st)
	ash = G.sphere('p_ash', 1.0, (0, 0, 0.0), (0.33, 0.33, 0.04), seg=32, rings=12)
	ash.data.materials.append(ASH)
	base_parts.append(ash)
	for (a, z) in ((20, 0.07), (-40, 0.11), (75, 0.09)):
		lg = G.cylinder('p_log', 0.045, 0.040, 0.48, (0, 0, z), (90, 0, a), seg=14)
		G.bevel(lg, 0.01, 2)
		lg.data.materials.append(M['bark'])
		base_parts.append(lg)
	for k in range(7):
		e = G.ico('p_ember', 0.025, (random.uniform(-0.15, 0.15), random.uniform(-0.15, 0.15), 0.04),
			sub=2)
		e.data.materials.append(EMBER)
		base_parts.append(e)
	hearth = G.join(base_parts, 'Hearth')

	pot_parts = []
	lift = 0.20                       # 솥을 돌 위에 올린다 — 그 사이로 불꽃이 보인다
	prof = [(0.18, 0.0), (0.27, 0.04), (0.33, 0.14), (0.345, 0.25), (0.33, 0.34), (0.30, 0.38),
		(0.315, 0.40), (0.325, 0.42), (0.29, 0.425), (0.275, 0.39), (0.30, 0.30), (0.30, 0.10)]
	pot = G.lathe('p_pot', prof, seg=48, loc=(0, 0, lift))
	G.subsurf(pot, 1)
	pot.data.materials.append(M['iron'])
	pot_parts.append(pot)
	for s in (1, -1):
		h = G.torus('p_handle', 0.06, 0.014, (s * 0.35, 0, lift + 0.33), (90, 0, 90), seg=20, ring=6)
		h.data.materials.append(M['iron'])
		pot_parts.append(h)
	for i in range(3):
		a = i * 2 * math.pi / 3 + 0.5
		lg = G.cylinder('p_leg', 0.03, 0.022, 0.20, (0.17 * math.cos(a), 0.17 * math.sin(a),
			lift - 0.07), (0, 0, 0), seg=10)
		lg.data.materials.append(M['iron'])
		pot_parts.append(lg)
	stew = G.sphere('p_stew', 1.0, (0, 0, lift + 0.36), (0.285, 0.285, 0.03), seg=40, rings=12)
	stew.data.materials.append(STEW)
	pot_parts.append(stew)
	# 건더기: 토마토 조각, 사슴고기, 허브
	for k in range(9):
		a = random.uniform(0, 2 * math.pi); r = random.uniform(0.0, 0.2)
		ch = G.ico('p_chunk', 0.035, (r * math.cos(a), r * math.sin(a), lift + 0.385),
			(1.2, 1.0, 0.6), sub=2)
		ch.data.materials.append(M['terracotta'] if k % 3 else M['wood'])
		pot_parts.append(ch)
	ladle = G.cylinder('p_ladle', 0.016, 0.012, 0.55, (0.12, 0.10, lift + 0.55), (28, -20, 0), seg=10)
	ladle.data.materials.append(M['wood'])
	pot_parts.append(ladle)
	cauldron = G.join(pot_parts, 'Cauldron')
	return hearth, cauldron, M


# =============================================================================
# 요리
# =============================================================================
def build_dish():
	M = mats_common()
	STEW = stew_mat('dish_stew', ns=0.35, scale=14.0, cav=0.12)
	GLAZE = G.proc_mat('dish_glaze', lin((0.86, 0.47, 0.28)), lin((0.94, 0.86, 0.70)),
		var_scale=4, var_amt=0.0, rough=0.2, rough_var=0.05,
		macro=('noise', 12.0, 3.0), macro_amt=0.3, cavity=0.05, normal_strength=0.3,
		micro=('noise', 200.0), bump_strength=0.04, coat=0.8, coat_rough=0.04,
		height_scale=0.004)
	CREAM = G.proc_mat('dish_cream', lin((0.98, 0.94, 0.84)), lin((0.94, 0.88, 0.76)),
		var_scale=10, var_amt=0.4, rough=0.25, sss=0.3, sss_radius=(0.4, 0.3, 0.2), coat=0.5,
		macro=('noise', 20.0, 2.0), macro_amt=0.5, normal_strength=0.4, height_scale=0.004)
	MEAT = G.proc_mat('dish_meat', lin((0.62, 0.36, 0.22)), lin((0.46, 0.24, 0.14)),
		var_scale=20, var_amt=0.7, rough=0.35, coat=0.4, macro=('grain', 60.0, 'X'),
		macro_amt=0.8, cavity=0.3, normal_strength=0.8, height_scale=0.005)
	TOM = G.proc_mat('dish_tomato', lin((0.88, 0.16, 0.08)), lin((0.96, 0.40, 0.14)),
		var_scale=15, var_amt=0.6, rough=0.12, sss=0.4, sss_radius=(0.8, 0.2, 0.1), coat=0.7,
		macro=('noise', 30.0), macro_amt=0.4, normal_strength=0.3, height_scale=0.003)
	BREAD = G.proc_mat('dish_bread', lin((0.78, 0.52, 0.27)), lin((0.55, 0.32, 0.15)),
		var_scale=6, var_amt=0.8, rough=0.75, macro=('pebbles', 18.0), macro_amt=0.9,
		cavity=0.35, normal_strength=0.8, micro=('noise', 160.0, 4.0), bump_strength=0.2,
		height_scale=0.008)
	CRUMB = G.proc_mat('dish_crumb', lin((0.96, 0.88, 0.70)), lin((0.88, 0.78, 0.58)),
		var_scale=20, var_amt=0.5, rough=0.85, macro=('cells', 45.0, 4.0), macro_amt=1.0,
		cavity=0.35, normal_strength=1.0, height_scale=0.006)
	HERB = G.proc_mat('dish_herb', lin((0.25, 0.45, 0.16)), lin((0.18, 0.34, 0.12)),
		var_scale=20, var_amt=0.6, rough=0.4, sss=0.2, sss_radius=(0.2, 0.5, 0.1), coat=0.3)

	parts = []
	prof = [(0.0, 0.0), (0.14, 0.0), (0.16, 0.01), (0.22, 0.05), (0.27, 0.11), (0.295, 0.15),
		(0.30, 0.165), (0.285, 0.17), (0.27, 0.14), (0.22, 0.08), (0.0, 0.07)]
	bowl = G.lathe('d_bowl', prof, seg=56, close_bottom=False)
	G.subsurf(bowl, 1)
	bowl.data.materials.append(GLAZE)
	parts.append(bowl)
	stew = G.sphere('d_stew', 1.0, (0, 0, 0.135), (0.272, 0.272, 0.025), seg=48, rings=12)
	G.deform(stew, lambda p: p + Vector((0, 0, 0.006 * math.sin(p.x * 40) * math.sin(p.y * 37))))
	stew.data.materials.append(STEW)
	parts.append(stew)
	for k in range(8):
		a = random.uniform(0, 2 * math.pi); r = random.uniform(0.04, 0.19)
		ch = G.box('d_meat', (0.06, 0.05, 0.04), (r * math.cos(a), r * math.sin(a), 0.155),
			(random.uniform(-15, 15), random.uniform(-15, 15), random.uniform(0, 90)))
		G.bevel(ch, 0.012, 3)
		ch.data.materials.append(MEAT)
		parts.append(ch)
	for k in range(7):
		a = random.uniform(0, 2 * math.pi); r = random.uniform(0.03, 0.2)
		tm = G.sphere('d_tom', 0.032, (r * math.cos(a), r * math.sin(a), 0.155), (1.2, 1.0, 0.55),
			seg=16, rings=8)
		tm.data.materials.append(TOM)
		parts.append(tm)
	swirl = G.torus('d_cream', 0.06, 0.012, (-0.05, 0.03, 0.158), (0, 0, 0), (1.3, 1.0, 0.4),
		seg=24, ring=6)
	swirl.data.materials.append(CREAM)
	parts.append(swirl)
	for k in range(12):
		a = random.uniform(0, 2 * math.pi); r = random.uniform(0.0, 0.22)
		hb = G.sphere('d_herb', 0.012, (r * math.cos(a), r * math.sin(a), 0.162), (1.4, 0.7, 0.3),
			(0, 0, random.uniform(0, 180)), seg=8, rings=4)
		hb.data.materials.append(HERB)
		parts.append(hb)
	# 로즈마리 가지
	rs = G.cylinder('d_rosemary', 0.006, 0.004, 0.22, (0.08, -0.06, 0.17), (90, 0, 35), seg=6)
	rs.data.materials.append(M['wood'])
	parts.append(rs)
	for k in range(10):
		t = -0.1 + k * 0.02
		for s in (1, -1):
			nd = G.sphere('d_needle', 0.014, (0.08 + t * math.cos(math.radians(35)),
				-0.06 + t * math.sin(math.radians(35)), 0.172), (0.25, 1.0, 0.18),
				(0, s * 40, 35 + s * 30), seg=6, rings=4)
			nd.data.materials.append(HERB)
			parts.append(nd)
	# 빵 한 조각: 그릇에 기대 놓는다
	crust = G.box('d_bread', (0.20, 0.09, 0.13), (0.24, 0.18, 0.11), (0, -30, 30))
	G.bevel(crust, 0.035, 4)
	crust.data.materials.append(BREAD)
	parts.append(crust)
	crumb = G.box('d_crumb', (0.17, 0.095, 0.10), (0.24, 0.18, 0.11), (0, -30, 30))
	G.bevel(crumb, 0.02, 3)
	crumb.data.materials.append(CRUMB)
	parts.append(crumb)
	return G.join(parts, 'Dish'), M


# =============================================================================
# 실행
# =============================================================================
G.reset(samples=64)
G.light_rig()

if WHAT == 'house':
	house, M = build_house()
	if STAGE == 'model':
		model_preview('house', (600, 600), (300, 470), 3)
		sys.exit(0)
	G.smart_uv(house, margin=0.003)
	G.bake_asset_textures(house, 'house', res=2048)
	render_set('house', [('house', (300, 330), (150, 250), None)])
	G.save_blend('house')

elif WHAT == 'tree':
	tree, M = build_tree()
	if STAGE == 'model':
		model_preview('tree', (500, 600), (250, 520), 3)
		sys.exit(0)
	G.smart_uv(tree, margin=0.004)
	G.bake_asset_textures(tree, 'tree', res=1024)
	render_set('tree', [('tree', (300, 380), (150, 320), None)])
	G.save_blend('tree')

elif WHAT == 'pot':
	hearth, cauldron, M = build_pot()
	if STAGE == 'model':
		model_preview('pot', (500, 500), (250, 360), 5)
		sys.exit(0)
	for ob, nm in ((hearth, 'hearth'), (cauldron, 'cauldron')):
		G.smart_uv(ob, margin=0.004)
		G.bake_asset_textures(ob, nm, res=1024)
	render_set('pot', [('pot_hearth', (150, 130), (75, 90), [hearth]),
		('pot_cauldron', (150, 180), (75, 140), [cauldron])])
	G.save_blend('pot')

elif WHAT == 'dish':
	dish, M = build_dish()
	if STAGE == 'model':
		model_preview('dish', (500, 500), (250, 320), 9)
		sys.exit(0)
	G.smart_uv(dish, margin=0.004)
	G.bake_asset_textures(dish, 'dish', res=1024)
	render_set('dish', [('dish_icon', (100, 90), (50, 60), None)])
	# ---- 클로즈업: 음식 사진처럼. 45도 원근, 얕은 심도, 뒤에서 오는 림 ----
	s = bpy.context.scene
	s.cycles.samples = 256
	cd = bpy.data.cameras.new('Close')
	cam = bpy.data.objects.new('CloseCam', cd)
	G.link(cam)
	cd.lens = 60
	cd.dof.use_dof = True
	cd.dof.aperture_fstop = 3.5
	cd.dof.focus_distance = 1.75
	cam.location = (0.04, -1.30, 1.18)
	cam.rotation_euler = (math.radians(47), 0, 0)
	s.camera = cam
	s.render.resolution_x = 512
	s.render.resolution_y = 512
	G.render_sprite(os.path.join(G.ROOT, 'renders', 'dish'), 'dish_closeup')
	import json
	mp = os.path.join(G.ROOT, 'renders', 'dish', 'meta.json')
	meta = json.load(open(mp))
	meta['statics'].append(dict(name='dish_closeup', canvas=[512, 512], pivot=[256, 256], ui=True))
	json.dump(meta, open(mp, 'w'), indent=1)
	G.save_blend('dish')

print('[env] done', WHAT)
