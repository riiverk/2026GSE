# -----------------------------------------------------------------------------
# landmarks.py — 마을 광장의 특별한 건물 다섯 채
#
#   python3 landmarks.py [tavern inn guild mill manor]   (없으면 전부)
#
# tavern  선술집: 시골집 + 잔 그림 간판 + 문 옆 술통 둘
# inn     여관:   시골집 + 초승달 간판 + 문 옆 등불 + 화분
# guild   협회 출장소: 시골집 + 문 위 문장판 + 깃대. 깃발은 따로 8장 (바람)
# mill    방앗간: 돌 기단의 둥근 탑 + 원뿔 기와 지붕. 날개는 따로 16장 (90도 회전)
# manor   영주 저택: 시골집 + 검은 슬레이트 지붕 + 보랏빛 휘장 둘
#
# 엔진은 집 자리에 이 그림을 그리고, 깃발·날개는 같은 피벗으로 그 위에 겹친다.
# 겹치는 그림을 구울 때 건물은 홀드아웃이라 가려지는 부분이 정확하다.
# -----------------------------------------------------------------------------
import sys, os, math, json, random
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy
import bmesh
from mathutils import Vector, Matrix
import ggcommon as G
from ggcommon import mball, mell, mcap, lin

ALL = ['tavern', 'inn', 'guild', 'mill', 'manor']
WHICH = [a for a in sys.argv[1:] if a in ALL] or ALL
STAGE = os.environ.get('GG_STAGE', 'all')
OUT = os.path.join(G.ROOT, 'renders', 'landmarks')

# 집과 같은 캔버스·피벗이라야 엔진이 집 자리에 그대로 바꿔 그릴 수 있다
HOUSE_CANVAS = (300, 330)
HOUSE_PIVOT = (150, 250)
MILL_CANVAS = (300, 380)
MILL_PIVOT = (150, 300)

CAM_N = Vector((1, -1, 0)).normalized()       # 카메라 쪽을 보는 수평 방향
SCR_R = Vector((1, 1, 0)).normalized()        # 화면 오른쪽으로 가는 수평 방향


def scene():
	G.reset(samples=64)
	G.light_rig()


def holdout_mat():
	m = bpy.data.materials.new('lm_holdout')
	nt = m.node_tree
	for n in list(nt.nodes):
		nt.nodes.remove(n)
	o = nt.nodes.new('ShaderNodeOutputMaterial')
	h = nt.nodes.new('ShaderNodeHoldout')
	nt.links.new(h.outputs[0], o.inputs['Surface'])
	return m


def render(name, canvas, pivot, show, hold=()):
	"""show 만 보이게, hold 는 홀드아웃으로 렌더한다."""
	HOLD = bpy.data.materials.get('lm_holdout') or holdout_mat()
	saved = {}
	for o in bpy.data.objects:
		if o.type not in ('MESH', 'CURVE'):
			continue
		o.hide_render = not (o in show or o in hold)
		if o in hold:
			saved[o.name] = [sl.material for sl in o.material_slots]
			for sl in o.material_slots:
				sl.material = HOLD
	G.iso_camera(canvas[0], canvas[1], pivot[0], pivot[1], scale=G.SCALE)
	bpy.context.scene.frame_set(1)
	G.render_sprite(OUT, name)
	for o in bpy.data.objects:
		if o.name in saved:
			for sl, m in zip(o.material_slots, saved[o.name]):
				sl.material = m
		if o.type in ('MESH', 'CURVE'):
			o.hide_render = False
	STATICS[name] = dict(name=name, canvas=list(canvas), pivot=list(pivot))


def bake(ob, asset, res=1024):
	G.smart_uv(ob, margin=0.003)
	G.bake_asset_textures(ob, asset, res=res)


# =============================================================================
# 공용 부품
# =============================================================================
def mats_extra():
	M = {}
	M['iron'] = G.proc_mat('lm_iron', lin((0.18, 0.16, 0.16)), lin((0.26, 0.21, 0.18)),
		var_scale=8, var_amt=0.5, rough=0.55, metallic=0.85,
		macro=('noise', 30.0, 4.0), macro_amt=0.6, cavity=0.2, normal_strength=0.6,
		micro=('noise', 220.0, 3.0), bump_strength=0.15, height_scale=0.006)
	M['board'] = G.proc_mat('lm_board', lin((0.60, 0.43, 0.27)), lin((0.46, 0.32, 0.20)),
		var_scale=7, var_amt=0.6, rough=0.65, macro=('grain', 26.0, 'X'), macro_amt=0.8,
		cavity=0.25, normal_strength=0.8, micro=('noise', 150.0), bump_strength=0.15,
		height_scale=0.008)
	M['gold'] = G.simple_mat('lm_gold', lin((0.95, 0.76, 0.36)), rough=0.35, metallic=1.0)
	M['cream'] = G.simple_mat('lm_cream', lin((0.97, 0.92, 0.74)), rough=0.5)
	M['amber'] = G.simple_mat('lm_amber', lin((0.92, 0.58, 0.20)), rough=0.3, coat=0.5)
	M['foam'] = G.simple_mat('lm_foam', lin((0.98, 0.96, 0.90)), rough=0.7)
	M['burlap'] = G.proc_mat('lm_burlap', lin((0.78, 0.66, 0.46)), lin((0.66, 0.54, 0.36)),
		var_scale=5, var_amt=0.5, rough=0.9, macro=('noise', 10.0, 2.0), macro_amt=0.5,
		cavity=0.2, normal_strength=0.5, micro=('cells', 300.0, 3.0), bump_strength=0.2,
		height_scale=0.006)
	M['sail'] = G.proc_mat('lm_sail', lin((0.96, 0.92, 0.84)), lin((0.86, 0.80, 0.70)),
		var_scale=3, var_amt=0.5, rough=0.85, macro=('noise', 6.0, 2.0), macro_amt=0.6,
		cavity=0.15, normal_strength=0.5, micro=('cells', 260.0, 3.0), bump_strength=0.15,
		sheen=0.3, height_scale=0.006)
	M['glow'] = G.simple_mat('lm_glow', lin((1.0, 0.82, 0.48)), rough=0.3,
		emission=(lin((1.0, 0.78, 0.42)), 3.0))
	M['plum'] = G.proc_mat('lm_plum', lin((0.46, 0.18, 0.34)), lin((0.34, 0.12, 0.26)),
		var_scale=4, var_amt=0.5, rough=0.8, macro=('wave', 30.0, 1.0, 'Z'), macro_amt=0.7,
		cavity=0.25, normal_strength=0.7, micro=('noise', 200.0), bump_strength=0.12,
		sheen=0.5, height_scale=0.006)
	M['blue'] = G.proc_mat('lm_blue', lin((0.30, 0.42, 0.70)), lin((0.22, 0.32, 0.56)),
		var_scale=4, var_amt=0.4, rough=0.8, macro=('noise', 8.0, 2.0), macro_amt=0.5,
		cavity=0.2, normal_strength=0.5, micro=('cells', 260.0, 3.0), bump_strength=0.12,
		sheen=0.4, height_scale=0.006)
	return M


def hanging_sign(X, icon):
	"""정면(-Y) 벽 오른쪽에서 앞으로 뻗은 쇠 까치발 + 매달린 간판. 그림은 카메라 쪽(+X) 면.

	로컬 좌표(벽 = x 0, 까치발 = +x, 그림 면 = +y)로 만든 뒤 벽 자리로 옮긴다."""
	objs = []
	L = 0.42
	rod = G.cylinder('sg_rod', 0.016, 0.016, L, (L / 2, 0, 0.96), (0, 90, 0), seg=10)
	brace = G.cylinder('sg_brace', 0.011, 0.011, 0.32, (0.11, 0, 0.88), (0, 50, 0), seg=8)
	plate = G.box('sg_plate', (0.03, 0.11, 0.16), (0.012, 0, 0.92))
	scroll = G.torus('sg_scroll', 0.045, 0.009, (L + 0.03, 0, 0.985), (90, 0, 0), seg=16, ring=6)
	for o in (rod, brace, plate, scroll):
		o.data.materials.append(X['iron'])
		objs.append(o)
	for cx in (0.13, 0.35):
		ch = G.cylinder('sg_chain', 0.007, 0.007, 0.08, (cx, 0, 0.915), seg=6)
		ch.data.materials.append(X['iron'])
		objs.append(ch)
	bc = (0.24, 0, 0.74)
	board = G.box('sg_board', (0.34, 0.040, 0.27), bc)
	G.bevel(board, 0.014, 2)
	board.data.materials.append(X['board'])
	frame = G.box('sg_frame', (0.36, 0.028, 0.29), bc)
	G.bevel(frame, 0.008, 2)
	frame.data.materials.append(X['iron'])
	objs += [board, frame]
	fy = 0.024                 # 그림 면
	if icon == 'mug':
		mug = G.cylinder('sg_mug', 0.055, 0.064, 0.13, (0.225, fy, 0.725), seg=20)
		G.deform(mug, lambda v: Vector((v.x, fy + (v.y - fy) * 0.30, v.z)))
		mug.data.materials.append(X['amber'])
		handle = G.torus('sg_handle', 0.038, 0.012, (0.295, fy, 0.725), (90, 0, 0), seg=16, ring=6)
		handle.data.materials.append(X['amber'])
		foam = G.metaball('sg_foam', [mball((0.205, fy, 0.795), 0.034), mball((0.245, fy, 0.80), 0.036),
			mball((0.225, fy, 0.815), 0.030)], resolution=0.006)
		G.deform(foam, lambda v: Vector((v.x, min(v.y, fy + 0.016), v.z)))
		foam.data.materials.append(X['foam'])
		objs += [mug, handle, foam]
	else:
		moon = G.cylinder('sg_moon', 0.080, 0.080, 0.024, (0.225, fy, 0.735), (90, 0, 0), seg=32)
		cut = G.cylinder('sg_cut', 0.070, 0.070, 0.08, (0.255, fy, 0.755), (90, 0, 0), seg=32)
		bo = moon.modifiers.new('cut', 'BOOLEAN')
		bo.object = cut
		bo.operation = 'DIFFERENCE'
		G.set_active(moon)
		bpy.ops.object.modifier_apply(modifier='cut')
		bpy.data.objects.remove(cut)
		moon.data.materials.append(X['cream'])
		star = G.sphere('sg_star', 1.0, (0.315, fy, 0.80), (0.022, 0.010, 0.022), seg=10, rings=5)
		star.data.materials.append(X['cream'])
		objs += [moon, star]
	m = Matrix.Translation((0.48, -0.51, 0.0)) @ Matrix.Rotation(math.radians(-90), 4, 'Z')
	for o in objs:
		o.data.transform(m @ o.matrix_world)
		o.matrix_world = Matrix.Identity(4)
	return objs


def barrel(loc, X, M, lying=False):
	"""술통. lying 이면 옆으로 눕혀 아래 두 통 사이에 얹는다."""
	b = G.lathe('lm_barrel', [(0.10, 0.0), (0.125, 0.06), (0.135, 0.15), (0.125, 0.24),
		(0.10, 0.30)], seg=24, close_top=True)
	b.data.materials.append(M['wood'])
	objs = [b]
	for z in (0.05, 0.25):
		h = G.torus('lm_hoop', 0.127, 0.010, (0, 0, z), seg=24, ring=6)
		h.data.materials.append(X['iron'])
		objs.append(h)
	if lying:
		m = Matrix.Translation(Vector(loc) + Vector((0, 0, 0.13))) @ \
			Matrix.Rotation(math.radians(15), 4, 'Z') @ Matrix.Rotation(math.radians(90), 4, 'Y') @ \
			Matrix.Translation((0, 0, -0.15))
	else:
		m = Matrix.Translation(loc)
	for o in objs:
		o.data.transform(m)
	return objs


# =============================================================================
# 건물
# =============================================================================
def build_house_variant(kind):
	import env
	X = mats_extra()
	roof = None
	if kind == 'manor':
		roof = G.proc_mat('manor_slate', lin((0.24, 0.22, 0.27)), lin((0.16, 0.15, 0.19)),
			var_scale=5, var_amt=0.8, rough=0.6, macro=('noise', 14.0, 3.0), macro_amt=0.5,
			cavity=0.25, normal_strength=0.5, micro=('noise', 160.0, 4.0), bump_strength=0.2,
			height_scale=0.008)
	house, M = env.build_house(roof)
	extra = []
	overlay = []
	if kind == 'tavern':
		extra += hanging_sign(X, 'mug')
		extra += barrel((-0.44, -0.68, 0.0), X, M)
		extra += barrel((-0.16, -0.74, 0.0), X, M)
		b3 = barrel((-0.30, -0.71, 0.27), X, M, lying=True)
		extra += b3
	elif kind == 'inn':
		extra += hanging_sign(X, 'moon')
		lan_box = G.box('lm_lantern', (0.08, 0.08, 0.11), (0.44, -0.545, 0.66))
		G.bevel(lan_box, 0.01, 2)
		lan_box.data.materials.append(X['glow'])
		lan_cap = G.cylinder('lm_lantcap', 0.07, 0.01, 0.05, (0.44, -0.545, 0.74), seg=4)
		lan_cap.rotation_euler = (0, 0, math.radians(45))
		lan_arm = G.cylinder('lm_lantarm', 0.008, 0.008, 0.06, (0.44, -0.52, 0.78), (90, 0, 0), seg=8)
		for o in (lan_cap, lan_arm):
			o.data.materials.append(X['iron'])
		extra += [lan_box, lan_cap, lan_arm]
		for (x, y) in ((-0.02, -0.66), (0.46, -0.66)):
			pot = G.lathe('lm_pot', [(0.05, 0.0), (0.07, 0.10), (0.08, 0.13)], seg=16)
			pot.location = (x, y, 0)
			G.bake_transform(pot)
			pot.data.materials.append(M['terracotta'])
			leaf = G.ico('lm_shrub', 0.085, (x, y, 0.18), (1, 1, 0.9), sub=3)
			leaf.data.materials.append(M['leaf'])
			extra += [pot, leaf]
	elif kind == 'guild':
		# 문 위 문장판
		plq = G.box('lm_plaque', (0.24, 0.03, 0.14), (0.22, -0.525, 0.86))
		G.bevel(plq, 0.015, 2)
		plq.data.materials.append(X['blue'])
		emb = G.cylinder('lm_emblem', 0.035, 0.035, 0.02, (0.22, -0.545, 0.86), (90, 0, 0), seg=20)
		emb.data.materials.append(X['gold'])
		extra += [plq, emb]
		# 깃대: 앞 왼쪽 모서리에 세운다
		px, py = -0.70, -0.60
		pole = G.cylinder('lm_pole', 0.022, 0.018, 2.10, (px, py, 1.05), seg=12)
		pole.data.materials.append(M['wood'])
		fin = G.sphere('lm_finial', 0.040, (px, py, 2.13))
		fin.data.materials.append(X['gold'])
		foot = G.box('lm_polefoot', (0.12, 0.12, 0.10), (px, py, 0.05))
		G.bevel(foot, 0.02, 2)
		foot.data.materials.append(M['stone'])
		extra += [pole, fin, foot]
		overlay = ('flag', (px, py, 2.02))
	elif kind == 'manor':
		for x in (-0.47, 0.47):
			ban = G.box('lm_banner', (0.15, 0.018, 0.34), (x, -0.515, 0.70))
			bm = bmesh.new()
			bm.from_mesh(ban.data)
			for v in bm.verts:
				if v.co.z < 0.6:
					# 아래 끝을 뾰족하게
					v.co.z -= 0.06 * (1.0 - abs(v.co.x - x) / 0.075)
			bm.to_mesh(ban.data)
			bm.free()
			ban.data.materials.append(X['plum'])
			rod = G.cylinder('lm_banrod', 0.010, 0.010, 0.20, (x, -0.53, 0.875), (0, 90, 0), seg=8)
			rod.data.materials.append(X['gold'])
			emb = G.sphere('lm_banemb', 1.0, (x, -0.53, 0.74), (0.035, 0.008, 0.035), seg=12, rings=6)
			emb.data.materials.append(X['gold'])
			extra += [ban, rod, emb]
	body = G.join([house] + extra, 'Landmark_' + kind)
	return body, X, M, overlay


def build_mill():
	import env
	M = env.mats_common('mill')
	X = mats_extra()
	parts = []

	def add(ob, mat):
		ob.data.materials.append(mat)
		parts.append(ob)
		return ob

	base = G.lathe('ml_base', [(0.60, 0.0), (0.60, 0.20), (0.56, 0.24)], seg=40, close_top=True)
	G.bevel(base, 0.02, 2, angle=50)
	add(base, M['stone'])
	tower = G.lathe('ml_tower', [(0.54, 0.20), (0.53, 0.80), (0.48, 1.45), (0.44, 1.90)],
		seg=40, close_top=True, close_bottom=False)
	G.deform(tower, lambda v: Vector((v.x, v.y, v.z + 0.01 * math.sin(v.x * 9 + v.y * 5))))
	add(tower, M['plaster'])
	# 돌 띠
	add(G.torus('ml_band', 0.478, 0.035, (0, 0, 1.40), seg=40, ring=8), M['stone'])
	# 원뿔 지붕 (테라코타) + 꼭지
	roof = G.lathe('ml_roof', [(0.58, 1.84), (0.56, 1.88), (0.38, 2.10), (0.17, 2.28), (0.04, 2.38),
		(0.0, 2.40)], seg=40, close_bottom=True)
	add(roof, M['terracotta'])
	add(G.sphere('ml_knob', 0.05, (0, 0, 2.42)), M['wood'])
	# 문 (화면 왼쪽 아래 = -Y)
	door = G.box('ml_door', (0.26, 0.10, 0.44), (0.0, -0.53, 0.42))
	G.bevel(door, 0.02, 2)
	add(door, M['wood'])
	arch = G.cylinder('ml_arch', 0.13, 0.13, 0.10, (0.0, -0.53, 0.64), (90, 0, 0), seg=24)
	G.deform(arch, lambda v: Vector((v.x, v.y, max(v.z, 0.64))))
	add(arch, M['wood'])
	step = G.box('ml_step', (0.36, 0.16, 0.05), (0.0, -0.66, 0.025))
	G.bevel(step, 0.015, 2)
	add(step, M['stone'])
	# 작은 창 둘 (등불빛)
	GLASS = G.simple_mat('ml_glass', lin((1.0, 0.80, 0.46)), rough=0.25,
		emission=(lin((1.0, 0.76, 0.42)), 2.4))
	for (a, z) in ((-0.55, 1.15), (0.95, 0.75)):
		d = Vector((math.cos(a), math.sin(a), 0))
		r = 0.49 if z > 0.9 else 0.525
		w = G.box('ml_win', (0.13, 0.05, 0.17), tuple(d * r + Vector((0, 0, z))),
			(0, 0, math.degrees(a) + 90))
		add(w, GLASS)
		f = G.box('ml_winfr', (0.17, 0.035, 0.21), tuple(d * (r - 0.012) + Vector((0, 0, z))),
			(0, 0, math.degrees(a) + 90))
		add(f, M['wood'])
	# 날개 축 (카메라 쪽)
	hub_c = CAM_N * 0.52 + Vector((0, 0, 1.70))
	q = Vector((0, 0, 1)).rotation_difference(CAM_N)
	axle = G.cylinder('ml_axle', 0.05, 0.05, 0.20, (0, 0, 0), seg=16)
	axle.rotation_mode = 'QUATERNION'
	axle.rotation_quaternion = q
	axle.location = CAM_N * 0.48 + Vector((0, 0, 1.70))
	G.bake_transform(axle)
	add(axle, M['wood'])
	# 자루 둘과 밀가루 포대
	for (x, y, s) in ((0.52, -0.42, 1.0), (0.66, -0.20, 0.85)):
		sack = G.metaball('ml_sack', [mell((x, y, 0.11 * s), (0.11 * s, 0.10 * s, 0.12 * s)),
			mell((x, y, 0.24 * s), (0.06 * s, 0.05 * s, 0.04 * s))], resolution=0.012)
		add(sack, X['burlap'])
	mill = G.join(parts, 'Mill')
	return mill, M, X, hub_c


def build_blades(hub_c, M, X):
	"""날개 넷. 축 = CAM_N. 프레임마다 축을 기준으로 돌린다.

	탑과 따로 굽기 때문에 머티리얼을 공유하면 안 된다. 블렌더 베이크는 오브젝트가 쓰는
	모든 머티리얼의 활성 이미지 노드에 결과를 쓰므로, 탑의 텍스처를 덮어쓴다."""
	BW = G.proc_mat('blade_wood', lin((0.55, 0.39, 0.25)), lin((0.42, 0.29, 0.18)),
		var_scale=7, var_amt=0.6, rough=0.65, macro=('grain', 22.0, 'Z'), macro_amt=0.8,
		cavity=0.25, normal_strength=0.8, micro=('noise', 150.0), bump_strength=0.15,
		height_scale=0.008)
	M = dict(M, wood=BW)
	parts = []
	hub = G.cylinder('bl_hub', 0.075, 0.075, 0.06, (0, 0, 0.0), seg=20)
	hub.data.materials.append(M['wood'])
	parts.append(hub)
	for i in range(4):
		a = i * math.pi / 2
		# 날개 하나를 로컬 (x 옆, y 길이, z 축 방향) 에 만든 뒤 돌린다
		spar = G.box('bl_spar', (0.040, 1.00, 0.035), (0, 0.52, 0.02))
		spar.data.materials.append(M['wood'])
		sail = G.box('bl_sail', (0.22, 0.74, 0.012), (0.125, 0.60, 0.030))
		sail.data.materials.append(X['sail'])
		bars = []
		for k in range(4):
			b = G.box('bl_bar', (0.25, 0.014, 0.016), (0.12, 0.30 + k * 0.20, 0.040))
			b.data.materials.append(M['wood'])
			bars.append(b)
		rail = G.box('bl_rail', (0.016, 0.74, 0.016), (0.24, 0.60, 0.040))
		rail.data.materials.append(M['wood'])
		for o in [spar, sail, rail] + bars:
			o.matrix_world = Matrix.Rotation(a, 4, 'Z') @ o.matrix_world
			G.bake_transform(o)
			parts.append(o)
	blades = G.join(parts, 'Blades')
	# 로컬 Z(축) -> CAM_N, 중심 -> hub_c
	blades.rotation_mode = 'QUATERNION'
	q = Vector((0, 0, 1)).rotation_difference(CAM_N)
	blades.rotation_quaternion = q
	blades.location = hub_c
	return blades, q


def build_flag(anchor, X, phase):
	"""깃대 꼭대기에서 화면 오른쪽으로 펼쳐지는 깃발. phase 0..1 = 바람 한 주기."""
	W, H = 0.62, 0.40
	nu, nv = 28, 14
	bm = bmesh.new()
	grid = []
	for j in range(nv + 1):
		row = []
		for i in range(nu + 1):
			u = i / nu
			v = j / nv
			wave = math.sin(u * 7.0 - phase * 2 * math.pi) * 0.06 * u
			droop = -0.05 * u * u
			p = Vector(anchor) + SCR_R * (u * W) + Vector((0, 0, -v * H + droop)) + CAM_N * wave
			row.append(bm.verts.new(p))
		grid.append(row)
	faces = []
	for j in range(nv):
		for i in range(nu):
			f = bm.faces.new((grid[j][i], grid[j][i + 1], grid[j + 1][i + 1], grid[j + 1][i]))
			faces.append((f, (i + 0.5) / nu, (j + 0.5) / nv))
	# 문장: 가운데 금색 원
	for f, u, v in faces:
		du = (u - 0.52) * W
		dv = (v - 0.5) * H
		f.material_index = 1 if du * du + dv * dv < 0.085 ** 2 else 0
	ob = G.obj_from_bm('Flag', bm)
	G.solidify(ob, 0.012)
	# 굽지 않는 프로시저럴 그대로 쓴다 (문장판의 lm_blue 는 집 텍스처로 바뀌어 있다)
	cloth = bpy.data.materials.get('flag_cloth') or G.proc_mat('flag_cloth', lin((0.30, 0.42, 0.70)),
		lin((0.24, 0.34, 0.58)), var_scale=4, var_amt=0.4, rough=0.8, macro=('noise', 8.0, 2.0),
		macro_amt=0.5, cavity=0.2, normal_strength=0.5, sheen=0.4, height_scale=0.006)
	ob.data.materials.append(cloth)
	ob.data.materials.append(X['gold'])
	return ob


# =============================================================================
STATICS = {}
mp = os.path.join(OUT, 'meta.json')
if os.path.exists(mp):
	for st in json.load(open(mp)).get('statics', []):
		STATICS[st['name']] = st

for kind in WHICH:
	scene()
	random.seed(7)
	if kind == 'mill':
		mill, M, X, hub_c = build_mill()
		blades, q = build_blades(hub_c, M, X)
		if STAGE == 'model':
			G.iso_camera(600, 760, 300, 600, scale=4)
			G.render_sprite(os.path.join(G.ROOT, 'preview', 'landmarks'), 'mill')
			continue
		bake(mill, 'mill')
		bake(blades, 'mill_blades', res=512)
		render('mill', MILL_CANVAS, MILL_PIVOT, [mill])
		# 날개 16장: 90도 (날개 넷이라 한 주기)
		for k in range(16):
			blades.rotation_quaternion = q @ Matrix.Rotation(-k * (math.pi / 2) / 16, 4, 'Z').to_quaternion()
			bpy.context.view_layer.update()
			render('mill_blades_%d' % k, MILL_CANVAS, MILL_PIVOT, [blades], hold=[mill])
		blades.rotation_quaternion = q
		G.save_blend('mill')
	else:
		body, X, M, overlay = build_house_variant(kind)
		if STAGE == 'model':
			G.iso_camera(600, 660, 300, 500, scale=4)
			if overlay:
				build_flag(overlay[1], X, 0.0)
			G.render_sprite(os.path.join(G.ROOT, 'preview', 'landmarks'), kind)
			continue
		bake(body, 'house_' + kind)
		render('house_' + kind, HOUSE_CANVAS, HOUSE_PIVOT, [body])
		if overlay:
			for k in range(8):
				fl = build_flag(overlay[1], X, k / 8.0)
				render('guild_flag_%d' % k, HOUSE_CANVAS, HOUSE_PIVOT, [fl], hold=[body])
				bpy.data.objects.remove(fl)
		G.save_blend('house_' + kind)

if STAGE != 'model':
	order = ['house_tavern', 'house_inn', 'house_guild'] + ['guild_flag_%d' % k for k in range(8)] + \
		['mill'] + ['mill_blades_%d' % k for k in range(16)] + ['house_manor']
	meta = dict(asset='landmarks', scale=G.SCALE,
		statics=[STATICS[n] for n in order if n in STATICS])
	G.write_sprite_meta(OUT, meta)
print('[landmarks] done', WHICH)
