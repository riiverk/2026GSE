# -----------------------------------------------------------------------------
# lakedeer.py — 호수사슴 (포획 2 / 미식 ★★, 튜토리얼의 "큰 놈")
#
# 따뜻한 라벤더 털, 레몬크림 배, 등에 레몬크림으로 은은히 빛나는 반점,
# 갈대 순처럼 끝이 동그란 상아빛 뿔. 기준 팔레트(프로방스)만 쓴다 — 차가운
# 물빛·수정빛은 북부 숲 전용이라 여기서는 쓰지 않는다 (CLAUDE.md 5.2).
# 현실 사슴을 닮지 않도록 몸통은 통통한 콩 모양, 목은 거의 없고 머리가 크며
# 다리는 짧고 굵다 (봉제 인형 비례). 무광 설정.
# 동작: idle 8 (숨쉬기, 귀 털기, 고개 들었다 숙이기, 꼬리 까딱). 정지해 있는 개체라
# 이동 동작은 없다.
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

# 배는 레몬크림, 등은 라벤더 (오브젝트 높이로 나눈다 — 노이즈로 섞으면 얼룩져 보인다)
FUR = G.proc_mat('deer_fur', lin((0.97, 0.91, 0.74)), lin((0.93, 0.86, 0.70)),
	var_scale=3.0, var_amt=0.5, rough=0.8, top=(0.50, 0.68, lin((0.66, 0.52, 0.72))),
	macro=('wave', 26.0, 6.0, 'Z'), macro_amt=0.6, cavity=0.2, normal_strength=0.6,
	micro=('noise', 200.0, 3.0), bump_strength=0.12, sheen=0.5,
	grad_z=(0.25, 1.10, 1.25, 0.82), height_scale=0.008)
ANTLER = G.proc_mat('deer_antler', lin((0.96, 0.90, 0.76)), lin((0.88, 0.78, 0.60)),
	var_scale=8, var_amt=0.6, rough=0.4, sss=0.4, sss_radius=(0.8, 0.6, 0.4),
	emission=(lin((1.0, 0.92, 0.66)), 0.35), macro=('wave', 40.0, 2.0, 'Z'), macro_amt=0.5,
	normal_strength=0.5, height_scale=0.004)
HOOF = G.simple_mat('deer_hoof', lin((0.36, 0.26, 0.24)), rough=0.5)
SPOT = G.simple_mat('deer_spot', lin((1.0, 0.94, 0.72)), rough=0.6,
	emission=(lin((1.0, 0.90, 0.62)), 0.9))
EYE = G.simple_mat('deer_eye', lin((0.10, 0.07, 0.12)), rough=0.12, coat=1.0)
SHINE = G.simple_mat('deer_shine', (1, 1, 1), rough=0.2, emission=((1, 1, 1), 2.5))
NOSE = G.simple_mat('deer_nose', lin((0.62, 0.36, 0.40)), rough=0.4)

# ---- 몸 (정면 = -Y) ----
LEGS = [((0.16, -0.30), 'f'), ((-0.16, -0.30), 'f'), ((0.17, 0.32), 'b'), ((-0.17, 0.32), 'b')]
el = [
	mell((0, 0.02, 0.70), (0.33, 0.50, 0.30)),
	mball((0, -0.28, 0.76), 0.30),
	mball((0, 0.32, 0.74), 0.29),
	mcap((0, -0.36, 0.86), (0, -0.46, 1.06), 0.20),      # 짧고 굵은 목
]
for (x, y), fb in LEGS:
	el += [mcap((x, y, 0.62), (x * 0.95, y - 0.02, 0.34), 0.10 if fb == 'f' else 0.11),
		mcap((x * 0.95, y - 0.02, 0.34), (x * 0.95, y - 0.03, 0.10), 0.072)]
el += [mball((0, 0.56, 0.84), 0.08)]                        # 솜뭉치 꼬리
body = G.metaball('deer_body', el, resolution=0.02)
body.data.materials.append(FUR)

head = G.metaball('deer_head', [
	mball((0, -0.54, 1.22), 0.31),
	mell((0, -0.76, 1.12), (0.15, 0.14, 0.12)),          # 동그란 주둥이
	mell((0.30, -0.48, 1.34), (0.06, 0.14, 0.07)),       # 옆으로 늘어진 귀
	mell((-0.30, -0.48, 1.34), (0.06, 0.14, 0.07)),
], resolution=0.012)
head.data.materials.append(FUR)

hooves = []
for (x, y), fb in LEGS:
	h = G.cylinder('deer_hoof', 0.076, 0.082, 0.10, (x * 0.95, y - 0.03, 0.05), seg=16)
	h.data.materials.append(HOOF)
	hooves.append(h)

# 뿔: 갈대처럼 위로 갈라지는 가지 (양쪽)
ant_el = []
for s in (1, -1):
	b0 = (s * 0.11, -0.52, 1.44)
	b1 = (s * 0.17, -0.50, 1.62)
	b2 = (s * 0.24, -0.46, 1.76)
	ant_el += [mcap(b0, b1, 0.036), mcap(b1, b2, 0.030), mball(b2, 0.050),
		mcap(b1, (s * 0.08, -0.58, 1.74), 0.026), mball((s * 0.08, -0.58, 1.74), 0.040)]
antlers = G.metaball('deer_antlers', ant_el, resolution=0.008)
antlers.data.materials.append(ANTLER)

# 등의 빛 반점
spots = []
for (x, y) in ((0.10, -0.18), (-0.12, -0.05), (0.14, 0.12), (-0.06, 0.26), (0.05, 0.40),
		(-0.16, 0.34), (0.0, 0.02), (0.18, -0.02)):
	p, n = G.raycast_place(body, (x * 1.1, y, 3.0), (0, 0, -1), 0.002)
	if p is None:
		continue
	sp = G.sphere('deer_spot', 1.0, (0, 0, 0), (0.042, 0.042, 0.009), seg=12, rings=6)
	sp.rotation_mode = 'QUATERNION'
	sp.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(n)
	sp.location = p
	G.bake_transform(sp)
	sp.data.materials.append(SPOT)
	spots.append(sp)

face = []
for s in (1, -1):
	for (nm, mat, dx, dz, size, push) in (('eye', EYE, 0.13, 1.25, (0.050, 0.018, 0.060), 0.0),
			('shine', SHINE, 0.12, 1.28, (0.015, 0.006, 0.017), 0.016)):
		p, n = G.raycast_place(head, (s * dx, -2.0, dz), (0, 1, 0), push)
		if p is None:
			continue
		ob = G.sphere('deer_' + nm, 1.0, (0, 0, 0), size, seg=16, rings=8)
		ob.rotation_mode = 'QUATERNION'
		ob.rotation_quaternion = Vector((0, -1, 0)).rotation_difference(n)
		ob.location = p
		G.bake_transform(ob)
		ob.data.materials.append(mat)
		face.append(ob)
p, n = G.raycast_place(head, (0, -2.0, 1.14), (0, 1, 0), -0.004)
nose = G.sphere('deer_nose', 1.0, p, (0.055, 0.032, 0.038), seg=16, rings=8)
nose.data.materials.append(NOSE)
face.append(nose)

if STAGE == 'model':
	G.iso_camera(420, 460, 210, 400, scale=3)
	G.render_sprite(os.path.join(G.ROOT, 'preview'), 'deer_model')
	sys.exit(0)

# ---- 리그 ----
bones = [
	('root', (0, 0, 0), (0, 0.3, 0), None, False, 0, False),
	('body', (0, -0.36, 0.72), (0, 0.46, 0.72), 'root', True, 0, False),
	('neck', (0, -0.36, 0.86), (0, -0.48, 1.08), 'body', True, 0, False),
	('head', (0, -0.48, 1.08), (0, -0.54, 1.48), 'neck', True, 0, True),
	('ear.L', (0.22, -0.48, 1.32), (0.36, -0.48, 1.34), 'head', True, 0, False),
	('ear.R', (-0.22, -0.48, 1.32), (-0.36, -0.48, 1.34), 'head', True, 0, False),
	('tail', (0, 0.50, 0.80), (0, 0.62, 0.88), 'body', True, 0, False),
]
legs = []
for k, ((x, y), fb) in enumerate(LEGS):
	sfx = ('.L' if x > 0 else '.R')
	nm = 'leg_' + fb
	bones += [(nm + '_up' + sfx, (x, y, 0.62), (x * 0.95, y - 0.02, 0.34), 'body', True, 0, False),
		(nm + '_lo' + sfx, (x * 0.95, y - 0.02, 0.34), (x * 0.95, y - 0.03, 0.02), nm + '_up' + sfx,
		True, 0, True)]
	legs += [nm + '_up' + sfx, nm + '_lo' + sfx]
arm = G.make_armature('LakeDeer_Rig', bones)
G.skin(body, arm, ['body', 'neck', 'tail'] + legs, power=8, side_split=True)
G.skin(head, arm, ['head', 'ear.L', 'ear.R'], power=10, side_split=True)
for h, ((x, y), fb) in zip(hooves, LEGS):
	G.skin(h, arm, only='leg_' + fb + '_lo' + ('.L' if x > 0 else '.R'))
G.skin(antlers, arm, only='head')
for o in spots:
	G.skin(o, arm, only='body')
for o in face:
	G.skin(o, arm, only='head')
deer = G.join([body, head, antlers] + hooves + spots, 'LakeDeer')
fc = G.join(face, 'LakeDeer_Face')
G.bind(deer, arm)
G.bind(fc, arm)

TAU = 2 * math.pi
sin = math.sin
IDLE = G.anim_fn(arm, 'lakedeer_idle', 1, 49, 1, {
	'body': lambda t: dict(scale=(1 + 0.015 * sin(t * TAU * 2), 1, 1 + 0.015 * sin(t * TAU * 2))),
	'neck': lambda t: dict(rot=(6 * max(0.0, sin(t * TAU)) ** 2, 0, 0)),
	'head': lambda t: dict(rot=(-3 * sin(t * TAU), 0, 6 * sin(t * TAU + 1.0))),
	'ear.L': lambda t: dict(rot=(0, 0, 22 * max(0.0, sin(t * TAU * 3 - 1.0)) ** 8)),
	'ear.R': lambda t: dict(rot=(0, 0, -22 * max(0.0, sin(t * TAU * 3 + 1.5)) ** 8)),
	'tail': lambda t: dict(rot=(0, 0, 25 * sin(t * TAU * 4))),
})
arm.animation_data.action = None
G.reset_pose(arm)
bpy.context.view_layer.update()

G.smart_uv(deer, margin=0.003)
G.bake_asset_textures(deer, 'lakedeer', res=1024)

if STAGE == 'anim':
	G.iso_camera(300, 320, 150, 270, scale=2)
	G.use_action(arm, IDLE)
	for f in (1, 13, 25):
		bpy.context.scene.frame_set(f)
		G.render_sprite(os.path.join(G.ROOT, 'preview', 'deer'), 'idle_%02d' % f)
	sys.exit(0)

anims = [('idle', IDLE, [1, 7, 13, 19, 25, 31, 37, 43], 6, 1)]
G.use_action(arm, IDLE)
G.render_character(arm, arm, 'lakedeer', (300, 330), (150, 270), anims)
arm.animation_data.action = IDLE
G.save_blend('lakedeer')
print('[lakedeer] done')
