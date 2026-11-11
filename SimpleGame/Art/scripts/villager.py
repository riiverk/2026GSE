# -----------------------------------------------------------------------------
# villager.py — 마을 주민 공용 몸 + 옷 색 레이어 + 모자 11종
#
# 주민 30여 명을 모델 하나로 덮는다. 엔진은 세 장을 겹쳐 그린다.
#   villager        몸 전체 (옷은 중간 회색 — 아래 레이어가 덮는다)
#   villager_cloth  옷만 (흰 천). 나머지는 홀드아웃이라 가려지는 부분이 정확하다.
#                   엔진이 NPC 의 cloth 색을 곱한다 (npcs.ggd 의 cloth)
#   villager_hat    모자만 (npcs.ggd 의 look). 기본 자세로 방향마다 한 장.
#                   몸 프레임마다 머리 앵커가 sprites.txt 에 있어 엔진이 따라 옮긴다
#
# 체형: 성인, 약 3등신 (리노보다 머리가 작고 팔다리가 길다). 높이 약 1.68 BU.
# 옷: 허리띠를 맨 긴소매 튜닉 + 갈색 바지 + 부츠. 머리는 짧게 붙여 모자가 덮는다.
# -----------------------------------------------------------------------------
import sys, os, math, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy
from mathutils import Vector
import ggcommon as G
from ggcommon import mball, mell, mcap, lin

STAGE = os.environ.get('GG_STAGE', 'all')   # model | anim | all
ONLY = os.environ.get('GG_ONLY', '')        # hats 만 다시: GG_ONLY=hats

G.reset(samples=40)
G.light_rig()
G.MATTE = 1.0

# =============================================================================
# 머티리얼
# =============================================================================
M = {}
M['skin'] = G.proc_mat('vil_skin', lin((0.90, 0.72, 0.58)), lin((0.92, 0.66, 0.56)),
	var_scale=6, var_amt=0.35, rough=0.55, rough_var=0.08,
	micro=('noise', 160.0, 3.0), bump_strength=0.08, sss=0.2,
	sss_radius=(0.5, 0.22, 0.12), height_scale=0.004)
M['hair'] = G.proc_mat('vil_hair', lin((0.30, 0.20, 0.13)), lin((0.22, 0.14, 0.09)),
	var_scale=5, var_amt=0.6, rough=0.45, rough_var=0.12,
	macro=('wave', 3.0, 3.0, 'Z'), macro_amt=0.5, cavity=0.15, normal_strength=0.45,
	micro=('noise', 90.0, 4.0), bump_strength=0.12, sheen=0.3, height_scale=0.010)
# 옷: 거의 흰 천. 엔진이 색을 곱하므로 명암과 결만 담는다
M['cloth'] = G.proc_mat('vil_cloth', lin((0.93, 0.92, 0.90)), lin((0.84, 0.83, 0.81)),
	var_scale=3, var_amt=0.5, rough=0.82, rough_var=0.05,
	macro=('noise', 5.0, 2.0, 0.5), macro_amt=0.7, cavity=0.2,
	micro=('cells', 240.0, 3.0), bump_strength=0.18, sheen=0.35, height_scale=0.012)
M['trousers'] = G.proc_mat('vil_trousers', lin((0.36, 0.30, 0.25)), lin((0.29, 0.24, 0.20)),
	var_scale=4, var_amt=0.5, rough=0.85, macro=('noise', 6.0, 2.0), macro_amt=0.6,
	micro=('wave', 200.0, 1.0, 'X'), bump_strength=0.15, height_scale=0.012)
M['leather'] = G.proc_mat('vil_leather', lin((0.38, 0.24, 0.14)), lin((0.28, 0.17, 0.09)),
	var_scale=6, var_amt=0.6, rough=0.45, rough_var=0.15,
	macro=('noise', 14.0, 3.0), macro_amt=0.5, cavity=0.2,
	micro=('cells', 180.0, 4.0), bump_strength=0.2, coat=0.1, height_scale=0.008)
BRASS = G.simple_mat('vil_brass', lin((0.85, 0.66, 0.32)), rough=0.35, metallic=1.0)

EYE = G.simple_mat('vil_eye', lin((0.12, 0.08, 0.06)), rough=0.15, coat=1.0)
SHINE = G.simple_mat('vil_shine', (1, 1, 1), rough=0.2, emission=((1, 1, 1), 2.5))
MOUTH = G.simple_mat('vil_mouth', lin((0.50, 0.22, 0.18)), rough=0.5)
BLUSH = G.simple_mat('vil_blush', lin((0.96, 0.62, 0.55)), rough=0.6)
BROW = G.simple_mat('vil_brow', lin((0.22, 0.14, 0.09)), rough=0.6)

# 렌더 패스용
GRAY = G.simple_mat('vil_cloth_base', lin((0.52, 0.50, 0.48)), rough=0.85)
HOLD = bpy.data.materials.new('vil_holdout')
_nt = HOLD.node_tree
for _n in list(_nt.nodes):
	_nt.nodes.remove(_n)
_o = _nt.nodes.new('ShaderNodeOutputMaterial')
_h = _nt.nodes.new('ShaderNodeHoldout')
_nt.links.new(_h.outputs[0], _o.inputs['Surface'])

# =============================================================================
# 뼈 위치
# =============================================================================
J = dict(
	hip=(0, 0, 0.60), spine=(0, 0, 0.76), chest=(0, 0, 0.92), neck=(0, 0, 1.10),
	head=(0, 0, 1.15), head_top=(0, 0, 1.66),
	sho=(0.200, 0.0, 1.035), elb=(0.258, 0.008, 0.835), wri=(0.285, -0.012, 0.655),
	hand_end=(0.292, -0.022, 0.565),
	thigh=(0.100, 0.0, 0.600), knee=(0.105, -0.014, 0.335), ankle=(0.105, 0.0, 0.090),
	toe=(0.105, -0.13, 0.035),
)
HC = Vector((0, 0, 1.37))      # 머리 중심


def mir(p):
	return (-p[0], p[1], p[2])


def sides():
	return ((1, lambda p: p), (-1, mir))


# =============================================================================
# 메쉬 — 몸
# =============================================================================
# 튜닉 (옷 레이어)
tunic_el = [
	mell((0, 0.004, 0.90), (0.205, 0.160, 0.20)),
	mell((0, -0.006, 0.74), (0.188, 0.150, 0.12)),
	mell((0, 0.000, 0.58), (0.214, 0.172, 0.11)),     # 허리 아래로 퍼지는 자락
	mcap((0, 0, 1.00), (0, 0, 1.07), 0.085),            # 깃
]
for s, f in sides():
	tunic_el += [mball(f((0.165, 0.0, 1.015)), 0.078)]
tunic = G.metaball('vil_tunic', tunic_el, resolution=0.014)

sleeve_el = []
for s, f in sides():
	sleeve_el += [mcap(f(J['sho']), f(J['elb']), 0.066),
		mcap(f(J['elb']), f((0.282, -0.010, 0.675)), 0.058)]
sleeves = G.metaball('vil_sleeves', sleeve_el, resolution=0.011)

# 손
hand_el = []
for s, f in sides():
	hand_el += [mell(f((0.290, -0.020, 0.610)), (0.044, 0.050, 0.056)),
		mball(f((0.268, -0.058, 0.632)), 0.022)]
hands = G.metaball('vil_hands', hand_el, resolution=0.010)

# 바지
leg_el = []
for s, f in sides():
	leg_el += [mcap(f(J['thigh']), f(J['knee']), 0.076),
		mcap(f(J['knee']), f((0.105, -0.004, 0.13)), 0.064)]
legs = G.metaball('vil_legs', leg_el, resolution=0.012)

# 부츠
boot_el = []
for s, f in sides():
	boot_el += [mell(f((0.105, -0.036, 0.052)), (0.072, 0.118, 0.056)),
		mball(f((0.105, 0.0, 0.110)), 0.070),
		mell(f((0.105, -0.004, 0.165)), (0.072, 0.068, 0.036))]
boots = G.metaball('vil_boots', boot_el, resolution=0.014)

# 머리 + 목 + 귀 + 코
head = G.metaball('vil_head', [
	mell((0, 0.0, 1.375), (0.232, 0.218, 0.238)),
	mell((0, -0.055, 1.280), (0.172, 0.150, 0.118)),
	mcap((0, 0, 1.04), (0, 0, 1.20), 0.072),
	mell((0.232, 0.02, 1.345), (0.030, 0.045, 0.056)),
	mell((-0.232, 0.02, 1.345), (0.030, 0.045, 0.056)),
	mball((0, -0.224, 1.322), 0.030),
], resolution=0.013)

# 머리카락: 두피에 붙인 짧은 머리. 모자가 덮을 수 있도록 부피를 낮게
hair_el = [
	mell((0, 0.030, 1.448), (0.254, 0.248, 0.205)),
	mell((0, 0.105, 1.320), (0.232, 0.168, 0.135)),
	mell((0.216, 0.030, 1.390), (0.058, 0.150, 0.120)),
	mell((-0.216, 0.030, 1.390), (0.058, 0.150, 0.120)),
]
# 앞머리는 이마에 낮게 붙인다. 모자 테두리 안쪽에 들어가야 위로 삐져나오지 않는다
for (x0, x1, r) in ((-0.15, -0.19, 0.040), (-0.05, -0.09, 0.043), (0.05, 0.01, 0.042),
		(0.15, 0.11, 0.038)):
	hair_el += [mcap((x0, -0.130, 1.500), (x1, -0.192, 1.452), r * 0.88)]
hair = G.metaball('vil_hair', hair_el, resolution=0.012)

# 허리띠: 튜닉 표면을 재서 맞춘다
pxr, _ = G.raycast_place(tunic, (2.0, 0.0, 0.665), (-1, 0, 0))
pyr, _ = G.raycast_place(tunic, (0.0, -2.0, 0.665), (0, 1, 0))
bx = (pxr.x if pxr else 0.19) + 0.006
by = (-pyr.y if pyr else 0.16) + 0.006
belt = G.torus('vil_belt', 1.0, 0.020, (0, 0.0, 0.665), (0, 0, 0), scale=(bx, by, 1.0),
	seg=40, ring=8)
buckle = G.box('vil_buckle', (0.050, 0.014, 0.040), (0.0, -by - 0.004, 0.665))
G.bevel(buckle, 0.005, 2)
pouch = G.box('vil_pouch', (0.070, 0.040, 0.075), (bx * 0.78, -by * 0.62, 0.615), (0, 0, -25))
G.bevel(pouch, 0.015, 3)

# =============================================================================
# 얼굴
# =============================================================================
face_parts = []


def stick(name, mat, x, z, size, push=0.0, seg=16):
	p, n = G.raycast_place(head, (x, -1.0, z), (0, 1, 0), push)
	if p is None:
		return None
	ob = G.sphere(name, 1.0, (0, 0, 0), size, seg=seg, rings=seg // 2)
	ob.rotation_mode = 'QUATERNION'
	ob.rotation_quaternion = Vector((0, -1, 0)).rotation_difference(n)
	ob.location = p
	G.bake_transform(ob)
	ob.data.materials.append(mat)
	face_parts.append(ob)
	return ob


for s in (1, -1):
	stick('vil_eye', EYE, 0.088 * s, 1.372, (0.031, 0.014, 0.044), push=0.002)
	stick('vil_shine', SHINE, 0.080 * s + 0.003, 1.390, (0.010, 0.005, 0.012), push=0.013, seg=10)
	stick('vil_blush', BLUSH, 0.150 * s, 1.300, (0.034, 0.007, 0.018))
	stick('vil_brow', BROW, 0.090 * s, 1.448, (0.040, 0.010, 0.010))
stick('vil_mouth', MOUTH, 0.0, 1.258, (0.028, 0.009, 0.011))

# =============================================================================
# 머티리얼 배정 + 합치기
# =============================================================================
for ob, key in ((tunic, 'cloth'), (sleeves, 'cloth'), (hands, 'skin'), (legs, 'trousers'),
		(boots, 'leather'), (head, 'skin'), (hair, 'hair'), (belt, 'leather'),
		(pouch, 'leather')):
	ob.data.materials.append(M[key])
buckle.data.materials.append(BRASS)

# =============================================================================
# 모자 11종 (npcs.ggd 의 look 이름과 같다)
# =============================================================================
HAT_MATS = {}


def hmat(name, color, rough=0.6, **kw):
	if name not in HAT_MATS:
		HAT_MATS[name] = G.simple_mat('hat_' + name, lin(color), rough=rough, **kw)
	return HAT_MATS[name]


def finish(objs, name):
	ob = G.join(objs, 'Hat_' + name) if len(objs) > 1 else objs[0]
	ob.name = 'Hat_' + name
	return ob


def brim_profile(r_in, r_out, z, curl=0.0, th=0.016):
	"""모자 챙 (lathe 프로필). curl > 0 이면 가장자리가 위로 말린다."""
	pts = []
	n = 8
	for i in range(n + 1):
		r = r_in + (r_out - r_in) * i / n
		u = i / n
		pts.append((r, z + curl * u * u))
	top = [(r, zz + th) for (r, zz) in reversed(pts)]
	return pts + top


def hat_bandit():
	red = hmat('bandit_red', (0.70, 0.18, 0.15), 0.8)
	dark = hmat('bandit_mask', (0.58, 0.15, 0.13), 0.85)
	cap = G.metaball('hb_cap', [
		mell((0, 0.015, 1.490), (0.280, 0.272, 0.192)),
		mcap((0.0, 0.250, 1.430), (0.070, 0.390, 1.300), 0.036),
		mcap((0.0, 0.250, 1.430), (-0.050, 0.400, 1.320), 0.032),
		mball((0.0, 0.255, 1.432), 0.050),
	], resolution=0.010)
	cap.data.materials.append(red)
	# 코 아래를 가리는 복면 수건
	mask = G.metaball('hb_mask', [
		mell((0, -0.160, 1.270), (0.218, 0.085, 0.068)),
		mcap((-0.170, 0.08, 1.290), (0.170, 0.08, 1.290), 0.036),
		mcap((0.205, -0.10, 1.270), (0.200, 0.08, 1.290), 0.038),
		mcap((-0.205, -0.10, 1.270), (-0.200, 0.08, 1.290), 0.038),
	], resolution=0.010)
	mask.data.materials.append(dark)
	return finish([cap, mask], 'bandit')


def hat_merchant():
	c = hmat('merchant', (0.48, 0.33, 0.20), 0.85)
	cap = G.metaball('hm_cap', [
		mell((0.02, 0.020, 1.600), (0.268, 0.285, 0.078)),
		mell((0, 0.000, 1.545), (0.250, 0.250, 0.060)),
		mell((0, -0.230, 1.560), (0.170, 0.110, 0.020)),
		mball((0.02, 0.02, 1.672), 0.026),
	], resolution=0.010)
	cap.data.materials.append(c)
	return finish([cap], 'merchant')


def hat_straw(name, color, band):
	straw = hmat(name, color, 0.75)
	bandm = hmat(name + '_band', band, 0.7)
	brim = G.lathe('hs_brim', brim_profile(0.20, 0.45, 1.555, curl=-0.03), seg=48,
		close_bottom=False)
	crown = G.lathe('hs_crown', [(0.235, 1.545), (0.232, 1.640), (0.205, 1.735), (0.12, 1.785),
		(0.0, 1.795)], seg=40, close_bottom=False)
	for o in (brim, crown):
		o.data.materials.append(straw)
	band_ob = G.torus('hs_band', 0.236, 0.022, (0, 0, 1.585), seg=40, ring=8)
	band_ob.data.materials.append(bandm)
	objs = [brim, crown, band_ob]
	if name == 'pilgrim':
		# 순례자의 조개 배지
		shell = G.sphere('hs_shell', 1.0, (0.0, -0.250, 1.600), (0.045, 0.012, 0.040), seg=16, rings=8)
		shell.data.materials.append(hmat('pilgrim_shell', (0.95, 0.90, 0.80), 0.4))
		objs.append(shell)
	return finish(objs, name)


def hat_hunter():
	felt = hmat('hunter', (0.32, 0.42, 0.25), 0.85)
	crown = G.lathe('hh_crown', [(0.245, 1.540), (0.230, 1.640), (0.180, 1.760), (0.10, 1.800),
		(0.0, 1.780)], seg=40, close_bottom=False)
	G.deform(crown, lambda v: Vector((v.x, v.y, v.z - 0.035 * max(0.0, 1 - abs(v.x) / 0.10)
		* (1 if v.z > 1.74 else 0))))
	brim = G.lathe('hh_brim', brim_profile(0.22, 0.34, 1.545, curl=0.04), seg=40,
		close_bottom=False)
	for o in (crown, brim):
		o.data.materials.append(felt)
	band = G.torus('hh_band', 0.243, 0.016, (0, 0, 1.575), seg=40, ring=8)
	band.data.materials.append(hmat('hunter_band', (0.30, 0.20, 0.12), 0.7))
	feather = G.metaball('hh_feather', [
		mcap((0.200, 0.080, 1.600), (0.250, 0.150, 1.760), 0.022),
		mcap((0.250, 0.150, 1.760), (0.270, 0.200, 1.880), 0.016),
	], resolution=0.008)
	feather.data.materials.append(hmat('hunter_feather', (0.93, 0.87, 0.72), 0.6))
	return finish([crown, brim, band, feather], 'hunter')


def hat_bard():
	red = hmat('bard', (0.78, 0.25, 0.30), 0.8)
	cap = G.metaball('hd_cap', [
		mell((-0.060, 0.020, 1.615), (0.300, 0.285, 0.090)),
		mell((-0.170, 0.030, 1.570), (0.140, 0.200, 0.070)),
		mell((0, 0, 1.545), (0.245, 0.245, 0.055)),
	], resolution=0.010)
	cap.data.materials.append(red)
	feather = G.metaball('hd_feather', [
		mcap((0.120, 0.060, 1.650), (0.230, 0.110, 1.840), 0.026),
		mcap((0.230, 0.110, 1.840), (0.330, 0.180, 1.960), 0.018),
		mball((0.335, 0.185, 1.968), 0.014),
	], resolution=0.008)
	feather.data.materials.append(hmat('bard_feather', (0.98, 0.90, 0.55), 0.6))
	return finish([cap, feather], 'bard')


def hat_noble():
	blk = hmat('noble', (0.15, 0.13, 0.16), 0.55)
	crown = G.cylinder('hn_crown', 0.178, 0.192, 0.30, (0, 0, 1.715), seg=40)
	G.bevel(crown, 0.012, 2, angle=60)
	brim = G.lathe('hn_brim', brim_profile(0.17, 0.30, 1.560, curl=0.05, th=0.014),
		seg=48, close_bottom=False)
	for o in (crown, brim):
		o.data.materials.append(blk)
	band = G.torus('hn_band', 0.181, 0.026, (0, 0, 1.605), scale=(1, 1, 1.15), seg=40, ring=8)
	band.data.materials.append(hmat('noble_band', (0.46, 0.20, 0.36), 0.6))
	return finish([crown, brim, band], 'noble')


def hat_villain():
	blk = hmat('villain', (0.13, 0.11, 0.12), 0.7)
	brim = G.lathe('hv_brim', brim_profile(0.20, 0.46, 1.555, curl=0.07, th=0.016),
		seg=48, close_bottom=False)
	crown = G.lathe('hv_crown', [(0.240, 1.545), (0.225, 1.650), (0.170, 1.740), (0.0, 1.770)],
		seg=40, close_bottom=False)
	for o in (brim, crown):
		o.data.materials.append(blk)
	# 오른쪽 눈 안대 + 끈
	patch = None
	p, n = G.raycast_place(head, (-0.088, -1.0, 1.372), (0, 1, 0), 0.010)
	if p is not None:
		patch = G.sphere('hv_patch', 1.0, (0, 0, 0), (0.052, 0.012, 0.050), seg=16, rings=8)
		patch.rotation_mode = 'QUATERNION'
		patch.rotation_quaternion = Vector((0, -1, 0)).rotation_difference(n)
		patch.location = p
		G.bake_transform(patch)
		patch.data.materials.append(hmat('villain_patch', (0.10, 0.09, 0.10), 0.5))
	strap = G.torus('hv_strap', 0.240, 0.009, (0, 0.005, 1.400), (0, -16, 0), scale=(1, 0.96, 1),
		seg=48, ring=6)
	strap.data.materials.append(hmat('villain_patch', (0.10, 0.09, 0.10), 0.5))
	return finish([brim, crown, strap] + ([patch] if patch else []), 'villain')


def hat_official():
	navy = hmat('official', (0.22, 0.28, 0.48), 0.7)
	crown = G.cylinder('ho_crown', 0.236, 0.250, 0.15, (0, 0, 1.610), seg=40)
	G.bevel(crown, 0.02, 2, angle=60)
	crown.data.materials.append(navy)
	visor = G.metaball('ho_visor', [mell((0, -0.250, 1.548), (0.165, 0.090, 0.016))],
		resolution=0.008)
	visor.data.materials.append(hmat('official_visor', (0.12, 0.12, 0.16), 0.35))
	band = G.torus('ho_band', 0.238, 0.020, (0, 0, 1.570), seg=40, ring=8)
	band.data.materials.append(hmat('official_gold', (0.95, 0.80, 0.40), 0.35, metallic=1.0))
	badge = G.sphere('ho_badge', 1.0, (0, -0.252, 1.640), (0.030, 0.010, 0.030), seg=12, rings=6)
	badge.data.materials.append(hmat('official_gold', (0.95, 0.80, 0.40), 0.35, metallic=1.0))
	return finish([crown, visor, band, badge], 'official')


def hat_guard():
	steel = hmat('guard', (0.70, 0.72, 0.76), 0.48, metallic=1.0)
	dome = G.lathe('hg_dome', [(0.292, 1.450)] + [(0.292 * math.cos(a), 1.450 + 0.240 * math.sin(a))
		for a in [math.radians(x) for x in range(8, 91, 8)]], seg=48, close_bottom=False)
	brim = G.lathe('hg_brim', brim_profile(0.28, 0.39, 1.450, curl=-0.04, th=0.014),
		seg=48, close_bottom=False)
	ridge = G.torus('hg_ridge', 0.292, 0.014, (0, 0, 1.465), seg=48, ring=6)
	for o in (dome, brim, ridge):
		o.data.materials.append(steel)
	return finish([dome, brim, ridge], 'guard')


def hat_cloak():
	wool = hmat('cloak', (0.24, 0.23, 0.29), 0.9)
	hood = G.sphere('hc_hood', 1.0, (0, 0.025, 1.400), (0.300, 0.300, 0.300), seg=40, rings=20)
	# 얼굴 쪽을 도려낸다
	import bmesh
	bm = bmesh.new()
	bm.from_mesh(hood.data)
	kill = [f for f in bm.faces if f.calc_center_median().y < -0.12 and f.calc_center_median().z < 1.60]
	kill += [f for f in bm.faces if f.calc_center_median().z < 1.16]
	bmesh.ops.delete(bm, geom=list(set(kill)), context='FACES')
	bm.to_mesh(hood.data)
	bm.free()
	G.solidify(hood, 0.024)
	G.subsurf(hood, 1)
	# 어깨를 덮는 망토 자락
	cape = G.lathe('hc_cape', [(0.330, 0.840), (0.320, 0.930), (0.270, 1.040), (0.150, 1.150),
		(0.120, 1.185)], seg=40, close_bottom=False)
	G.solidify(cape, 0.02)
	G.deform(cape, lambda v: Vector((v.x, v.y * (1.0 if v.y > 0 else 0.92), v.z)))
	for o in (hood, cape):
		o.data.materials.append(wool)
	return finish([hood, cape], 'cloak')


HAT_BUILD = [('bandit', hat_bandit), ('merchant', hat_merchant),
	('traveler', lambda: hat_straw('traveler', (0.84, 0.72, 0.45), (0.55, 0.32, 0.22))),
	('hunter', hat_hunter),
	('pilgrim', lambda: hat_straw('pilgrim', (0.62, 0.62, 0.66), (0.36, 0.34, 0.40))),
	('bard', hat_bard), ('noble', hat_noble), ('villain', hat_villain),
	('official', hat_official), ('guard', hat_guard), ('cloak', hat_cloak)]
hats = {}
for nm, fn in HAT_BUILD:
	hats[nm] = fn()

if STAGE == 'model':
	out = os.path.join(G.ROOT, 'preview', 'villager_model')
	for h in hats.values():
		h.hide_render = True
	G.iso_camera(220, 300, 110, 270, scale=2.6)
	G.render_sprite(out, 'iso')
	cam = bpy.data.objects['GG_Cam']
	cam.rotation_euler = (math.radians(90), 0, 0)
	cam.location = (0, -10, 0.95)
	cam.data.shift_x = 0
	cam.data.shift_y = 0
	cam.data.ortho_scale = 2.0
	G.render_sprite(out, 'front')
	cam.rotation_euler = (math.radians(90), 0, math.radians(90))
	cam.location = (10, 0, 0.95)
	G.render_sprite(out, 'side')
	# 모자 시트: 정면에서 머리만
	cam.rotation_euler = (math.radians(75), 0, math.radians(25))
	fwd = cam.rotation_euler.to_matrix() @ Vector((0, 0, -1))
	cam.location = Vector((0, 0, 1.45)) - fwd * 10
	cam.data.ortho_scale = 1.1
	bpy.context.scene.render.resolution_x = 220
	bpy.context.scene.render.resolution_y = 220
	for nm, h in hats.items():
		for o in hats.values():
			o.hide_render = (o is not h)
		G.render_sprite(out, 'hat_' + nm)
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
		('ik_foot' + sfx, side(J['ankle'], s), side((0.105, 0.0, 0.19), s), 'root', False, 0, False),
		('foot' + sfx, side(J['ankle'], s), side(J['toe'], s), 'ik_foot' + sfx, True, 0, False),
		('pole_knee' + sfx, side((0.105, -0.7, 0.335), s), side((0.105, -0.7, 0.42), s), 'root',
			False, 0, False),
	]
arm = G.make_armature('Villager_Rig', bones)

for sfx in ('.L', '.R'):
	G.ik(arm, 'shin' + sfx, 'ik_foot' + sfx, 'pole_knee' + sfx, chain=2, pole_angle=-90)


def knee_check(sfx):
	vl = bpy.context.view_layer
	pb = arm.pose.bones['ik_foot' + sfx]
	pb.location = (0, 0.12, 0.07)
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
	score = k.y + abs(k.x - 0.105) * 2
	if best is None or score < best[0]:
		best = (score, ang)
for sfx in ('.L', '.R'):
	arm.pose.bones['shin' + sfx].constraints['IK'].pole_angle = math.radians(best[1])
print('[villager] pole angle', best)

LIMB = lambda n: [n + '.L', n + '.R']
G.skin(tunic, arm, ['hips', 'spine', 'chest'] + LIMB('upper_arm') + LIMB('thigh'),
	power=8, side_split=True)
G.skin(sleeves, arm, LIMB('upper_arm') + LIMB('forearm'), power=8, side_split=True)
G.skin(hands, arm, LIMB('hand') + LIMB('forearm'), power=8, side_split=True)
G.skin(legs, arm, LIMB('thigh') + LIMB('shin'), power=8, side_split=True)
G.skin(boots, arm, LIMB('shin') + LIMB('foot'), power=10, side_split=True)
G.skin(head, arm, ['chest', 'neck', 'head'], power=8)
G.skin(hair, arm, only='head')
for ob in (belt, buckle, pouch):
	G.skin(ob, arm, ['hips', 'spine'], power=6)
for fp in face_parts:
	G.skin(fp, arm, only='head')
for h in hats.values():
	G.skin(h, arm, only='head')

vil = G.join([tunic, sleeves, hands, legs, boots, head, hair, belt, buckle, pouch], 'Villager')
face = G.join(face_parts, 'Villager_Face')
G.bind(vil, arm)
G.bind(face, arm)
for h in hats.values():
	G.bind(h, arm)
print('[villager] verts', len(vil.data.vertices))

# =============================================================================
# 애니메이션
# =============================================================================
TAU = 2 * math.pi
sin, cos = math.sin, math.cos

IDLE = G.anim_fn(arm, 'villager_idle', 1, 25, 2, {
	'hips': lambda t: dict(loc=(0, -0.004 * (0.5 - 0.5 * cos(t * TAU)), 0)),
	'spine': lambda t: dict(rot=(1.0 * sin(t * TAU), 0, 0)),
	'chest': lambda t: dict(rot=(-1.2 * sin(t * TAU), 0, 0), scale=(1 + 0.010 * sin(t * TAU),) * 3),
	'head': lambda t: dict(rot=(1.2 * sin(t * TAU + 0.6), 0, 2.5 * sin(t * TAU))),
	'upper_arm.L': lambda t: dict(rot=(2.0 * sin(t * TAU), 0, -2 - 1.2 * sin(t * TAU))),
	'upper_arm.R': lambda t: dict(rot=(2.0 * sin(t * TAU), 0, 2 + 1.2 * sin(t * TAU))),
	'forearm.L': lambda t: dict(rot=(-6 - 2 * sin(t * TAU), 0, 0)),
	'forearm.R': lambda t: dict(rot=(-6 - 2 * sin(t * TAU), 0, 0)),
})

STRIDE = 0.095
LIFT = 0.065


def foot_fn(ph):
	def f(t):
		a = t * TAU + ph
		return dict(loc=(0, LIFT * max(0.0, -sin(a)), STRIDE * cos(a)))
	return f


WALK = G.anim_fn(arm, 'villager_walk', 1, 17, 1, {
	'ik_foot.L': foot_fn(0.0),
	'ik_foot.R': foot_fn(math.pi),
	'hips': lambda t: dict(loc=(0, -0.020 + 0.018 * abs(sin(t * TAU)), 0),
		rot=(0, 6 * cos(t * TAU), 0)),
	'spine': lambda t: dict(rot=(4, 0, 0)),
	'chest': lambda t: dict(rot=(1.0, -8 * cos(t * TAU), 0)),
	'head': lambda t: dict(rot=(-3 + 1.5 * sin(2 * t * TAU), 2.0 * cos(t * TAU), 0)),
	'upper_arm.L': lambda t: dict(rot=(26 * cos(t * TAU), 0, -4)),
	'upper_arm.R': lambda t: dict(rot=(-26 * cos(t * TAU), 0, 4)),
	'forearm.L': lambda t: dict(rot=(-14 - 12 * max(0, -cos(t * TAU)), 0, 0)),
	'forearm.R': lambda t: dict(rot=(-14 - 12 * max(0, cos(t * TAU)), 0, 0)),
})
for a in (IDLE, WALK):
	a.use_fake_user = True
arm.animation_data.action = None
G.reset_pose(arm)
bpy.context.view_layer.update()

G.smart_uv(vil, margin=0.004)
G.bake_asset_textures(vil, 'villager', res=1024)

if STAGE == 'anim':
	for h in hats.values():
		h.hide_render = True
	hats['traveler'].hide_render = False
	G.iso_camera(180, 230, 90, 205, scale=2.4)
	for act, frames in ((WALK, (1, 5, 9, 13)), (IDLE, (1, 13))):
		G.use_action(arm, act)
		for f in frames:
			bpy.context.scene.frame_set(f)
			G.render_sprite(os.path.join(G.ROOT, 'preview', 'villager_anim'), '%s_%02d' % (act.name, f))
	sys.exit(0)

# =============================================================================
# 렌더 패스
# =============================================================================
ORIG = {ob.name: [sl.material for sl in ob.material_slots] for ob in (vil, face)}
CLOTH = M['cloth']


def set_pass(kind, hat=None):
	for ob in (vil, face):
		for sl, orig in zip(ob.material_slots, ORIG[ob.name]):
			if kind == 'base':
				sl.material = GRAY if orig is CLOTH else orig
			elif kind == 'cloth':
				sl.material = CLOTH if orig is CLOTH else HOLD
			else:
				sl.material = HOLD
	for nm, h in hats.items():
		h.hide_render = not (kind == 'hat' and nm == hat)


S = G.SCALE
CANVAS = (170, 236)
PIVOT = (85, 212)
K30 = math.cos(math.radians(30))


def head_anchor():
	"""머리 뼈 시작점(목 위)의 화면 위치. 피벗 기준 아틀라스 px."""
	bpy.context.view_layer.update()
	p = arm.matrix_world @ arm.pose.bones['head'].head
	ax = (p.x + p.y) * 36.0 * S
	ay = ((p.x - p.y) * 18.0 - p.z * G.PX_PER_BU * K30) * S
	return [round(ax, 1), round(ay, 1)]


def render_pass(asset, anims, kind, hat=None):
	out_dir = G.ensure_dir(os.path.join(G.ROOT, 'renders', asset))
	G.iso_camera(CANVAS[0], CANVAS[1], PIVOT[0], PIVOT[1], scale=S)
	set_pass(kind, hat)
	sc = bpy.context.scene
	meta = dict(asset=asset, canvas=list(CANVAS), pivot=list(PIVOT), scale=S,
		dirs=[d for d, _ in G.SCREEN_DIRS], anims=[])
	for (an, act, frames, fps, loop) in anims:
		if kind == 'hat':
			set_pass('hat', an)
		G.use_action(arm, act)
		anchors = []
		for (dn, sv) in G.SCREEN_DIRS:
			arm.rotation_euler.z = G.facing_rot_z(G.screen_to_blender_dir(*sv))
			for fi, fr in enumerate(frames):
				sc.frame_set(fr)
				anchors.append(head_anchor())
				G.render_sprite(out_dir, '%s.%s.%s.%d' % (asset, an, dn, fi))
		meta['anims'].append(dict(name=an, frames=len(frames), fps=fps, loop=loop, anchors=anchors))
	arm.rotation_euler.z = 0.0
	G.write_sprite_meta(out_dir, meta)


BODY_ANIMS = [
	('idle', IDLE, [1, 5, 9, 13, 17, 21], 6, 1),
	('walk', WALK, [1, 3, 5, 7, 9, 11, 13, 15], 12, 1),
]
if ONLY != 'hats':
	render_pass('villager', BODY_ANIMS, 'base')
	render_pass('villager_cloth', BODY_ANIMS, 'cloth')
# 모자: 기본 자세(idle 첫 프레임) 한 장씩. 이름 = look
render_pass('villager_hat', [(nm, IDLE, [1], 1, 0) for nm, _ in HAT_BUILD], 'hat')

set_pass('base')
for h in hats.values():
	h.hide_render = True
arm.animation_data.action = IDLE
G.save_blend('villager')
print('[villager] done')
