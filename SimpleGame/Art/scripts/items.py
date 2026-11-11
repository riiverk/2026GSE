# -----------------------------------------------------------------------------
# items.py — 레벨 1 에서 떨어지는 재료 셋 (DropActor)
#
# item_berry  토마토마 열매: 반들반들한 방울토마토 + 별 모양 꼭지
# item_meat   알밤멧돼지 등심: 마블링이 보이는 단면 + 밤색 껍질 지방층
# item_core   미식핵: 밤톨 모양의 금빛 결정. 안에서 빛난다 (블룸 임계값 위)
#
# 음식이라 윤기를 살린다 (MATTE 0). 엔진이 둥실 띄우고 그림자는 따로 그린다.
# -----------------------------------------------------------------------------
import sys, os, math, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy
import bmesh
from mathutils import Vector
import ggcommon as G
from ggcommon import mball, mell, mcap, lin

STAGE = os.environ.get('GG_STAGE', 'all')
OUT = os.path.join(G.ROOT, 'renders', 'items')
CANVAS = (96, 96)
PIVOT = (48, 72)

G.reset(samples=64)
G.light_rig()
G.MATTE = 0.0
STATICS = []


def render_only(ob, name):
	for o in bpy.data.objects:
		if o.type == 'MESH':
			o.hide_render = (o is not ob)
	G.iso_camera(CANVAS[0], CANVAS[1], PIVOT[0], PIVOT[1], scale=G.SCALE)
	bpy.context.scene.frame_set(1)
	G.render_sprite(OUT if STAGE != 'model' else os.path.join(G.ROOT, 'preview', 'items'), name)
	STATICS.append(dict(name=name, canvas=list(CANVAS), pivot=list(PIVOT)))


# ---- 토마토마 열매 ----
skin = G.proc_mat('item_tomato', lin((0.90, 0.22, 0.16)), lin((0.80, 0.16, 0.12)),
	var_scale=5, var_amt=0.5, rough=0.22, rough_var=0.08, macro=('noise', 6.0, 2.0),
	macro_amt=0.3, cavity=0.1, normal_strength=0.3, micro=('noise', 300.0, 2.0),
	bump_strength=0.05, sss=0.25, sss_radius=(0.6, 0.2, 0.1), coat=0.6, height_scale=0.004)
leaf = G.proc_mat('item_calyx', lin((0.36, 0.56, 0.24)), lin((0.26, 0.44, 0.18)),
	var_scale=8, var_amt=0.5, rough=0.5, micro=('noise', 200.0), bump_strength=0.1)
tom = G.metaball('it_tomato', [mell((0, 0, 0.17), (0.17, 0.17, 0.145))], resolution=0.008)
tom.data.materials.append(skin)
cal = []
for i in range(5):
	a = i * 2 * math.pi / 5
	tip = (0.12 * math.cos(a), 0.12 * math.sin(a), 0.29)
	cal.append(mcap((0, 0, 0.315), tip, 0.022))
cal.append(mcap((0, 0, 0.30), (0.02, 0.0, 0.38), 0.018))
calyx = G.metaball('it_calyx', cal, resolution=0.006)
G.deform(calyx, lambda v: Vector((v.x, v.y, v.z - 0.25 * (v.x * v.x + v.y * v.y))))
calyx.data.materials.append(leaf)
berry = G.join([tom, calyx], 'ItemBerry')

# ---- 멧돼지 등심 ----
meat_m = G.proc_mat('item_meat', lin((0.86, 0.36, 0.34)), lin((0.97, 0.86, 0.80)),
	var_scale=9, var_amt=0.55, var_edge=(0.55, 0.62), rough=0.35, rough_var=0.1,
	macro=('noise', 12.0, 4.0), macro_amt=0.4, cavity=0.2, normal_strength=0.4,
	micro=('noise', 260.0, 3.0), bump_strength=0.08, sss=0.3, sss_radius=(0.8, 0.3, 0.2),
	coat=0.3, height_scale=0.004)
crust_m = G.proc_mat('item_crust', lin((0.52, 0.30, 0.16)), lin((0.40, 0.22, 0.12)),
	var_scale=6, var_amt=0.6, rough=0.4, macro=('noise', 20.0, 3.0), macro_amt=0.5,
	cavity=0.2, normal_strength=0.5, micro=('noise', 220.0), bump_strength=0.1, coat=0.4)
slab = G.metaball('it_slab', [mell((0, 0, 0.11), (0.21, 0.13, 0.10)),
	mell((0.06, 0.02, 0.12), (0.12, 0.10, 0.09))], resolution=0.008)
slab.data.materials.append(meat_m)
cap = G.metaball('it_cap', [mell((0, 0.075, 0.13), (0.22, 0.07, 0.11))], resolution=0.008)
cap.data.materials.append(crust_m)
bone_m = G.simple_mat('item_bone', lin((0.95, 0.90, 0.80)), rough=0.4)
bone = G.metaball('it_bone', [mcap((-0.20, 0.0, 0.12), (-0.30, 0.02, 0.15), 0.028),
	mball((-0.31, 0.03, 0.17), 0.034), mball((-0.31, -0.01, 0.13), 0.032)], resolution=0.006)
bone.data.materials.append(bone_m)
meat = G.join([slab, cap, bone], 'ItemMeat')
meat.rotation_euler = (0, 0, math.radians(-25))
G.bake_transform(meat)

# ---- 미식핵 ----
core_m = G.simple_mat('item_core', lin((0.98, 0.68, 0.22)), rough=0.12, coat=1.0, metallic=0.3,
	emission=(lin((1.0, 0.70, 0.30)), 0.45))
glint_m = G.simple_mat('item_core_glint', (1, 1, 1), rough=0.1, emission=((1, 1, 1), 4.0))
core = G.ico('it_core', 1.0, (0, 0, 0.22), (0.15, 0.15, 0.19), sub=1)
G.deform(core, lambda v: Vector((v.x, v.y, v.z + 0.06 * max(0.0, v.z - 0.22) / 0.19)))
core.data.materials.append(core_m)
glint = G.sphere('it_glint', 0.03, (-0.05, -0.08, 0.30), (1, 1, 1), seg=10, rings=6)
glint.data.materials.append(glint_m)
gem = G.join([core, glint], 'ItemCore')
for p in gem.data.polygons:
	p.use_smooth = False

if STAGE == 'model':
	for ob, nm in ((berry, 'item_berry'), (meat, 'item_meat'), (gem, 'item_core')):
		render_only(ob, nm)
	sys.exit(0)

for ob, asset in ((berry, 'item_berry'), (meat, 'item_meat')):
	G.smart_uv(ob, margin=0.004)
	G.bake_asset_textures(ob, asset, res=512)
for ob, nm in ((berry, 'item_berry'), (meat, 'item_meat'), (gem, 'item_core')):
	render_only(ob, nm)
G.write_sprite_meta(OUT, dict(asset='items', scale=G.SCALE, statics=STATICS))
G.save_blend('items')
print('[items] done')
