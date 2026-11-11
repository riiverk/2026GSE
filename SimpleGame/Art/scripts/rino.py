# -----------------------------------------------------------------------------
# rino.py — 주인공 리노 (소년, 시골 식당집 아이)
#
# 실루엣: 큰 머리(2.6등신), 갈색 클레이 머리카락, 푸른 회색 셔츠, 흰 앞치마,
# 테라코타 목수건, 무릎 바지 + 크림색 니삭스 + 갈색 부츠. 앞치마 주머니에 나무 국자.
# 허리에 작은 식칼 칼집 — 요리사이자 사냥꾼.
#
# 높이 약 1.47 BU (게임 1배 약 64 px). 정면 = 블렌더 -Y.
# -----------------------------------------------------------------------------
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy
from mathutils import Vector
import ggcommon as G
from ggcommon import mball, mell, mcap, lin

STAGE = os.environ.get('GG_STAGE', 'all')   # model | all

G.reset(samples=40)
G.light_rig()
G.MATTE = 1.0      # 캐릭터·몬스터는 반사광을 한 단계 죽인다 (음식은 그대로)

# =============================================================================
# 머티리얼
# =============================================================================
M = {}
M['skin'] = G.proc_mat('rino_skin', lin((0.93, 0.76, 0.62)), lin((0.95, 0.70, 0.60)),
	var_scale=6, var_amt=0.35, rough=0.55, rough_var=0.08,
	micro=('noise', 160.0, 3.0), bump_strength=0.08, sss=0.22,
	sss_radius=(0.5, 0.22, 0.12), height_scale=0.004)
M['hair'] = G.proc_mat('rino_hair', lin((0.40, 0.25, 0.15)), lin((0.30, 0.18, 0.10)),
	var_scale=5, var_amt=0.6, rough=0.45, rough_var=0.12,
	macro=('wave', 3.2, 3.0, 'Z'), macro_amt=0.5, cavity=0.15, normal_strength=0.45,
	micro=('noise', 90.0, 4.0), bump_strength=0.12, sheen=0.3, height_scale=0.010)
M['shirt'] = G.proc_mat('rino_shirt', lin((0.38, 0.46, 0.58)), lin((0.33, 0.40, 0.52)),
	var_scale=3, var_amt=0.5, rough=0.8, rough_var=0.06,
	macro=('noise', 5.0, 2.0, 0.5), macro_amt=0.7, cavity=0.18,
	micro=('cells', 260.0, 3.0), bump_strength=0.18, sheen=0.4, height_scale=0.012)
M['apron'] = G.proc_mat('rino_apron', lin((0.95, 0.92, 0.85)), lin((0.88, 0.80, 0.70)),
	var_scale=7, var_amt=0.25, rough=0.85, rough_var=0.05,
	macro=('noise', 6.0, 2.0, 0.5), macro_amt=0.6, cavity=0.15,
	micro=('cells', 300.0, 3.0), bump_strength=0.15, sheen=0.25, height_scale=0.010)
M['scarf'] = G.proc_mat('rino_scarf', lin((0.80, 0.38, 0.25)), lin((0.70, 0.30, 0.20)),
	var_scale=5, var_amt=0.4, rough=0.75, macro=('noise', 9.0, 2.0), macro_amt=0.6,
	micro=('cells', 260.0, 3.0), bump_strength=0.15, sheen=0.3, height_scale=0.010)
M['trousers'] = G.proc_mat('rino_trousers', lin((0.36, 0.30, 0.26)), lin((0.30, 0.25, 0.22)),
	var_scale=4, var_amt=0.5, rough=0.85, macro=('noise', 6.0, 2.0), macro_amt=0.6,
	micro=('wave', 220.0, 1.0, 'X'), bump_strength=0.15, height_scale=0.012)
M['socks'] = G.proc_mat('rino_socks', lin((0.92, 0.87, 0.76)), lin((0.86, 0.80, 0.68)),
	var_scale=5, var_amt=0.3, rough=0.9,
	macro=('wave', 90.0, 0.0, 'X'), macro_amt=0.7, cavity=0.2,
	micro=('noise', 200.0, 2.0), bump_strength=0.1, sheen=0.4, height_scale=0.006)
M['leather'] = G.proc_mat('rino_leather', lin((0.42, 0.26, 0.15)), lin((0.32, 0.19, 0.10)),
	var_scale=6, var_amt=0.6, rough=0.42, rough_var=0.15,
	macro=('noise', 14.0, 3.0), macro_amt=0.5, cavity=0.2,
	micro=('cells', 180.0, 4.0), bump_strength=0.2, coat=0.15, height_scale=0.008)
M['wood'] = G.proc_mat('rino_wood', lin((0.62, 0.45, 0.28)), lin((0.50, 0.34, 0.20)),
	var_scale=8, var_amt=0.6, rough=0.55, macro=('grain', 30.0, 'Z'), macro_amt=0.7,
	micro=('noise', 150.0), bump_strength=0.15, height_scale=0.006)
M['metal'] = G.proc_mat('rino_metal', lin((0.78, 0.78, 0.80)), lin((0.65, 0.66, 0.70)),
	var_scale=10, var_amt=0.3, rough=0.28, rough_var=0.1, metallic=1.0,
	micro=('noise', 300.0), bump_strength=0.05)

EYE = G.simple_mat('rino_eye', lin((0.16, 0.09, 0.05)), rough=0.15, coat=1.0)
SHINE = G.simple_mat('rino_eye_shine', (1, 1, 1), rough=0.2, emission=((1, 1, 1), 2.5))
MOUTH = G.simple_mat('rino_mouth', lin((0.55, 0.22, 0.18)), rough=0.5)
BLUSH = G.simple_mat('rino_blush', lin((0.98, 0.62, 0.55)), rough=0.6)
BROW = G.simple_mat('rino_brow', lin((0.28, 0.16, 0.09)), rough=0.6)

# =============================================================================
# 뼈 위치 (모델과 리그가 같은 숫자를 쓴다)
# =============================================================================
J = dict(
	hip=(0, 0, 0.42), spine=(0, 0, 0.55), chest=(0, 0, 0.68), neck=(0, 0, 0.84),
	head=(0, 0, 0.90), head_top=(0, 0, 1.40),
	sho=(0.155, 0.0, 0.795), elb=(0.215, 0.005, 0.635), wri=(0.245, -0.01, 0.49),
	hand_end=(0.255, -0.02, 0.405),
	thigh=(0.085, 0.0, 0.425), knee=(0.09, -0.012, 0.25), ankle=(0.09, 0.0, 0.075),
	toe=(0.09, -0.11, 0.03),
)


def mir(p):
	return (-p[0], p[1], p[2])


# =============================================================================
# 메쉬
# =============================================================================
parts = []

# ---- 몸 (셔츠 + 바지 + 니삭스 + 팔뚝 피부가 한 덩어리) ----
body_el = [
	mell((0, 0.005, 0.635), (0.168, 0.135, 0.20)),
	mell((0, -0.012, 0.52), (0.165, 0.14, 0.12)),
	mell((0, 0.0, 0.445), (0.150, 0.125, 0.085)),
]
for s in (1, -1):
	f = (lambda p: p) if s > 0 else mir
	body_el += [
		mball(f((0.140, 0.0, 0.785)), 0.07),
		mcap(f(J['sho']), f((0.208, 0.004, 0.66)), 0.058),
		mcap(f(J['thigh']), f((0.09, -0.010, 0.265)), 0.073),
	]
body = G.metaball('rino_body', body_el, resolution=0.014)

# ---- 팔뚝 + 손: 소매를 걷어 올린 맨팔. 몸과 다른 덩어리라 경계가 매끈하다 ----
arm_el = []
for s in (1, -1):
	f = (lambda p: p) if s > 0 else mir
	arm_el += [
		mcap(f((0.212, 0.004, 0.655)), f(J['wri']), 0.045),
		mell(f((0.252, -0.016, 0.445)), (0.042, 0.050, 0.052)),
		mball(f((0.230, -0.052, 0.465)), 0.021),
	]
arms = G.metaball('rino_arms', arm_el, resolution=0.010)

# 걷어 올린 소매 단: 팔꿈치 위 도넛
cuffs = []
for s in (1, -1):
	f = (lambda p: p) if s > 0 else mir
	a = Vector(f(J['sho'])); b = Vector(f(J['elb']))
	d = (b - a).normalized()
	c = G.torus('rino_cuff', 0.050, 0.020, (0, 0, 0), (0, 0, 0), seg=24, ring=8)
	c.rotation_mode = 'QUATERNION'
	c.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(d)
	c.location = a + (b - a) * 0.86
	G.bake_transform(c)
	cuffs.append(c)

# ---- 정강이: 니삭스 + 무릎 바지 단 ----
shin_el = []
knee_bands = []
for s in (1, -1):
	f = (lambda p: p) if s > 0 else mir
	shin_el += [mcap(f((0.09, -0.010, 0.27)), f((0.09, -0.003, 0.11)), 0.056)]
	kb = G.torus('rino_kneeband', 0.064, 0.020, f((0.09, -0.010, 0.262)), (0, 0, 0), seg=24, ring=8)
	knee_bands.append(kb)
shins = G.metaball('rino_shins', shin_el, resolution=0.010)

# ---- 부츠 ----
boot_el = []
for s in (1, -1):
	f = (lambda p: p) if s > 0 else mir
	boot_el += [
		mell(f((0.09, -0.032, 0.048)), (0.066, 0.105, 0.052)),
		mball(f((0.09, 0.0, 0.095)), 0.064),
		mell(f((0.09, -0.005, 0.135)), (0.066, 0.062, 0.03)),
	]
boots = G.metaball('rino_boots', boot_el, resolution=0.014)

# ---- 손 (벙어리장갑처럼 둥글게 + 엄지) ----

# ---- 머리 + 목 + 귀 ----
head_el = [
	mell((0, 0.0, 1.13), (0.300, 0.275, 0.272)),
	mball((0.12, -0.15, 1.035), 0.115),
	mball((-0.12, -0.15, 1.035), 0.115),
	mell((0, -0.10, 0.99), (0.15, 0.13, 0.09)),
	mcap((0, 0, 0.82), (0, 0, 0.95), 0.062),
	mell((0.292, 0.02, 1.09), (0.035, 0.05, 0.065)),
	mell((-0.292, 0.02, 1.09), (0.035, 0.05, 0.065)),
	mball((0, -0.262, 1.065), 0.032),
]
head = G.metaball('rino_head', head_el, resolution=0.014)

# ---- 머리카락: 뒤통수를 덮는 캡 + 앞머리 뭉치 + 정수리 삐침 ----
hair_el = [
	mell((0, 0.045, 1.215), (0.322, 0.300, 0.250)),
	mell((0, 0.11, 1.07), (0.29, 0.22, 0.17)),
	mell((0.25, 0.04, 1.13), (0.07, 0.17, 0.15)),
	mell((-0.25, 0.04, 1.13), (0.07, 0.17, 0.15)),
]
# 앞머리: 오른쪽(화면 왼쪽)으로 쓸어 넘긴 큰 덩어리 넷. 끝이 뾰족하게 모인다
for (x0, z0, x1, z1, r) in [(-0.22, 1.33, -0.27, 1.17, 0.060), (-0.08, 1.36, -0.17, 1.215, 0.066),
		(0.07, 1.36, -0.03, 1.225, 0.064), (0.20, 1.33, 0.13, 1.215, 0.058)]:
	hair_el.append(mcap((x0, -0.17, z0), (x1, -0.245, z1), r))
	hair_el.append(mball((x1, -0.25, z1 - 0.01), r * 0.55))
# 정수리 삐침 세 가닥 — 실루엣의 개성
hair_el += [
	mcap((0.02, 0.0, 1.43), (0.06, -0.05, 1.53), 0.035),
	mcap((-0.05, 0.03, 1.42), (-0.12, 0.02, 1.50), 0.032),
	mcap((0.10, 0.06, 1.40), (0.17, 0.10, 1.46), 0.028),
]
hair = G.metaball('rino_hair', hair_el, resolution=0.013)

# ---- 앞치마: 몸 앞면에 레이캐스트로 붙인 판 + 두께 ----
import bmesh


def surface_patch(name, target, xs, zs, width_fn, offset, flat_below=None, flare=0.0):
	"""target 정면에 붙는 격자 판. width_fn(z) -> 반폭."""
	bm = bmesh.new()
	rows = []
	last_y = None
	for zi, z in enumerate(zs):
		hw = width_fn(z)
		row = []
		for xi in range(xs):
			u = xi / (xs - 1) * 2 - 1
			x = u * hw
			if flat_below is not None and z < flat_below and last_y is not None:
				y = last_y[xi] - flare * (flat_below - z)
			else:
				p, n = G.raycast_place(target, (x, -1.0, z), (0, 1, 0), offset)
				y = p.y if p is not None else -0.15
			row.append(bm.verts.new((x, y, z)))
		last_y = [v.co.y for v in row]
		rows.append(row)
	for k in range(len(rows) - 1):
		for i in range(xs - 1):
			bm.faces.new((rows[k][i], rows[k][i + 1], rows[k + 1][i + 1], rows[k + 1][i]))
	ob = G.obj_from_bm(name, bm)
	return ob


def apron_w(z):
	if z > 0.62:
		return 0.085 + (0.78 - z) * 0.12
	return 0.13 + (0.62 - z) * 0.18


zs = [0.78 - i * 0.02 for i in range(26)]   # 0.78 .. 0.28
apron = surface_patch('rino_apron', body, 13, zs, apron_w, 0.012, flat_below=0.47, flare=0.035)
G.solidify(apron, 0.012)
G.subsurf(apron, 1)
# 주머니 + 국자
pocket = surface_patch('rino_pocket', apron, 7,
	[0.53 - i * 0.015 for i in range(6)], lambda z: 0.055, 0.004)
G.solidify(pocket, 0.008)
pocket.location.x = 0.035
G.bake_transform(pocket)
ladle_h = G.cylinder('rino_ladle_h', 0.009, 0.007, 0.16, (0.06, -0.205, 0.585), (12, 18, 0), seg=10)
ladle_b = G.sphere('rino_ladle_b', 0.028, (0.072, -0.215, 0.672), (1, 0.6, 1.0), seg=16, rings=8)

# 앞치마 끈: 목 고리 + 허리끈
neck_strap = G.torus('rino_strap_neck', 0.085, 0.009, (0, 0.005, 0.80), (12, 0, 0),
	scale=(1.12, 0.95, 1), seg=28, ring=6)
waist = G.torus('rino_strap_waist', 1.0, 0.011, (0, 0.0, 0.575), (0, 0, 0),
	scale=(0.175, 0.150, 1.0), seg=36, ring=6)

# ---- 목수건: 목에 감은 고리 + 앞 삼각 매듭 ----
scarf_el = [mcap((0.07, -0.05, 0.835), (-0.07, -0.05, 0.835), 0.034),
	mcap((0.07, -0.05, 0.835), (0.06, 0.06, 0.845), 0.032),
	mcap((-0.07, -0.05, 0.835), (-0.06, 0.06, 0.845), 0.032),
	mcap((0.06, 0.06, 0.845), (-0.06, 0.06, 0.845), 0.032),
	mell((0.0, -0.10, 0.80), (0.05, 0.022, 0.05)),
	mcap((0.0, -0.10, 0.79), (0.03, -0.11, 0.73), 0.022),
	mcap((0.0, -0.10, 0.79), (-0.035, -0.105, 0.735), 0.020)]
scarf = G.metaball('rino_scarf', scarf_el, resolution=0.010)

# ---- 벨트 + 칼집 + 칼자루 ----
belt = G.torus('rino_belt', 1.0, 0.018, (0, 0.0, 0.465), (0, 0, 0),
	scale=(0.158, 0.135, 1.0), seg=36, ring=8)
buckle = G.box('rino_buckle', (0.045, 0.012, 0.035), (0.0, -0.148, 0.465))
G.bevel(buckle, 0.005, 2)
sheath = G.box('rino_sheath', (0.03, 0.022, 0.11), (0.165, -0.035, 0.42), (0, 12, 0))
G.bevel(sheath, 0.008, 3)
knife = G.cylinder('rino_knife', 0.012, 0.013, 0.055, (0.172, -0.035, 0.50), (0, 12, 0), seg=10)

# =============================================================================
# 얼굴 (베이크하지 않는 작은 부품, 레이캐스트로 머리 표면에 붙인다)
# =============================================================================
face_parts = []


def stick(name, mat, x, z, size, rot_extra=(0, 0, 0), push=0.0, sphere_seg=20):
	p, n = G.raycast_place(head, (x, -1.0, z), (0, 1, 0), push)
	if p is None:
		return None
	ob = G.sphere(name, 1.0, (0, 0, 0), size, (0, 0, 0), seg=sphere_seg, rings=sphere_seg // 2)
	# 표면 노멀을 -Y 로 보는 판에 맞춘다
	q = Vector((0, -1, 0)).rotation_difference(n)
	ob.rotation_mode = 'QUATERNION'
	ob.rotation_quaternion = q
	ob.location = p
	G.bake_transform(ob)
	ob.data.materials.append(mat)
	face_parts.append(ob)
	return ob


for s in (1, -1):
	stick('rino_eye', EYE, 0.108 * s, 1.085, (0.044, 0.018, 0.062), push=0.002)
	stick('rino_shine', SHINE, 0.096 * s + 0.004, 1.11, (0.014, 0.006, 0.016), push=0.016,
		sphere_seg=12)
	stick('rino_shine2', SHINE, 0.122 * s, 1.062, (0.007, 0.004, 0.008), push=0.014,
		sphere_seg=10)
	stick('rino_blush', BLUSH, 0.175 * s, 1.005, (0.040, 0.008, 0.022), push=0.0)
	b = stick('rino_brow', BROW, 0.108 * s, 1.18, (0.040, 0.010, 0.010), push=0.0)
stick('rino_mouth', MOUTH, 0.0, 0.985, (0.030, 0.010, 0.013), push=0.0)

# =============================================================================
# 머티리얼 배정 + 합치기
# =============================================================================
for ob, key in [(boots, 'leather'), (arms, 'skin'), (shins, 'socks'), (head, 'skin'), (hair, 'hair'),
		(apron, 'apron'), (pocket, 'apron'), (neck_strap, 'apron'), (waist, 'apron'),
		(scarf, 'scarf'), (belt, 'leather'), (buckle, 'metal'), (sheath, 'leather'),
		(knife, 'wood'), (ladle_h, 'wood'), (ladle_b, 'wood')]:
	ob.data.materials.append(M[key])

for c in cuffs:
	c.data.materials.append(M['shirt'])
for kb in knee_bands:
	kb.data.materials.append(M['trousers'])

# 몸은 허리띠 높이에서 셔츠/바지로 나눈다. 경계는 벨트가 덮는다
for k in ('shirt', 'trousers'):
	body.data.materials.append(M[k])
for p in body.data.polygons:
	p.material_index = 1 if p.center.z < 0.468 else 0

if STAGE == 'model':
	G.iso_camera(512, 512, 256, 470, scale=5)
	cam = bpy.data.objects['GG_Cam']
	G.render_sprite(os.path.join(G.ROOT, 'preview', 'rino_model'), 'iso')
	# 정면
	cam.rotation_euler = (math.radians(90), 0, 0)
	cam.location = (0, -10, 0.74)
	cam.data.shift_x = 0; cam.data.shift_y = 0
	cam.data.ortho_scale = 1.7
	G.render_sprite(os.path.join(G.ROOT, 'preview', 'rino_model'), 'front')
	cam.rotation_euler = (math.radians(90), 0, math.radians(90))
	cam.location = (10, 0, 0.74)
	G.render_sprite(os.path.join(G.ROOT, 'preview', 'rino_model'), 'side')
	sys.exit(0)

# =============================================================================
# 리그
# =============================================================================
def side(p, s):
	return p if s > 0 else mir(p)


bones = [
	('root', (0, 0, 0), (0, 0.25, 0), None, False, 0, False),
	('hips', J['hip'], J['spine'], 'root', True, 0, False),
	('spine', J['spine'], J['chest'], 'hips', True, 0, True),
	('chest', J['chest'], J['neck'], 'spine', True, 0, True),
	('neck', J['neck'], J['head'], 'chest', True, 0, True),
	('head', J['head'], J['head_top'], 'neck', True, 0, True),
]
for s, sfx in ((1, '.L'), (-1, '.R')):
	bones += [
		('upper_arm' + sfx, side(J['sho'], s), side(J['elb'], s), 'chest', True, 0, False),
		('forearm' + sfx, side(J['elb'], s), side(J['wri'], s), 'upper_arm' + sfx, True, 0, True),
		('hand' + sfx, side(J['wri'], s), side(J['hand_end'], s), 'forearm' + sfx, True, 0, True),
		('thigh' + sfx, side(J['thigh'], s), side(J['knee'], s), 'hips', True, 0, False),
		('shin' + sfx, side(J['knee'], s), side(J['ankle'], s), 'thigh' + sfx, True, 0, True),
		# IK 컨트롤: 위를 향하게 둬서 로컬 축이 (X=옆, Y=위, Z=앞) 이 되게 한다
		('ik_foot' + sfx, side(J['ankle'], s), side((0.09, 0.0, 0.175), s), 'root', False, 0, False),
		('foot' + sfx, side(J['ankle'], s), side(J['toe'], s), 'ik_foot' + sfx, True, 0, False),
		('pole_knee' + sfx, side((0.09, -0.6, 0.25), s), side((0.09, -0.6, 0.33), s), 'root',
			False, 0, False),
	]
arm = G.make_armature('Rino_Rig', bones)

# ---- IK + 폴 각도 자동 결정 ----
for sfx in ('.L', '.R'):
	G.ik(arm, 'shin' + sfx, 'ik_foot' + sfx, 'pole_knee' + sfx, chain=2, pole_angle=-90)


def knee_check(sfx):
	vl = bpy.context.view_layer
	pb = arm.pose.bones['ik_foot' + sfx]
	pb.location = (0, 0.10, 0.06)          # 발을 앞·위로: 무릎이 굽어야 한다
	vl.update()
	k = arm.matrix_world @ arm.pose.bones['shin' + sfx].head
	pb.location = (0, 0, 0)
	vl.update()
	return k


best = None
for ang in (-90, 90, 0, 180):
	for sfx in ('.L', '.R'):
		arm.pose.bones['shin' + sfx].constraints['IK'].pole_angle = math.radians(ang)
	k = knee_check('.L')
	score = k.y + abs(k.x - 0.09) * 2     # 무릎이 앞(-Y)으로, 옆으로 돌지 않게
	if best is None or score < best[0]:
		best = (score, ang, tuple(round(c, 3) for c in k))
for sfx in ('.L', '.R'):
	arm.pose.bones['shin' + sfx].constraints['IK'].pole_angle = math.radians(best[1])
print('[rino] pole angle', best)

# ---- 스키닝 ----
LIMB = lambda n: [n + '.L', n + '.R']
G.skin(body, arm, ['hips', 'spine', 'chest'] + LIMB('upper_arm') + LIMB('thigh'),
	power=8, side_split=True)
G.skin(arms, arm, LIMB('upper_arm') + LIMB('forearm') + LIMB('hand'), power=8, side_split=True)
for c in cuffs:
	G.skin(c, arm, LIMB('upper_arm') + LIMB('forearm'), power=8, side_split=True)
G.skin(shins, arm, LIMB('thigh') + LIMB('shin'), power=8, side_split=True)
for kb in knee_bands:
	G.skin(kb, arm, LIMB('thigh') + LIMB('shin'), power=8, side_split=True)
G.skin(boots, arm, LIMB('shin') + LIMB('foot'), power=10, side_split=True)
G.skin(head, arm, ['chest', 'neck', 'head'], power=8)
G.skin(hair, arm, only='head')
for ob in (apron, pocket, ladle_h, ladle_b):
	G.skin(ob, arm, ['chest', 'spine', 'hips'] + LIMB('thigh'), power=5)
G.skin(neck_strap, arm, ['chest', 'neck'], power=6)
G.skin(waist, arm, ['spine', 'hips'], power=6)
G.skin(scarf, arm, ['chest', 'neck'], power=6)
for ob in (belt, buckle, sheath, knife):
	G.skin(ob, arm, ['hips', 'spine'], power=6)
for fp in face_parts:
	G.skin(fp, arm, only='head')

# ---- 사냥용 큰 국자 (레벨 1). 오른손에 쥔다. 평소 스프라이트에서는 숨긴다 ----
STEEL = G.simple_mat('rino_steel', lin((0.80, 0.82, 0.86)), rough=0.25, metallic=1.0)
GRIP = (-0.252, -0.030, 0.452)
hunt_handle = G.cylinder('rino_hl_handle', 0.017, 0.014, 0.54, (GRIP[0], GRIP[1], GRIP[2] - 0.23), seg=12)
hunt_cap = G.sphere('rino_hl_cap', 0.024, (GRIP[0], GRIP[1], GRIP[2] + 0.045))
bowl_c = Vector((GRIP[0], GRIP[1] - 0.075, GRIP[2] - 0.54))
hunt_bowl = G.lathe('rino_hl_bowl', [(0.0, -0.085), (0.048, -0.075), (0.075, -0.048), (0.087, -0.014),
	(0.090, 0.0), (0.082, 0.0), (0.077, -0.011), (0.066, -0.038), (0.041, -0.063), (0.0, -0.071)],
	seg=28, close_bottom=False)
# 입구가 앞(-Y)을 보게 눕힌다
hunt_bowl.rotation_euler = (math.radians(-90), 0, 0)
hunt_bowl.location = bowl_c
G.bake_transform(hunt_bowl)
hunt_neck = G.cylinder('rino_hl_neck', 0.013, 0.013, 0.09, tuple(bowl_c + Vector((0, 0.04, 0.06))),
	(-40, 0, 0), seg=10)
hunt = [hunt_handle, hunt_cap, hunt_bowl, hunt_neck]
for o in hunt:
	o.data.materials.append(STEEL)
	G.skin(o, arm, only='hand.R')
hunt_ladle = G.join(hunt, 'Rino_HuntLadle')
G.bind(hunt_ladle, arm)
hunt_ladle.hide_render = True

rino = G.join([body, arms, shins, boots, head, hair, apron, pocket, ladle_h, ladle_b, neck_strap,
	waist, scarf, belt, buckle, sheath, knife] + cuffs + knee_bands, 'Rino')
face = G.join(face_parts, 'Rino_Face')
G.bind(rino, arm)
G.bind(face, arm)
print('[rino] verts', len(rino.data.vertices))

# =============================================================================
# 애니메이션
# =============================================================================
TAU = 2 * math.pi
sin, cos = math.sin, math.cos


def idle(t):
	b = sin(t * TAU)
	return b


IDLE_FNS = {
	'hips': lambda t: dict(loc=(0, -0.004 * (0.5 - 0.5 * cos(t * TAU)), 0)),
	'spine': lambda t: dict(rot=(1.2 * sin(t * TAU), 0, 0)),
	'chest': lambda t: dict(rot=(-1.5 * sin(t * TAU), 0, 0), scale=(1 + 0.012 * sin(t * TAU),) * 3),
	'head': lambda t: dict(rot=(1.5 * sin(t * TAU + 0.6), 0, 3.0 * sin(t * TAU))),
	'upper_arm.L': lambda t: dict(rot=(2.5 * sin(t * TAU), 0, -2 - 1.5 * sin(t * TAU))),
	'upper_arm.R': lambda t: dict(rot=(2.5 * sin(t * TAU), 0, 2 + 1.5 * sin(t * TAU))),
	'forearm.L': lambda t: dict(rot=(-8 - 3 * sin(t * TAU), 0, 0)),
	'forearm.R': lambda t: dict(rot=(-8 - 3 * sin(t * TAU), 0, 0)),
}
IDLE = G.anim_fn(arm, 'rino_idle', 1, 25, 2, IDLE_FNS)

STRIDE = 0.075
LIFT = 0.055


def foot_fn(ph):
	def f(t):
		a = t * TAU + ph
		fwd = STRIDE * cos(a)
		lift = LIFT * max(0.0, -sin(a))
		return dict(loc=(0, lift, fwd))
	return f


WALK_FNS = {
	'ik_foot.L': foot_fn(0.0),
	'ik_foot.R': foot_fn(math.pi),
	'hips': lambda t: dict(loc=(0, -0.018 + 0.016 * abs(sin(t * TAU)), 0),
		rot=(0, 7 * cos(t * TAU), 0)),
	'spine': lambda t: dict(rot=(5, 0, 0)),
	'chest': lambda t: dict(rot=(1.5, -9 * cos(t * TAU), 0)),
	'head': lambda t: dict(rot=(-4 + 2 * sin(2 * t * TAU), 2.5 * cos(t * TAU), 0)),
	'upper_arm.L': lambda t: dict(rot=(30 * cos(t * TAU), 0, -4)),
	'upper_arm.R': lambda t: dict(rot=(-30 * cos(t * TAU), 0, 4)),
	'forearm.L': lambda t: dict(rot=(-18 - 14 * max(0, -cos(t * TAU)), 0, 0)),
	'forearm.R': lambda t: dict(rot=(-18 - 14 * max(0, cos(t * TAU)), 0, 0)),
}
WALK = G.anim_fn(arm, 'rino_walk', 1, 17, 1, WALK_FNS)


# ---- 감사 의례: 두 손을 가슴 앞에 모으고 고개 숙여 절한다 ----
def solve_clasp(sfx, target):
	"""손끝이 target 에 오도록 팔 회전을 격자 탐색 (몇 백 번이면 충분)."""
	vl = bpy.context.view_layer
	ua = arm.pose.bones['upper_arm' + sfx]
	fa = arm.pose.bones['forearm' + sfx]
	sgn = 1 if sfx == '.L' else -1
	best = None
	for ux in range(-80, 1, 8):
		for uz in range(0, 76, 5):
			for fx in range(-120, -19, 10):
				ua.rotation_euler = (math.radians(ux), 0, math.radians(uz * sgn))
				fa.rotation_euler = (math.radians(fx), 0, 0)
				vl.update()
				h = arm.matrix_world @ arm.pose.bones['hand' + sfx].tail
				d = (h - Vector(target)).length
				if best is None or d < best[0]:
					best = (d, ux, uz * sgn, fx)
	ua.rotation_euler = (0, 0, 0)
	fa.rotation_euler = (0, 0, 0)
	vl.update()
	return best


G.use_action(arm, IDLE)
arm.animation_data.action = None
G.reset_pose(arm)
cL = solve_clasp('.L', (0.012, -0.23, 0.70))
cR = solve_clasp('.R', (-0.012, -0.23, 0.70))
print('[rino] clasp', cL, cR)


def ease(x):
	x = max(0.0, min(1.0, x))
	return x * x * (3 - 2 * x)


def grat(t):
	# 0..0.25 손 모으기, 0.3..0.7 절, 0.85..1 복귀
	hands = ease(t / 0.25) * (1 - ease((t - 0.82) / 0.18))
	bow = ease((t - 0.28) / 0.17) * (1 - ease((t - 0.68) / 0.17))
	return hands, bow


def arm_fn(sfx, c):
	def f(t):
		h, b = grat(t)
		return dict(rot=(c[1] * h, 0, c[2] * h))
	return f


def fore_fn(sfx, c):
	def f(t):
		h, b = grat(t)
		return dict(rot=(c[3] * h, 0, 0))
	return f


GRAT = G.anim_fn(arm, 'rino_gratitude', 1, 33, 1, {
	'upper_arm.L': arm_fn('.L', cL),
	'upper_arm.R': arm_fn('.R', cR),
	'forearm.L': fore_fn('.L', cL),
	'forearm.R': fore_fn('.R', cR),
	'hand.L': lambda t: dict(rot=(0, 0, 20 * grat(t)[0])),
	'hand.R': lambda t: dict(rot=(0, 0, -20 * grat(t)[0])),
	'hips': lambda t: dict(loc=(0, -0.01 * grat(t)[1], 0.025 * grat(t)[1]),
		rot=(4 * grat(t)[1], 0, 0)),
	'spine': lambda t: dict(rot=(8 * grat(t)[1], 0, 0)),
	'chest': lambda t: dict(rot=(6 * grat(t)[1], 0, 0)),
	'head': lambda t: dict(rot=(10 * grat(t)[1] - 4 * grat(t)[0] * (1 - grat(t)[1]), 0, 0)),
})

# ---- 레벨 1: 국자를 든 idle / walk + 내려치기 ----
# 국자를 칼처럼 비껴 든다: 손목을 앞으로 꺾어 국자 머리가 앞쪽 아래에 온다
HOLD_R = dict(rot=(-38, 0, 8))
IDLE_L = G.anim_fn(arm, 'rino_idle_ladle', 1, 25, 2, dict(IDLE_FNS, **{
	'hand.R': lambda t: HOLD_R,
	'upper_arm.R': lambda t: dict(rot=(-12 + 2.5 * sin(t * TAU), 0, 6)),
	'forearm.R': lambda t: dict(rot=(-22, 0, 0)),
}))
WALK_L = G.anim_fn(arm, 'rino_walk_ladle', 1, 17, 1, dict(WALK_FNS, **{
	'hand.R': lambda t: HOLD_R,
	'upper_arm.R': lambda t: dict(rot=(-12 - 10 * cos(t * TAU), 0, 6)),
	'forearm.R': lambda t: dict(rot=(-26, 0, 0)),
}))


def atk(t):
	"""Level1.cpp 의 LADLE_T_* 구간과 맞춘다: 0~0.36 들어올림, ~0.54 내려침,
	~0.68 찍은 채 멈춤, ~1 복귀. 반환 (치켜든 정도 0..1, 내려친 정도 0..1)."""
	if t < 0.36:
		return ease(t / 0.36), 0.0
	if t < 0.54:
		k = (t - 0.36) / 0.18
		return 1.0 - k * k, k * k
	if t < 0.68:
		return 0.0, 1.0
	k = ease((t - 0.68) / 0.32)
	return 0.0, 1.0 - k


def atk_arm(t):
	r, s_ = atk(t)
	# 평소(-12) -> 머리 위(-175) -> 앞 아래로 내려침(-70)
	return dict(rot=(-12 + (-163) * r + (-58) * s_ * (1 - r), 0, 6 - 10 * r))


ATTACK = G.anim_fn(arm, 'rino_attack', 1, 33, 1, {
	'upper_arm.R': atk_arm,
	'forearm.R': lambda t: dict(rot=(-22 + 12 * atk(t)[0] + 10 * atk(t)[1], 0, 0)),
	'hand.R': lambda t: dict(rot=(-38 + 20 * atk(t)[0] + 8 * atk(t)[1], 0, 8)),
	'upper_arm.L': lambda t: dict(rot=(10 * atk(t)[0] - 18 * atk(t)[1], 0, -10 * atk(t)[0])),
	'forearm.L': lambda t: dict(rot=(-20, 0, 0)),
	'spine': lambda t: dict(rot=(-6 * atk(t)[0] + 10 * atk(t)[1], 0, 0)),
	'chest': lambda t: dict(rot=(-5 * atk(t)[0] + 8 * atk(t)[1], -10 * atk(t)[0] + 6 * atk(t)[1], 0)),
	'head': lambda t: dict(rot=(-4 * atk(t)[0] + 6 * atk(t)[1], 0, 0)),
	'hips': lambda t: dict(loc=(0, -0.015 * atk(t)[1], 0)),
	'ik_foot.L': lambda t: dict(loc=(0, 0, 0.05 * atk(t)[1])),
})

for a in (IDLE, WALK, GRAT, IDLE_L, WALK_L, ATTACK):
	a.use_fake_user = True
G.use_action(arm, IDLE)
arm.animation_data.action = None
G.reset_pose(arm)
bpy.context.view_layer.update()

# =============================================================================
# 텍스처 베이크 (레스트 포즈에서)
# =============================================================================
G.smart_uv(rino, margin=0.004)
G.bake_asset_textures(rino, 'rino', res=1024)

if STAGE == 'anim':
	# 애니메이션 검수: 정면 근접, 각 액션 몇 프레임
	G.iso_camera(160, 200, 80, 180, scale=2.4)
	hunt_ladle.hide_render = False
	for act, frames in ((ATTACK, (1, 9, 13, 17, 21, 29)), (WALK_L, (1, 9)), (IDLE_L, (1,))):
		G.use_action(arm, act)
		for f in frames:
			bpy.context.scene.frame_set(f)
			G.render_sprite(os.path.join(G.ROOT, 'preview', 'rino_anim'), '%s_%02d' % (act.name, f))
	sys.exit(0)

# =============================================================================
# 스프라이트 렌더 + 저장
# =============================================================================
anims = [
	('idle', IDLE, [1, 5, 9, 13, 17, 21], 6, 1),
	('walk', WALK, [1, 3, 5, 7, 9, 11, 13, 15], 12, 1),
	('gratitude', GRAT, [1, 5, 9, 13, 17, 21, 25, 29], 8, 0),
]
G.use_action(arm, IDLE)
meta = G.render_character(arm, arm, 'rino', (128, 176), (64, 160), anims)
hunt_ladle.hide_render = False
G.render_character(arm, arm, 'rino_ladle', (176, 200), (88, 170), [
	('idle', IDLE_L, [1, 5, 9, 13, 17, 21], 6, 1),
	('walk', WALK_L, [1, 3, 5, 7, 9, 11, 13, 15], 12, 1),
	('attack', ATTACK, [1, 5, 9, 13, 17, 21, 25, 29], 8, 0),
])
hunt_ladle.hide_render = True
arm.animation_data.action = IDLE
G.save_blend('rino')
print('[rino] done')
