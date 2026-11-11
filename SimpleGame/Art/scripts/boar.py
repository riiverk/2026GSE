# -----------------------------------------------------------------------------
# boar.py — 알밤멧돼지 (레벨 1 의 선공 미식종, 포획 3 / 미식 ★★)
#
# 등은 반들반들한 알밤 껍질(짙은 밤색), 배와 주둥이 아래는 밤 속살 같은 크림색.
# 등줄기에 밤송이 가시 갈기 (끝이 둥근 연두갈색 가시). 짧고 굵은 다리, 둥근
# 들창코, 작은 상아 엄니, 돌돌 말린 꼬리. 현실 멧돼지처럼 길쭉하지 않고
# 알밤처럼 통통한 물방울 몸.
#
# 높이 약 0.62 BU, 길이 약 0.95 BU. 정면 = 블렌더 -Y. 무광 설정.
# 동작 (Level1 의 상태와 맞춘다)
#   idle 6     MS_WANDER 정지   숨쉬기, 코 킁킁
#   run 8      이동·추격         뒤뚱 달리기
#   windup 6   MS_WINDUP         움츠리며 앞발로 땅 긁기 (0.34 초)
#   charge 4   MS_CHARGE         고개 숙이고 돌진 (루프)
#   caught 8   쓰러질 때          빙글 돌며 작아지고 빛이 된다 (유혈 없음)
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
G.MATTE = 1.0

# ---- 머티리얼 ----
# 몸: 등은 밤 껍질, 아래는 크림. 높이 그라디언트(grad_z)로 경계를 나눈다
SHELL = G.proc_mat('boar_shell', lin((0.93, 0.82, 0.62)), lin((0.86, 0.72, 0.52)),
	var_scale=4, var_amt=0.5, rough=0.45, rough_var=0.12,
	macro=('noise', 9.0, 3.0), macro_amt=0.35, cavity=0.12, normal_strength=0.4,
	micro=('noise', 180.0, 3.0), bump_strength=0.1, coat=0.35, sheen=0.2,
	top=(0.38, 0.47, lin((0.45, 0.24, 0.13))), height_scale=0.006)
BURR = G.proc_mat('boar_burr', lin((0.70, 0.64, 0.34)), lin((0.50, 0.42, 0.20)),
	var_scale=6, var_amt=0.7, rough=0.7, macro=('noise', 30.0, 3.0), macro_amt=0.5,
	cavity=0.25, normal_strength=0.6, micro=('noise', 200.0, 3.0), bump_strength=0.15,
	height_scale=0.004)
SNOUT = G.proc_mat('boar_snout', lin((0.86, 0.60, 0.52)), lin((0.76, 0.48, 0.42)),
	var_scale=10, var_amt=0.4, rough=0.5, micro=('noise', 220.0, 3.0), bump_strength=0.1,
	sss=0.15, height_scale=0.003)
HOOF = G.simple_mat('boar_hoof', lin((0.24, 0.16, 0.12)), rough=0.5)
TUSK = G.simple_mat('boar_tusk', lin((0.96, 0.92, 0.80)), rough=0.35, coat=0.3)
EYE = G.simple_mat('boar_eye', lin((0.08, 0.05, 0.04)), rough=0.12, coat=1.0)
SHINE = G.simple_mat('boar_shine', (1, 1, 1), rough=0.2, emission=((1, 1, 1), 2.5))
NOSTRIL = G.simple_mat('boar_nostril', lin((0.36, 0.18, 0.16)), rough=0.6)
BROW = G.simple_mat('boar_brow', lin((0.22, 0.12, 0.08)), rough=0.6)

# ---- 몸 (정면 -Y) ----
LEGS = [((0.15, -0.20), 'f'), ((-0.15, -0.20), 'f'), ((0.15, 0.22), 'b'), ((-0.15, 0.22), 'b')]
el = [
	mell((0, 0.04, 0.38), (0.27, 0.40, 0.25)),      # 알밤 몸통
	mball((0, -0.20, 0.40), 0.25),                   # 어깨
	mell((0, 0.30, 0.37), (0.22, 0.16, 0.20)),      # 엉덩이 (끝이 뾰족한 밤 꼬리 쪽)
]
for (x, y), fb in LEGS:
	el += [mcap((x, y, 0.28), (x * 1.02, y - 0.01, 0.07), 0.068 if fb == 'f' else 0.075)]
body = G.metaball('boar_body', el, resolution=0.014)
body.data.materials.append(SHELL)

head = G.metaball('boar_head', [
	mball((0, -0.38, 0.40), 0.20),
	mell((0, -0.52, 0.33), (0.13, 0.12, 0.11)),
	mell((0.16, -0.33, 0.56), (0.05, 0.035, 0.07)),   # 귀
	mell((-0.16, -0.33, 0.56), (0.05, 0.035, 0.07)),
], resolution=0.010)
head.data.materials.append(SHELL)

# 들창코 (원판)
p, n = G.raycast_place(head, (0, -2.0, 0.32), (0, 1, 0), -0.01)
snout = G.cylinder('boar_snout', 0.085, 0.080, 0.07, (0, 0, 0), seg=28)
snout.rotation_mode = 'QUATERNION'
snout.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(Vector((0, -1, 0.15)).normalized())
snout.location = p + Vector((0, -0.02, 0))
G.bake_transform(snout)
G.bevel(snout, 0.02, 3, angle=50)
snout.data.materials.append(SNOUT)
snout_front_y = min(v.co.y for v in snout.data.vertices)

# 밤송이 가시 갈기: 정수리에서 꼬리까지 등줄기
spikes = []
# 밤송이처럼 짧고 뾰족한 가시를 등줄기 띠에 촘촘히. 원뿔이라 끝이 가늘다
import random
random.seed(3)
for k in range(46):
	u = (k + 0.5) / 46.0
	y = -0.42 + u * 0.76
	side = random.uniform(-0.11, 0.11) * math.sin(u * math.pi) ** 0.5
	tgt = body if y > -0.30 else head
	pt, nn = G.raycast_place(tgt, (side, y, 3.0), (0, 0, -1), -0.012)
	if pt is None:
		continue
	L = (0.06 + 0.04 * math.sin(u * math.pi)) * random.uniform(0.85, 1.15)
	d = (nn + Vector((side * 1.5, 0.35, 0.2))).normalized()
	cone = G.cylinder('boar_spike', 0.026, 0.004, L, (0, 0, L / 2), seg=8)
	cone.rotation_mode = 'QUATERNION'
	cone.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(d)
	cone.location = pt
	G.bake_transform(cone)
	cone.data.materials.append(BURR)
	spikes.append(cone)
burr = G.join(spikes, 'boar_burr')

# 발굽
hooves = []
for (x, y), fb in LEGS:
	h = G.cylinder('boar_hoof', 0.066, 0.072, 0.07, (x * 1.02, y - 0.012, 0.035), seg=16)
	h.data.materials.append(HOOF)
	hooves.append(h)

# 꼬리: 돌돌 말린 고리
tail = G.torus('boar_tail', 0.04, 0.012, (0, 0.47, 0.44), (0, 90, 0), seg=16, ring=6)
tail.data.materials.append(SHELL)

# 엄니
tusks = []
for s in (1, -1):
	t = G.metaball('boar_tusk', [mcap((s * 0.075, snout_front_y + 0.05, 0.27),
		(s * 0.11, snout_front_y + 0.02, 0.36), 0.018)], resolution=0.005)
	t.data.materials.append(TUSK)
	tusks.append(t)

# 얼굴
face = []


def stick(name, mat, tgt, x, z, size, push, dirv=(0, 1, 0)):
	pp, nn = G.raycast_place(tgt, (x, -2.0, z), dirv, push)
	if pp is None:
		return
	ob = G.sphere(name, 1.0, (0, 0, 0), size, seg=16, rings=8)
	ob.rotation_mode = 'QUATERNION'
	ob.rotation_quaternion = Vector((0, -1, 0)).rotation_difference(nn)
	ob.location = pp
	G.bake_transform(ob)
	ob.data.materials.append(mat)
	face.append(ob)


for s in (1, -1):
	stick('boar_eye', EYE, head, 0.095 * s, 0.45, (0.032, 0.012, 0.036), 0.0)
	stick('boar_shine', SHINE, head, 0.088 * s, 0.462, (0.010, 0.004, 0.011), 0.010)
	stick('boar_brow', BROW, head, 0.10 * s, 0.505, (0.040, 0.010, 0.011), 0.002)
	stick('boar_nostril', NOSTRIL, snout, 0.032 * s, 0.33, (0.016, 0.008, 0.022), 0.0)

if STAGE == 'model':
	G.iso_camera(400, 360, 200, 300, scale=6)
	G.render_sprite(os.path.join(G.ROOT, 'preview', 'boar_model'), 'iso')
	cam = bpy.data.objects['GG_Cam']
	cam.rotation_euler = (math.radians(80), 0, math.radians(-35))
	cam.location = Vector((0, 0, 0.35)) - (cam.rotation_euler.to_matrix() @ Vector((0, 0, -1))) * 10
	cam.data.shift_x = 0; cam.data.shift_y = 0; cam.data.ortho_scale = 1.3
	G.render_sprite(os.path.join(G.ROOT, 'preview', 'boar_model'), 'side')
	sys.exit(0)

# ---- 리그 ----
bones = [
	('root', (0, 0, 0), (0, 0.2, 0), None, False, 0, False),
	('body', (0, -0.22, 0.38), (0, 0.38, 0.38), 'root', True, 0, False),
	('head', (0, -0.26, 0.40), (0, -0.60, 0.36), 'body', True, 0, False),
	('ear.L', (0.13, -0.33, 0.50), (0.18, -0.33, 0.62), 'head', True, 0, False),
	('ear.R', (-0.13, -0.33, 0.50), (-0.18, -0.33, 0.62), 'head', True, 0, False),
	('tail', (0, 0.42, 0.42), (0, 0.50, 0.46), 'body', True, 0, False),
]
for (x, y), fb in LEGS:
	sfx = '.L' if x > 0 else '.R'
	bones.append(('leg_' + fb + sfx, (x, y, 0.30), (x * 1.02, y - 0.012, 0.0), 'body', True, 0, False))
arm = G.make_armature('Boar_Rig', bones)

LEGB = ['leg_f.L', 'leg_f.R', 'leg_b.L', 'leg_b.R']
G.skin(body, arm, ['body'] + LEGB, power=8, side_split=True)
G.skin(head, arm, ['head', 'ear.L', 'ear.R', 'body'], power=8, side_split=True)
G.skin(burr, arm, ['body', 'head'], power=6)
G.skin(snout, arm, only='head')
for t in tusks:
	G.skin(t, arm, only='head')
G.skin(tail, arm, only='tail')
for h, ((x, y), fb) in zip(hooves, LEGS):
	G.skin(h, arm, only='leg_' + fb + ('.L' if x > 0 else '.R'))
for f in face:
	G.skin(f, arm, only='head')
boar = G.join([body, head, burr, snout, tail] + tusks + hooves, 'Boar')
fc = G.join(face, 'Boar_Face')
G.bind(boar, arm)
G.bind(fc, arm)

TAU = 2 * math.pi
sin, cos = math.sin, math.cos

IDLE = G.anim_fn(arm, 'boar_idle', 1, 25, 2, {
	'body': lambda t: dict(scale=(1 + 0.02 * sin(t * TAU), 1, 1 + 0.025 * sin(t * TAU))),
	'head': lambda t: dict(rot=(4 * max(0.0, sin(t * TAU * 2)) ** 4, 0, 4 * sin(t * TAU))),
	'ear.L': lambda t: dict(rot=(0, 0, 18 * max(0.0, sin(t * TAU * 2 - 1.0)) ** 6)),
	'ear.R': lambda t: dict(rot=(0, 0, -18 * max(0.0, sin(t * TAU * 2 + 0.5)) ** 6)),
	'tail': lambda t: dict(rot=(0, 0, 25 * sin(t * TAU * 3))),
})


def gallop(ph, amp=34):
	def f(t):
		return dict(rot=(amp * sin(t * TAU + ph), 0, 0))
	return f


RUN = G.anim_fn(arm, 'boar_run', 1, 17, 1, {
	'leg_f.L': gallop(0.0), 'leg_f.R': gallop(0.5),
	'leg_b.L': gallop(math.pi), 'leg_b.R': gallop(math.pi + 0.5),
	'body': lambda t: dict(loc=(0, 0, 0.035 * abs(sin(t * TAU))), rot=(5 * sin(t * TAU), 0, 0)),
	'head': lambda t: dict(rot=(-5 * sin(t * TAU + 0.6), 0, 0)),
	'ear.L': lambda t: dict(rot=(-20 - 10 * sin(t * TAU), 0, 0)),
	'ear.R': lambda t: dict(rot=(-20 - 10 * sin(t * TAU + 0.3), 0, 0)),
	'tail': lambda t: dict(rot=(0, 0, 30 * sin(t * TAU * 2))),
})


def ease(x):
	x = max(0.0, min(1.0, x))
	return x * x * (3 - 2 * x)


# 움츠리기: 엉덩이를 낮추고 앞발로 땅을 두 번 긁는다
WINDUP = G.anim_fn(arm, 'boar_windup', 1, 17, 1, {
	'body': lambda t: dict(loc=(0, -0.05 * ease(t * 3), 0.03 * ease(t * 3)),
		rot=(-9 * ease(t * 3), 0, 0), scale=(1.04, 0.92 + 0.08 * (1 - ease(t * 3)), 1.0)),
	'head': lambda t: dict(rot=(16 * ease(t * 3), 0, 0)),
	'leg_f.L': lambda t: dict(rot=(-35 * max(0.0, sin(t * TAU * 2)), 0, 0)),
	'leg_b.L': lambda t: dict(rot=(14 * ease(t * 3), 0, 0)),
	'leg_b.R': lambda t: dict(rot=(14 * ease(t * 3), 0, 0)),
	'ear.L': lambda t: dict(rot=(-35 * ease(t * 3), 0, 0)),
	'ear.R': lambda t: dict(rot=(-35 * ease(t * 3), 0, 0)),
	'tail': lambda t: dict(rot=(0, 0, 40 * sin(t * TAU * 4))),
})

# 돌진: 고개를 숙이고 몸을 길게. 다리는 빠르게 (루프)
CHARGE = G.anim_fn(arm, 'boar_charge', 1, 9, 1, {
	'body': lambda t: dict(loc=(0, 0, 0.02 * abs(sin(t * TAU))), rot=(4, 0, 0), scale=(0.96, 1.10, 0.96)),
	'head': lambda t: dict(rot=(22, 0, 0)),
	'leg_f.L': gallop(0.0, 46), 'leg_f.R': gallop(0.4, 46),
	'leg_b.L': gallop(math.pi, 46), 'leg_b.R': gallop(math.pi + 0.4, 46),
	'ear.L': lambda t: dict(rot=(-50, 0, 0)),
	'ear.R': lambda t: dict(rot=(-50, 0, 0)),
	'tail': lambda t: dict(rot=(-30, 0, 0)),
})


def caught_body(t):
	e = ease(t)
	sc = 1.0 - 0.75 * e
	return dict(loc=(0, 0.10 * e, 0), rot=(0, 720 * e, 0), scale=(sc, sc, sc))


CAUGHT = G.anim_fn(arm, 'boar_caught', 101, 125, 1, {
	'body': caught_body,
	'leg_f.L': lambda t: dict(scale=(max(0.01, 1 - t * 1.2),) * 3),
	'leg_f.R': lambda t: dict(scale=(max(0.01, 1 - t * 1.2),) * 3),
	'leg_b.L': lambda t: dict(scale=(max(0.01, 1 - t * 1.2),) * 3),
	'leg_b.R': lambda t: dict(scale=(max(0.01, 1 - t * 1.2),) * 3),
})
for a in (IDLE, RUN, WINDUP, CHARGE, CAUGHT):
	a.use_fake_user = True
arm.animation_data.action = None
G.reset_pose(arm)
bpy.context.view_layer.update()

G.smart_uv(boar, margin=0.004)
G.bake_asset_textures(boar, 'boar', res=1024)

for m in (bpy.data.materials['boar_shell'], bpy.data.materials['boar_burr'],
		bpy.data.materials['boar_snout'], EYE, TUSK, HOOF):
	b = m.node_tree.nodes.get('BSDF') or m.node_tree.nodes.get('Principled BSDF')
	b.inputs['Emission Color'].default_value = (1.0, 0.86, 0.55, 1.0)
	for f, v in ((1, 0.0), (101, 0.0), (107, 0.25), (114, 1.2), (119, 3.5), (125, 9.0)):
		b.inputs['Emission Strength'].default_value = v
		b.inputs['Emission Strength'].keyframe_insert('default_value', frame=f)

CANVAS = (200, 190)
PIVOT = (100, 150)
if STAGE == 'anim':
	G.iso_camera(CANVAS[0], CANVAS[1], PIVOT[0], PIVOT[1], scale=2)
	for act, frames in ((RUN, (1, 5, 9, 13)), (WINDUP, (1, 9, 17)), (CHARGE, (1, 5)), (CAUGHT, (101, 113, 122))):
		G.use_action(arm, act)
		for f in frames:
			bpy.context.scene.frame_set(f)
			G.render_sprite(os.path.join(G.ROOT, 'preview', 'boar_anim'), '%s_%03d' % (act.name, f))
	sys.exit(0)

anims = [
	('idle', IDLE, [1, 5, 9, 13, 17, 21], 6, 1),
	('run', RUN, [1, 3, 5, 7, 9, 11, 13, 15], 14, 1),
	('windup', WINDUP, [1, 4, 7, 10, 13, 16], 18, 0),
	('charge', CHARGE, [1, 3, 5, 7], 16, 1),
	('caught', CAUGHT, [101, 104, 107, 110, 113, 116, 119, 122], 10, 0),
]
G.use_action(arm, IDLE)
G.render_character(arm, arm, 'boar', CANVAS, PIVOT, anims)
arm.animation_data.action = IDLE
bpy.context.scene.frame_set(1)
G.save_blend('boar')
print('[boar] done')
