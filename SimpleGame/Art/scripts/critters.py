# -----------------------------------------------------------------------------
# critters.py — 튜토리얼 잡몹 3종 (토마토마와 같은 리그·동작 체계)
#
#   python3 critters.py [potato|bean|mush ...]     (기본: 전부)
#
# 알감자 (포획 1)  울퉁불퉁한 감자 덩이 + 정수리 새싹. 흙빛, 무광.
# 방울콩 (포획 2)  작고 통통한 완두 알 + 잎 두 장 + 말린 덩굴손.
# 버섯당 (포획 2)  크림색 기둥 몸통 + 장밋빛 갓(크림 점박이). 얼굴은 기둥에.
#
# 리그: root / body / top(새싹·잎·갓) / stem / foot.L / foot.R — tomatoma.py 와 같다.
# 동작: idle 6, hop 8, caught 8 (빙글 돌며 작아지고 빛이 됨). 무광 설정(MATTE=1).
# -----------------------------------------------------------------------------
import sys, os, math, random
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy
from mathutils import Vector
import ggcommon as G
from ggcommon import mball, mell, mcap, lin

STAGE = os.environ.get('GG_STAGE', 'all')
WANT = sys.argv[1:] or ['potato', 'bean', 'mush']
TAU = 2 * math.pi
sin, cos = math.sin, math.cos


def lumpy(ob, amp, freq, seed):
	G.deform(ob, lambda p: p * (1.0 + amp * sin(p.x * freq + seed) * sin(p.y * freq * 1.3 + seed) *
		sin(p.z * freq * 0.8 + seed * 2)))


def face_on(target, cz, eye_dx, eye_size, mats, extra_z=0.0):
	EYE, SHINE, MOUTH, BLUSH = mats
	face = []

	def stick(name, mat, x, z, size, push):
		p, n = G.raycast_place(target, (x, -1.0, z), (0, 1, 0), push)
		if p is None:
			return
		ob = G.sphere(name, 1.0, (0, 0, 0), size, seg=18, rings=9)
		ob.rotation_mode = 'QUATERNION'
		ob.rotation_quaternion = Vector((0, -1, 0)).rotation_difference(n)
		ob.location = p
		G.bake_transform(ob)
		ob.data.materials.append(mat)
		face.append(ob)

	ew, eh = eye_size
	for s in (1, -1):
		stick('eye', EYE, eye_dx * s, cz + 0.03, (ew, 0.014, eh), 0.0)
		stick('shine', SHINE, eye_dx * s - 0.008, cz + 0.03 + eh * 0.45, (ew * 0.32, 0.005, ew * 0.38), 0.012)
		stick('blush', BLUSH, (eye_dx + 0.06) * s, cz - 0.02, (0.026, 0.006, 0.015), 0.0)
	stick('mouth', MOUTH, 0.0, cz - 0.032, (0.018, 0.008, 0.009), 0.0)
	return face


# =============================================================================
# 종별 몸
# =============================================================================

def build_potato(M):
	SK = G.proc_mat('potato_skin', lin((0.80, 0.65, 0.43)), lin((0.62, 0.46, 0.28)),
		var_scale=5, var_amt=0.6, rough=0.8, rough_var=0.08,
		macro=('pebbles', 18.0), macro_amt=0.5, cavity=0.35, normal_strength=0.7,
		micro=('noise', 180.0, 4.0), bump_strength=0.2, grad_z=(0.05, 0.40, 0.70, 1.05),
		height_scale=0.006)
	LF = M['leaf']
	CZ = 0.235
	body = G.sphere('potato_body', 1.0, seg=48, rings=28)
	G.deform(body, lambda p: Vector((p.x * 0.255, p.y * 0.215, p.z * 0.175 + CZ)))
	lumpy(body, 0.07, 9.0, 1.3)
	# 감자 눈: 몇 군데 콕 들어간 자리
	for (x, y, z) in ((0.16, -0.10, 0.30), (-0.18, 0.02, 0.26), (0.05, 0.14, 0.36)):
		c = Vector((x, y, z))
		G.deform(body, lambda p, c=c: p - (p - Vector((0, 0, CZ))).normalized() * 0.018 *
			math.exp(-((p - c).length / 0.03) ** 2))
	body.data.materials.append(SK)
	top_z = CZ + 0.165
	sprout = G.cylinder('potato_sprout', 0.012, 0.009, 0.08, (0.0, 0.02, top_z + 0.03), (12, 0, 0), seg=8)
	sprout.data.materials.append(LF)
	leaves = []
	for s in (1, -1):
		lf = G.sphere('potato_leaf', 1.0, (s * 0.04, 0.0, top_z + 0.075), (0.045, 0.022, 0.008),
			(0, -s * 28, s * 20), seg=12, rings=6)
		lf.data.materials.append(LF)
		leaves.append(lf)
	return dict(body=body, top=leaves, stem=[sprout], CZ=CZ, top_z=top_z,
		feet_mat=SK, feet_r=0.05, eye_dx=0.07, eye=(0.030, 0.040), extra=[])


def build_bean(M):
	SK = G.proc_mat('bean_skin', lin((0.54, 0.74, 0.36)), lin((0.42, 0.62, 0.28)),
		var_scale=4, var_amt=0.5, rough=0.45, rough_var=0.08,
		macro=('noise', 10.0, 2.0), macro_amt=0.4, cavity=0.1, normal_strength=0.3,
		micro=('noise', 220.0, 3.0), bump_strength=0.06, grad_z=(0.05, 0.38, 0.70, 1.08),
		sss=0.3, sss_radius=(0.3, 0.6, 0.2), coat=0.3, height_scale=0.004)
	LF = M['leaf']
	CZ = 0.20
	body = G.sphere('bean_body', 0.19, (0, 0, CZ), (1.05, 1.0, 0.95), seg=48, rings=28)
	body.data.materials.append(SK)
	top_z = CZ + 0.18
	leaves = []
	for s in (1, -1):
		lf = G.sphere('bean_leaf', 1.0, (s * 0.06, 0.02, top_z + 0.01), (0.075, 0.035, 0.010),
			(0, -s * 32, s * 15), seg=14, rings=7)
		lf.data.materials.append(LF)
		leaves.append(lf)
	# 말린 덩굴손: 위로 올라가며 도는 나선을 캡슐 사슬로
	pts = []
	for k in range(14):
		t = k / 13.0
		a = t * 3.2 * math.pi
		r = 0.035 * (1.0 - 0.6 * t)
		pts.append((r * cos(a) - 0.01, r * sin(a) + 0.01, top_z + 0.02 + t * 0.10))
	el = [mcap(pts[i], pts[i + 1], 0.007) for i in range(len(pts) - 1)]
	tendril = G.metaball('bean_tendril', el, resolution=0.004)
	tendril.data.materials.append(LF)
	return dict(body=body, top=leaves, stem=[tendril], CZ=CZ, top_z=top_z,
		feet_mat=LF, feet_r=0.04, eye_dx=0.062, eye=(0.030, 0.042), extra=[])


def build_mush(M):
	STEM = G.proc_mat('mush_stem', lin((0.92, 0.86, 0.74)), lin((0.84, 0.76, 0.62)),
		var_scale=6, var_amt=0.4, rough=0.75, macro=('wave', 30.0, 2.0, 'Z'), macro_amt=0.5,
		cavity=0.15, normal_strength=0.5, micro=('noise', 200.0), bump_strength=0.1,
		sss=0.15, sss_radius=(0.4, 0.35, 0.25), height_scale=0.004)
	CAP = G.proc_mat('mush_cap', lin((0.76, 0.42, 0.40)), lin((0.62, 0.30, 0.30)),
		var_scale=4, var_amt=0.6, rough=0.55, macro=('noise', 8.0, 3.0), macro_amt=0.5,
		cavity=0.2, normal_strength=0.6, micro=('noise', 160.0), bump_strength=0.12,
		grad_z=(0.35, 0.62, 0.80, 1.08), height_scale=0.006)
	SPOT = G.simple_mat('mush_spot', lin((0.96, 0.92, 0.82)), rough=0.7)
	GILL = G.proc_mat('mush_gill', lin((0.82, 0.70, 0.58)), lin((0.70, 0.58, 0.46)),
		var_scale=8, var_amt=0.4, rough=0.8, macro=('rings', 60.0, 0.0, 'Z'), macro_amt=0.8,
		cavity=0.3, normal_strength=0.8, height_scale=0.004)
	CZ = 0.19
	stem = G.metaball('mush_body', [mell((0, 0, 0.19), (0.125, 0.115, 0.165)),
		mell((0, 0, 0.07), (0.145, 0.135, 0.07))], resolution=0.008)
	stem.data.materials.append(STEM)
	top_z = 0.37
	cap = G.sphere('mush_cap', 1.0, seg=48, rings=24)
	G.deform(cap, lambda p: Vector((p.x * 0.235, p.y * 0.235,
		top_z + max(p.z, -0.25) * 0.15 - (0.02 if p.z < -0.2 else 0.0))))
	cap.data.materials.append(CAP)
	gill = G.cylinder('mush_gill', 0.22, 0.09, 0.025, (0, 0, top_z - 0.045), seg=40)
	gill.data.materials.append(GILL)
	spots = []
	for (a, el, r) in ((0.3, 0.55, 0.035), (2.0, 0.40, 0.03), (3.4, 0.70, 0.028), (4.6, 0.45, 0.034),
			(5.5, 0.80, 0.022), (1.2, 0.85, 0.024), (-0.6, 0.30, 0.026)):
		d = Vector((cos(a) * cos(el), sin(a) * cos(el), sin(el)))
		p, n = G.raycast_place(cap, tuple(Vector((0, 0, top_z)) + d * 1.0), tuple(-d), 0.002)
		if p is None:
			continue
		sp = G.sphere('mush_spot', 1.0, (0, 0, 0), (r, r, r * 0.25), seg=12, rings=6)
		sp.rotation_mode = 'QUATERNION'
		sp.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(n)
		sp.location = p
		G.bake_transform(sp)
		sp.data.materials.append(SPOT)
		spots.append(sp)
	return dict(body=stem, top=[cap, gill] + spots, stem=[], CZ=CZ, top_z=top_z,
		feet_mat=STEM, feet_r=0.045, eye_dx=0.055, eye=(0.026, 0.036), extra=[],
		face_target=stem)


BUILD = dict(potato=build_potato, bean=build_bean, mush=build_mush)


# =============================================================================
# 공통: 발, 얼굴, 리그, 동작, 베이크, 렌더
# =============================================================================

def make(sp):
	G.reset(samples=40)
	G.light_rig()
	G.MATTE = 1.0
	import env as E
	M = E.mats_common(sp)
	EYE = G.simple_mat(sp + '_eye', lin((0.12, 0.07, 0.06)), rough=0.12, coat=1.0)
	SHINE = G.simple_mat(sp + '_shine', (1, 1, 1), rough=0.2, emission=((1, 1, 1), 2.5))
	MOUTH = G.simple_mat(sp + '_mouth', lin((0.42, 0.10, 0.08)), rough=0.5)
	BLUSH = G.simple_mat(sp + '_blush', lin((1.0, 0.58, 0.48)), rough=0.4)

	d = BUILD[sp](M)
	CZ, top_z = d['CZ'], d['top_z']
	fr = d['feet_r']
	feet = G.metaball(sp + '_feet', [
		mell((0.085, -0.03, 0.032), (fr, fr * 1.3, 0.032)),
		mell((-0.085, -0.03, 0.032), (fr, fr * 1.3, 0.032)),
		mcap((0.075, 0.0, 0.09), (0.085, -0.02, 0.04), fr * 0.6),
		mcap((-0.075, 0.0, 0.09), (-0.085, -0.02, 0.04), fr * 0.6),
	], resolution=0.008)
	feet.data.materials.append(d['feet_mat'])
	face = face_on(d.get('face_target', d['body']), CZ, d['eye_dx'], d['eye'],
		(EYE, SHINE, MOUTH, BLUSH))

	if STAGE == 'model':
		G.iso_camera(400, 400, 200, 340, scale=8)
		G.render_sprite(os.path.join(G.ROOT, 'preview'), sp + '_model')
		return

	bones = [
		('root', (0, 0, 0), (0, 0.15, 0), None, False, 0, False),
		('body', (0, 0, 0.06), (0, 0, top_z), 'root', True, 0, False),
		('top', (0, 0, top_z - 0.02), (0, 0, top_z + 0.06), 'body', True, 0, False),
		('stem', (0, 0, top_z + 0.03), (0, 0, top_z + 0.12), 'top', True, 0, False),
		('foot.L', (0.085, 0.0, 0.11), (0.09, -0.02, 0.0), 'root', True, 0, False),
		('foot.R', (-0.085, 0.0, 0.11), (-0.09, -0.02, 0.0), 'root', True, 0, False),
	]
	arm = G.make_armature(sp.capitalize() + '_Rig', bones)
	G.skin(d['body'], arm, only='body')
	for o in d['top']:
		G.skin(o, arm, only='top')
	for o in d['stem']:
		G.skin(o, arm, only='stem')
	G.skin(feet, arm, ['foot.L', 'foot.R'], power=8, side_split=True)
	for f in face:
		G.skin(f, arm, only='body')
	ob = G.join([d['body'], feet] + d['top'] + d['stem'], sp.capitalize())
	fc = G.join(face, sp.capitalize() + '_Face')
	G.bind(ob, arm)
	G.bind(fc, arm)

	# ---- 동작 (tomatoma.py 와 같은 곡선) ----
	IDLE = G.anim_fn(arm, sp + '_idle', 1, 25, 1, {
		'body': lambda t: dict(scale=(1 + 0.03 * sin(t * TAU), 1 - 0.035 * sin(t * TAU),
			1 + 0.03 * sin(t * TAU))),
		'top': lambda t: dict(rot=(4 * sin(t * TAU + 0.5), 0, 3 * cos(t * TAU))),
		'stem': lambda t: dict(rot=(8 * sin(t * TAU + 1.2), 0, 6 * cos(t * TAU + 0.4))),
	})

	def hop(t):
		if t < 0.2:
			return 0.0, -0.10 * math.sin(t / 0.2 * math.pi * 0.5)
		if t < 0.75:
			u = (t - 0.2) / 0.55
			return 0.085 * math.sin(u * math.pi), 0.12 * math.cos(u * math.pi)
		u = (t - 0.75) / 0.25
		return 0.0, -0.12 * math.sin(u * math.pi)

	HOP = G.anim_fn(arm, sp + '_hop', 1, 13, 1, {
		'body': lambda t: dict(loc=(0, hop(t)[0], 0), scale=(1 - hop(t)[1] * 0.6, 1 + hop(t)[1],
			1 - hop(t)[1] * 0.6), rot=(-8 * math.sin(t * TAU), 0, 0)),
		'foot.L': lambda t: dict(loc=(0, -hop(t)[0], 0), rot=(-25 * max(0, hop(t)[0] / 0.085), 0, 0)),
		'foot.R': lambda t: dict(loc=(0, -hop(t)[0], 0), rot=(-25 * max(0, hop(t)[0] / 0.085), 0, 0)),
		'top': lambda t: dict(rot=(-14 * hop(t)[1] / 0.12, 0, 0)),
		'stem': lambda t: dict(rot=(-20 * hop(t)[1] / 0.12, 0, 0)),
	})

	def caught_body(t):
		e = t * t * (3 - 2 * t)
		sc = 1.0 - 0.75 * e
		return dict(loc=(0, 0.12 * e, 0), rot=(0, 720 * e, 0), scale=(sc, sc, sc))

	CAUGHT = G.anim_fn(arm, sp + '_caught', 101, 125, 1, {
		'body': caught_body,
		'foot.L': lambda t: dict(scale=(max(0.01, 1 - t * 1.3),) * 3),
		'foot.R': lambda t: dict(scale=(max(0.01, 1 - t * 1.3),) * 3),
		'top': lambda t: dict(rot=(0, 0, 0)),
	})
	arm.animation_data.action = None
	G.reset_pose(arm)
	bpy.context.view_layer.update()

	G.smart_uv(ob, margin=0.004)
	G.bake_asset_textures(ob, sp, res=512)

	# 포획 발광 (프레임 101~125 에서만)
	for m in list(ob.data.materials) + [EYE]:
		if m is None:
			continue
		b = m.node_tree.nodes.get('BSDF') or m.node_tree.nodes.get('Principled BSDF')
		if b is None:
			continue
		b.inputs['Emission Color'].default_value = (1.0, 0.86, 0.55, 1.0)
		for f, v in ((1, 0.0), (101, 0.0), (107, 0.25), (114, 1.2), (119, 3.5), (125, 9.0)):
			b.inputs['Emission Strength'].default_value = v
			b.inputs['Emission Strength'].keyframe_insert('default_value', frame=f)

	if STAGE == 'anim':
		G.iso_camera(110, 140, 55, 120, scale=2)
		for act, frames in ((HOP, (1, 5, 8)), (CAUGHT, (101, 113, 122)), (IDLE, (1,))):
			G.use_action(arm, act)
			for f in frames:
				bpy.context.scene.frame_set(f)
				G.render_sprite(os.path.join(G.ROOT, 'preview', 'critters'), '%s_%03d' % (act.name, f))
		return

	anims = [
		('idle', IDLE, [1, 5, 9, 13, 17, 21], 6, 1),
		('hop', HOP, [1, 3, 4, 5, 7, 9, 10, 12], 12, 1),
		('caught', CAUGHT, [101, 104, 107, 110, 113, 116, 119, 122], 10, 0),
	]
	G.use_action(arm, IDLE)
	G.render_character(arm, arm, sp, (110, 140), (55, 120), anims)
	arm.animation_data.action = IDLE
	bpy.context.scene.frame_set(1)
	G.save_blend(sp)
	print('[critters] done', sp)


for sp in WANT:
	make(sp)
