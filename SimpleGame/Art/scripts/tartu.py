# -----------------------------------------------------------------------------
# tartu.py — 타르투 (리노의 송로 강아지, 동행이자 재료 탐지 시스템)
#
# 라고토 로마뇰로(송로 개) 모티브. 크림색 곱슬털에 밤색 얼룩, 처진 귀,
# 송로버섯처럼 울퉁불퉁한 까만 코, 테라코타 목줄에 작은 송로 모양 방울.
# 머리가 몸만큼 큰 미니어처 비례. 높이 약 0.48 BU.
# 스프라이트는 3배로 굽는다 — 엔진이 tartu_size 단계에 따라 키운다.
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

# ---- 머티리얼 ----
FUR = G.proc_mat('tartu_fur', lin((0.95, 0.90, 0.80)), lin((0.62, 0.40, 0.24)),
	var_scale=2.6, var_amt=1.0, var_edge=(0.53, 0.57), rough=0.8, rough_var=0.08,
	macro=('curls', 62.0), macro_amt=0.85, cavity=0.12, normal_strength=0.8,
	micro=('noise', 220.0, 3.0), bump_strength=0.15, sheen=0.6, height_scale=0.008)
PATCH = G.proc_mat('tartu_patch', lin((0.62, 0.40, 0.24)), lin((0.52, 0.32, 0.18)),
	var_scale=5, var_amt=0.5, rough=0.8,
	macro=('curls', 62.0), macro_amt=0.85, cavity=0.12, normal_strength=0.8,
	micro=('noise', 220.0, 3.0), bump_strength=0.15, sheen=0.6, height_scale=0.008)
NOSE = G.proc_mat('tartu_nose', lin((0.10, 0.08, 0.07)), lin((0.18, 0.13, 0.10)),
	var_scale=40, var_amt=0.5, rough=0.35, rough_var=0.1,
	macro=('pebbles', 120.0), macro_amt=1.0, cavity=0.3, normal_strength=1.0,
	micro=('noise', 300.0), bump_strength=0.1, coat=0.4, height_scale=0.004)
COLLAR = G.proc_mat('tartu_collar', lin((0.80, 0.38, 0.25)), lin((0.70, 0.30, 0.20)),
	var_scale=6, var_amt=0.3, rough=0.55, macro=('noise', 20.0, 2.0), macro_amt=0.4,
	micro=('cells', 300.0, 3.0), bump_strength=0.1, coat=0.1)
BRASS = G.proc_mat('tartu_brass', lin((0.86, 0.66, 0.30)), lin((0.75, 0.55, 0.22)),
	var_scale=12, var_amt=0.4, rough=0.3, metallic=1.0,
	macro=('pebbles', 90.0), macro_amt=0.8, normal_strength=0.6, height_scale=0.003,
	micro=('noise', 300.0), bump_strength=0.05)
EYE = G.simple_mat('tartu_eye', lin((0.10, 0.06, 0.04)), rough=0.12, coat=1.0)
SHINE = G.simple_mat('tartu_shine', (1, 1, 1), rough=0.2, emission=((1, 1, 1), 2.5))
TONGUE = G.simple_mat('tartu_tongue', lin((0.92, 0.48, 0.48)), rough=0.4, sss=0.2)

# ---- 메쉬 ----
body = G.metaball('tartu_body', [
	mell((0, 0.03, 0.215), (0.112, 0.165, 0.098)),
	mball((0, -0.085, 0.235), 0.105),
	mball((0, 0.13, 0.225), 0.09),
	# 다리
	mcap((0.062, -0.095, 0.17), (0.062, -0.11, 0.035), 0.040),
	mcap((-0.062, -0.095, 0.17), (-0.062, -0.11, 0.035), 0.040),
	mcap((0.066, 0.13, 0.17), (0.066, 0.14, 0.035), 0.042),
	mcap((-0.066, 0.13, 0.17), (-0.066, 0.14, 0.035), 0.042),
	mell((0.062, -0.125, 0.026), (0.040, 0.048, 0.026)),
	mell((-0.062, -0.125, 0.026), (0.040, 0.048, 0.026)),
	mell((0.066, 0.125, 0.026), (0.040, 0.048, 0.026)),
	mell((-0.066, 0.125, 0.026), (0.040, 0.048, 0.026)),
	# 목
	mcap((0, -0.10, 0.26), (0, -0.15, 0.32), 0.075),
], resolution=0.009)

head = G.metaball('tartu_head', [
	mball((0, -0.16, 0.375), 0.128),
	mell((0, -0.10, 0.39), (0.12, 0.11, 0.12)),
	mell((0, -0.275, 0.335), (0.068, 0.07, 0.055)),
	mball((0.05, -0.24, 0.43), 0.045),     # 눈썹 위 곱슬 덩어리
	mball((-0.05, -0.24, 0.43), 0.045),
	mball((0, -0.17, 0.49), 0.06),          # 정수리 곱슬
], resolution=0.008)

ears = G.metaball('tartu_ears', [
	mell((0.125, -0.15, 0.36), (0.035, 0.062, 0.088)),
	mell((-0.125, -0.15, 0.36), (0.035, 0.062, 0.088)),
	mball((0.13, -0.15, 0.285), 0.04),
	mball((-0.13, -0.15, 0.285), 0.04),
], resolution=0.008)

tail = G.metaball('tartu_tail', [
	mcap((0, 0.17, 0.25), (0, 0.215, 0.315), 0.032),
	mcap((0, 0.215, 0.315), (0, 0.20, 0.38), 0.03),
	mball((0, 0.19, 0.39), 0.038),
], resolution=0.008)

p, n = G.raycast_place(head, (0, -1, 0.345), (0, 1, 0), -0.004)
nose = G.sphere('tartu_nose', 1.0, (0, 0, 0), (0.030, 0.024, 0.024), seg=24, rings=12)
nose.location = p
G.bake_transform(nose)

collar = G.torus('tartu_collar', 0.086, 0.016, (0, -0.128, 0.288), (38, 0, 0),
	scale=(1.0, 1.0, 1.0), seg=28, ring=8)
bell = G.sphere('tartu_bell', 0.022, (0, -0.19, 0.235), (1.0, 0.9, 1.1), seg=16, rings=10)

# 얼굴 부품
face = []


def stick(name, mat, x, z, size, push):
	p, n = G.raycast_place(head, (x, -1.0, z), (0, 1, 0), push)
	ob = G.sphere(name, 1.0, (0, 0, 0), size, seg=18, rings=9)
	ob.rotation_mode = 'QUATERNION'
	ob.rotation_quaternion = Vector((0, -1, 0)).rotation_difference(n)
	ob.location = p
	G.bake_transform(ob)
	ob.data.materials.append(mat)
	face.append(ob)


for s in (1, -1):
	stick('tartu_eye', EYE, 0.055 * s, 0.40, (0.024, 0.012, 0.028), 0.0)
	stick('tartu_shine', SHINE, 0.050 * s, 0.412, (0.008, 0.004, 0.009), 0.011)
stick('tartu_tongue', TONGUE, 0.0, 0.295, (0.020, 0.010, 0.014), -0.002)

# 머티리얼: 몸 = 크림 + 엉덩이·등 얼룩, 귀 = 밤색
# 얼룩은 털 머티리얼의 저주파 노이즈가 만든다 (면 단위로 나누면 경계가 톱니가 된다)
body.data.materials.append(FUR)
head.data.materials.append(FUR)
ears.data.materials.append(PATCH)
tail.data.materials.append(FUR)
nose.data.materials.append(NOSE)
collar.data.materials.append(COLLAR)
bell.data.materials.append(BRASS)

if STAGE == 'model':
	G.iso_camera(400, 400, 200, 360, scale=11)
	G.render_sprite(os.path.join(G.ROOT, 'preview', 'tartu_model'), 'iso')
	cam = bpy.data.objects['GG_Cam']
	cam.rotation_euler = (math.radians(90), 0, math.radians(-30))
	cam.location = Vector((0, 0, 0.25)) - (cam.rotation_euler.to_matrix() @ Vector((0, 0, -1))) * 10
	cam.data.shift_x = 0; cam.data.shift_y = 0; cam.data.ortho_scale = 0.75
	G.render_sprite(os.path.join(G.ROOT, 'preview', 'tartu_model'), 'side')
	sys.exit(0)

# ---- 리그 ----
bones = [
	('root', (0, 0, 0), (0, 0.15, 0), None, False, 0, False),
	('body', (0, -0.09, 0.22), (0, 0.13, 0.22), 'root', True, 0, False),
	('neck', (0, -0.10, 0.27), (0, -0.15, 0.33), 'body', True, 0, False),
	('head', (0, -0.15, 0.33), (0, -0.17, 0.48), 'neck', True, 0, True),
	('ear.L', (0.11, -0.15, 0.43), (0.135, -0.15, 0.27), 'head', True, 0, False),
	('ear.R', (-0.11, -0.15, 0.43), (-0.135, -0.15, 0.27), 'head', True, 0, False),
	('tail1', (0, 0.16, 0.245), (0, 0.215, 0.315), 'body', True, 0, False),
	('tail2', (0, 0.215, 0.315), (0, 0.19, 0.40), 'tail1', True, 0, True),
]
for nm, x, y in (('leg_f', 0.062, -0.095), ('leg_b', 0.066, 0.13)):
	for s, sfx in ((1, '.L'), (-1, '.R')):
		bones += [
			(nm + '_up' + sfx, (x * s, y, 0.19), (x * s, y - 0.008, 0.10), 'body', True, 0, False),
			(nm + '_lo' + sfx, (x * s, y - 0.008, 0.10), (x * s, y - 0.02, 0.012),
				nm + '_up' + sfx, True, 0, True),
		]
arm = G.make_armature('Tartu_Rig', bones)

legs = ['leg_f_up.L', 'leg_f_up.R', 'leg_b_up.L', 'leg_b_up.R',
	'leg_f_lo.L', 'leg_f_lo.R', 'leg_b_lo.L', 'leg_b_lo.R']
G.skin(body, arm, ['body', 'neck'] + legs, power=8, side_split=True)
G.skin(head, arm, ['neck', 'head'], power=10)
G.skin(ears, arm, ['ear.L', 'ear.R', 'head'], power=8, side_split=True)
G.skin(tail, arm, ['tail1', 'tail2', 'body'], power=8)
G.skin(nose, arm, only='head')
G.skin(collar, arm, ['neck', 'body'], power=6)
G.skin(bell, arm, only='neck')
for f in face:
	G.skin(f, arm, only='head')

dog = G.join([body, head, ears, tail, nose, collar, bell], 'Tartu')
fc = G.join(face, 'Tartu_Face')
G.bind(dog, arm)
G.bind(fc, arm)

# ---- 애니메이션 ----
TAU = 2 * math.pi
sin, cos = math.sin, math.cos

IDLE = G.anim_fn(arm, 'tartu_idle', 1, 25, 2, {
	'body': lambda t: dict(scale=(1 + 0.02 * sin(t * TAU), 1, 1 + 0.02 * sin(t * TAU))),
	'head': lambda t: dict(rot=(2 * sin(t * TAU), 0, 7 * sin(t * TAU) ** 3)),
	'ear.L': lambda t: dict(rot=(4 * sin(t * TAU + 1), 0, 0)),
	'ear.R': lambda t: dict(rot=(4 * sin(t * TAU + 1.5), 0, 0)),
	'tail1': lambda t: dict(rot=(0, 0, 28 * sin(t * TAU * 3))),
	'tail2': lambda t: dict(rot=(0, 0, 22 * sin(t * TAU * 3 - 0.8))),
})


def leg(ph, front):
	def f(t):
		a = t * TAU + ph
		return dict(rot=(30 * sin(a), 0, 0))
	return f


def leg_lo(ph, front):
	def f(t):
		a = t * TAU + ph
		# 앞으로 휘두를 때(sin 감소) 발목을 접는다
		k = max(0.0, cos(a))
		return dict(rot=((35 if front else -30) * k, 0, 0))
	return f


TROT = G.anim_fn(arm, 'tartu_walk', 1, 13, 1, {
	'leg_f_up.L': leg(0, True), 'leg_b_up.R': leg(0, False),
	'leg_f_up.R': leg(math.pi, True), 'leg_b_up.L': leg(math.pi, False),
	'leg_f_lo.L': leg_lo(0, True), 'leg_b_lo.R': leg_lo(0, False),
	'leg_f_lo.R': leg_lo(math.pi, True), 'leg_b_lo.L': leg_lo(math.pi, False),
	'body': lambda t: dict(loc=(0, 0, 0.012 * abs(sin(t * TAU))), rot=(2 * sin(2 * t * TAU), 0, 0)),
	'head': lambda t: dict(rot=(-4 * sin(2 * t * TAU), 0, 0)),
	'ear.L': lambda t: dict(rot=(-12 - 10 * sin(2 * t * TAU), 0, 0)),
	'ear.R': lambda t: dict(rot=(-12 - 10 * sin(2 * t * TAU + 0.4), 0, 0)),
	'tail1': lambda t: dict(rot=(-8, 0, 20 * sin(t * TAU * 2))),
	'tail2': lambda t: dict(rot=(0, 0, 18 * sin(t * TAU * 2 - 0.8))),
})


def sniff_amt(t):
	# 0..0.2 고개 숙이기, 0.2..0.8 킁킁, 0.8..1 다시 들기 (루프)
	x = min(1.0, t / 0.2) if t < 0.5 else min(1.0, (1 - t) / 0.2)
	return x * x * (3 - 2 * x)


SNIFF = G.anim_fn(arm, 'tartu_sniff', 1, 25, 1, {
	'neck': lambda t: dict(rot=(55 * sniff_amt(t), 0, 0)),
	'head': lambda t: dict(rot=(30 * sniff_amt(t) + 4 * sniff_amt(t) * sin(t * TAU * 6), 0,
		6 * sniff_amt(t) * sin(t * TAU * 2))),
	'body': lambda t: dict(rot=(-12 * sniff_amt(t), 0, 0)),
	'leg_f_up.L': lambda t: dict(rot=(-10 * sniff_amt(t), 0, 0)),
	'leg_f_up.R': lambda t: dict(rot=(-10 * sniff_amt(t), 0, 0)),
	'leg_b_up.L': lambda t: dict(rot=(12 * sniff_amt(t), 0, 0)),
	'leg_b_up.R': lambda t: dict(rot=(12 * sniff_amt(t), 0, 0)),
	'tail1': lambda t: dict(rot=(-10 * sniff_amt(t), 0, 35 * sin(t * TAU * 4))),
	'tail2': lambda t: dict(rot=(0, 0, 25 * sin(t * TAU * 4 - 0.8))),
	'ear.L': lambda t: dict(rot=(-20 * sniff_amt(t), 0, 0)),
	'ear.R': lambda t: dict(rot=(-20 * sniff_amt(t), 0, 0)),
})

arm.animation_data.action = None
G.reset_pose(arm)
bpy.context.view_layer.update()

G.smart_uv(dog, margin=0.004)
G.bake_asset_textures(dog, 'tartu', res=1024)

if STAGE == 'anim':
	G.iso_camera(150, 130, 75, 110, scale=3)
	for act, frames in ((TROT, (1, 4, 7, 10)), (SNIFF, (1, 7, 13)), (IDLE, (1, 13))):
		G.use_action(arm, act)
		for f in frames:
			bpy.context.scene.frame_set(f)
			G.render_sprite(os.path.join(G.ROOT, 'preview', 'tartu_anim'), '%s_%02d' % (act.name, f))
	sys.exit(0)

anims = [
	('idle', IDLE, [1, 5, 9, 13, 17, 21], 6, 1),
	('walk', TROT, [1, 3, 5, 7, 9, 11], 12, 1),
	('sniff', SNIFF, [1, 5, 9, 13, 17, 21], 8, 1),
]
G.use_action(arm, IDLE)
G.render_character(arm, arm, 'tartu', (150, 132), (75, 112), anims, scale=3)
arm.animation_data.action = IDLE
G.save_blend('tartu')
print('[tartu] done')
