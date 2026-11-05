# -----------------------------------------------------------------------------
# tomatoma.py — 토마토마 (포획 등급 1, 튜토리얼 첫 사냥감)
#
# "재료가 먼저, 동물은 나중": 한눈에 토마토로 읽혀야 한다. 다섯 갈래 골,
# 어깨를 감싸며 눕는 별 모양 꼭지, 짧고 굵은 꼭다리. 윤기 나는 껍질(코트 +
# 서브서피스). 동물 요소는 큰 눈과 뭉툭한 잎사귀 발 두 개뿐.
#
# 애니메이션: idle(숨쉬기 스쿼시), hop(이동 점프), caught(빙글 돌며 줄어들고
# 빛이 되어 사라짐 — 유혈 없이 "받았다"로 읽히는 포획 연출, 머티리얼 발광 키).
# -----------------------------------------------------------------------------
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy
from mathutils import Vector
import ggcommon as G
from ggcommon import mball, mell, mcap, lin

STAGE = os.environ.get('GG_STAGE', 'all')

G.reset(samples=40)
G.light_rig()

R = 0.235           # 가로 반지름
H = 0.185           # 세로 반높이
CZ = 0.255          # 몸 중심 높이

SKIN = G.proc_mat('tomatoma_skin', lin((0.86, 0.17, 0.10)), lin((0.94, 0.36, 0.13)),
	var_scale=3.0, var_amt=0.55, rough=0.22, rough_var=0.08,
	macro=('noise', 9.0, 2.0, 0.4), macro_amt=0.5, cavity=0.08, normal_strength=0.35,
	micro=('noise', 240.0, 3.0), bump_strength=0.06,
	grad_z=(0.07, 0.42, 0.62, 1.08), sss=0.35, sss_radius=(0.9, 0.2, 0.1), coat=0.55,
	coat_rough=0.08, height_scale=0.006)
LEAF = G.proc_mat('tomatoma_leaf', lin((0.36, 0.58, 0.20)), lin((0.24, 0.43, 0.14)),
	var_scale=12, var_amt=0.6, rough=0.5, rough_var=0.1,
	macro=('wave', 28.0, 4.0, 'X'), macro_amt=0.6, cavity=0.25, normal_strength=0.6,
	micro=('noise', 200.0), bump_strength=0.1, sheen=0.3, sss=0.15,
	sss_radius=(0.3, 0.6, 0.2), height_scale=0.004)
EYE = G.simple_mat('tomatoma_eye', lin((0.12, 0.07, 0.06)), rough=0.12, coat=1.0)
SHINE = G.simple_mat('tomatoma_shine', (1, 1, 1), rough=0.2, emission=((1, 1, 1), 2.5))
MOUTH = G.simple_mat('tomatoma_mouth', lin((0.42, 0.08, 0.06)), rough=0.5)
BLUSH = G.simple_mat('tomatoma_blush', lin((1.0, 0.55, 0.45)), rough=0.4)

# ---- 몸: 구를 다섯 갈래 골과 꼭지 함몰로 깎는다 ----
body = G.sphere('tomatoma_body', 1.0, seg=64, rings=40)


def tomato(p):
	th = math.atan2(p.y, p.x)
	rho = math.sqrt(p.x * p.x + p.y * p.y)
	top = max(0.0, p.z)
	# 골: 위로 갈수록 깊다
	groove = (1.0 - abs(math.cos(2.5 * th)) ** 0.6) * (0.035 + 0.08 * top)
	k = 1.0 - groove
	x, y = p.x * k * R, p.y * k * R
	z = p.z * H
	# 위·아래 평평하게, 꼭지 자리 함몰
	z -= 0.05 * math.exp(-(rho / 0.42) ** 2) * (1 if p.z > 0 else 0.4)
	z *= 0.97 if p.z > 0 else 1.0
	return Vector((x, y, z + CZ))


G.deform(body, tomato)

# ---- 꼭지: 어깨에 눕는 다섯 갈래 + 받침 + 꼭다리 ----
leaves = []
top_z = CZ + H * 0.97 - 0.06
for i in range(5):
	a = math.radians(90 + i * 72 + 8)
	lf = G.sphere('tomatoma_sepal', 1.0, seg=20, rings=10, scale=(0.030, 0.105, 0.010))

	def bend(p, a=a):
		# 끝으로 갈수록 가늘어지고 끝이 살짝 들린다
		t = max(0.0, p.y / 0.105)
		w = 1.0 - 0.5 * t
		return Vector((p.x * w, p.y + 0.10, p.z + 0.006 * t * t))
	G.deform(lf, bend)
	lf.rotation_euler = (0, 0, a - math.pi / 2)
	G.bake_transform(lf)
	# 어깨에 눕힌다: 정점마다 몸 표면 높이를 재서 그 위에 얹는다
	for v in lf.data.vertices:
		hit, nrm = G.raycast_place(body, (v.co.x, v.co.y, 2.0), (0, 0, -1), 0.0)
		zs = hit.z if hit is not None else top_z
		v.co.z = zs + 0.010 + v.co.z
	lf.data.update()
	leaves.append(lf)
seat = G.sphere('tomatoma_seat', 1.0, (0, 0, top_z + 0.012), (0.055, 0.055, 0.016), seg=20, rings=10)
stem = G.cylinder('tomatoma_stem', 0.018, 0.014, 0.075, (0, 0, top_z + 0.05), (8, 0, 0), seg=12)
knob = G.sphere('tomatoma_knob', 0.019, (0, -0.005, top_z + 0.09), (1.2, 1.0, 0.8), seg=12, rings=8)

# ---- 발: 뭉툭한 잎사귀 발 ----
feet = G.metaball('tomatoma_feet', [
	mell((0.09, -0.03, 0.035), (0.05, 0.065, 0.035)),
	mell((-0.09, -0.03, 0.035), (0.05, 0.065, 0.035)),
	mcap((0.08, 0.0, 0.10), (0.09, -0.02, 0.04), 0.03),
	mcap((-0.08, 0.0, 0.10), (-0.09, -0.02, 0.04), 0.03),
], resolution=0.008)

body.data.materials.append(SKIN)
for o in leaves + [seat, stem, knob, feet]:
	o.data.materials.append(LEAF)

# ---- 얼굴 ----
face = []


def stick(name, mat, x, z, size, push):
	p, n = G.raycast_place(body, (x, -1.0, z), (0, 1, 0), push)
	ob = G.sphere(name, 1.0, (0, 0, 0), size, seg=18, rings=9)
	ob.rotation_mode = 'QUATERNION'
	ob.rotation_quaternion = Vector((0, -1, 0)).rotation_difference(n)
	ob.location = p
	G.bake_transform(ob)
	ob.data.materials.append(mat)
	face.append(ob)


for s in (1, -1):
	stick('tm_eye', EYE, 0.072 * s, CZ + 0.03, (0.034, 0.014, 0.044), 0.0)
	stick('tm_shine', SHINE, 0.064 * s, CZ + 0.05, (0.011, 0.005, 0.013), 0.012)
	stick('tm_blush', BLUSH, 0.135 * s, CZ - 0.02, (0.028, 0.006, 0.016), 0.0)
stick('tm_mouth', MOUTH, 0.0, CZ - 0.035, (0.020, 0.008, 0.010), 0.0)

if STAGE == 'model':
	G.iso_camera(400, 400, 200, 340, scale=8)
	G.render_sprite(os.path.join(G.ROOT, 'preview', 'tm_model'), 'iso')
	sys.exit(0)

# ---- 리그 ----
bones = [
	('root', (0, 0, 0), (0, 0.15, 0), None, False, 0, False),
	('body', (0, 0, 0.08), (0, 0, 0.44), 'root', True, 0, False),
	('calyx', (0, 0, top_z), (0, 0, top_z + 0.06), 'body', True, 0, False),
	('stem', (0, 0, top_z + 0.04), (0, 0, top_z + 0.11), 'calyx', True, 0, False),
	('foot.L', (0.085, 0.0, 0.11), (0.09, -0.02, 0.0), 'root', True, 0, False),
	('foot.R', (-0.085, 0.0, 0.11), (-0.09, -0.02, 0.0), 'root', True, 0, False),
]
arm = G.make_armature('Tomatoma_Rig', bones)
G.skin(body, arm, only='body')
for o in leaves + [seat]:
	G.skin(o, arm, only='calyx')
for o in (stem, knob):
	G.skin(o, arm, only='stem')
G.skin(feet, arm, ['foot.L', 'foot.R'], power=8, side_split=True)
for f in face:
	G.skin(f, arm, only='body')

tm = G.join([body, feet, seat, stem, knob] + leaves, 'Tomatoma')
fc = G.join(face, 'Tomatoma_Face')
G.bind(tm, arm)
G.bind(fc, arm)

# ---- 애니메이션 ----
TAU = 2 * math.pi
sin, cos = math.sin, math.cos

IDLE = G.anim_fn(arm, 'tomatoma_idle', 1, 25, 1, {
	'body': lambda t: dict(scale=(1 + 0.03 * sin(t * TAU), 1 - 0.035 * sin(t * TAU),
		1 + 0.03 * sin(t * TAU))),
	'calyx': lambda t: dict(rot=(4 * sin(t * TAU + 0.5), 0, 3 * cos(t * TAU))),
	'stem': lambda t: dict(rot=(8 * sin(t * TAU + 1.2), 0, 6 * cos(t * TAU + 0.4))),
})


def hop(t):
	"""0..0.2 웅크림, 0.2..0.75 체공, 0.75..1 착지 스쿼시."""
	if t < 0.2:
		u = t / 0.2
		return 0.0, -0.10 * math.sin(u * math.pi * 0.5)
	if t < 0.75:
		u = (t - 0.2) / 0.55
		return 0.085 * math.sin(u * math.pi), 0.12 * math.cos(u * math.pi)
	u = (t - 0.75) / 0.25
	return 0.0, -0.12 * math.sin(u * math.pi)


def hop_body(t):
	h, st = hop(t)
	return dict(loc=(0, h, 0), scale=(1 - st * 0.6, 1 + st, 1 - st * 0.6),
		rot=(-8 * math.sin(t * TAU), 0, 0))


HOP = G.anim_fn(arm, 'tomatoma_hop', 1, 13, 1, {
	'body': hop_body,
	'foot.L': lambda t: dict(loc=(0, -hop(t)[0], 0), rot=(-25 * max(0, hop(t)[0] / 0.085), 0, 0)),
	'foot.R': lambda t: dict(loc=(0, -hop(t)[0], 0), rot=(-25 * max(0, hop(t)[0] / 0.085), 0, 0)),
	'calyx': lambda t: dict(rot=(-14 * hop(t)[1] / 0.12, 0, 0)),
	'stem': lambda t: dict(rot=(-20 * hop(t)[1] / 0.12, 0, 0)),
})
# 발은 root 의 자식이라 몸과 같이 뜨게 위치를 더한다 (발 뼈 로컬 Y 가 아래쪽이라 부호 반대)


def caught_body(t):
	e = t * t * (3 - 2 * t)
	sc = 1.0 - 0.75 * e
	return dict(loc=(0, 0.12 * e, 0), rot=(0, 720 * e, 0), scale=(sc, sc, sc))


CAUGHT = G.anim_fn(arm, 'tomatoma_caught', 101, 125, 1, {
	'body': caught_body,
	'foot.L': lambda t: dict(scale=(max(0.01, 1 - t * 1.3),) * 3),
	'foot.R': lambda t: dict(scale=(max(0.01, 1 - t * 1.3),) * 3),
	'calyx': lambda t: dict(rot=(0, 0, 0)),
})

arm.animation_data.action = None
G.reset_pose(arm)
bpy.context.view_layer.update()

G.smart_uv(tm, margin=0.004)
G.bake_asset_textures(tm, 'tomatoma', res=512)

# ---- 포획 발광: 최종 머티리얼의 에미션을 키로 올린다 (프레임 101~125 구간에만) ----
for m in (bpy.data.materials['tomatoma_skin'], bpy.data.materials['tomatoma_leaf'], EYE):
	b = m.node_tree.nodes.get('BSDF') or m.node_tree.nodes.get('Principled BSDF')
	b.inputs['Emission Color'].default_value = (1.0, 0.86, 0.55, 1.0)
	for f, v in ((1, 0.0), (101, 0.0), (107, 0.25), (114, 1.2), (119, 3.5), (125, 9.0)):
		b.inputs['Emission Strength'].default_value = v
		b.inputs['Emission Strength'].keyframe_insert('default_value', frame=f)

if STAGE == 'anim':
	G.iso_camera(110, 140, 55, 120, scale=2)
	for act, frames in ((HOP, (1, 3, 5, 8, 11)), (CAUGHT, (101, 109, 117, 124)), (IDLE, (1, 13))):
		G.use_action(arm, act)
		for f in frames:
			bpy.context.scene.frame_set(f)
			G.render_sprite(os.path.join(G.ROOT, 'preview', 'tm_anim'), '%s_%03d' % (act.name, f))
	sys.exit(0)

anims = [
	('idle', IDLE, [1, 5, 9, 13, 17, 21], 6, 1),
	('hop', HOP, [1, 3, 4, 5, 7, 9, 10, 12], 12, 1),
	('caught', CAUGHT, [101, 104, 107, 110, 113, 116, 119, 122], 10, 0),
]
G.use_action(arm, IDLE)
G.render_character(arm, arm, 'tomatoma', (110, 140), (55, 120), anims)
arm.animation_data.action = IDLE
bpy.context.scene.frame_set(1)
G.save_blend('tomatoma')
print('[tomatoma] done')
