# -----------------------------------------------------------------------------
# ggcommon.py — 그랑 구스토 블렌더 에셋 파이프라인 공용 모듈
#
# 모든 에셋 스크립트가 이 모듈을 쓴다. 바꾸면 전 에셋을 다시 굽는다.
#
#  1. 씬 초기화 (Cycles, Standard 뷰 변환, 투명 필름)
#  2. 메쉬 헬퍼 (bmesh 프리미티브, 메타볼 클레이, 모디파이어 적용)
#  3. 머티리얼: 프로시저럴 소스 -> 이미지 텍스처 베이크
#       basecolor / roughness / height(매크로) / normal(height 에서 계산) / bump(마이크로)
#  4. 리그 헬퍼 (아마추어, 거리 기반 스키닝, IK)
#  5. 애니메이션 헬퍼 (액션 생성·키)
#  6. 쿼터뷰 스프라이트 렌더 (게임의 72x36 타일과 픽셀 단위로 맞는 정사영 카메라)
#
# 좌표계: 블렌더 X = 게임 wx, 블렌더 Y = -게임 wy, Z = 높이.
# 캐릭터의 정면은 블렌더 -Y (= 카메라 쪽 대각선이 아니라 게임 +wy 방향).
# -----------------------------------------------------------------------------
import bpy, bmesh, math, os, json
import addon_utils
import numpy as np
from mathutils import Vector, Matrix, Euler

# Art/ 폴더 (이 스크립트의 한 단계 위). GG_ROOT 로 바꿀 수 있다
ROOT = os.environ.get('GG_ROOT', os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCALE = 2                                   # 스프라이트는 게임 1배의 2배로 굽는다
PX_PER_BU = 72.0 / math.sqrt(2.0)           # 1배 기준, 1 BU = 1 타일
CAM_ROT = Euler((math.radians(60.0), 0.0, math.radians(45.0)), 'XYZ')

# 스프라이트 방향. 화면 기준 8방향 중 5개만 굽고 W 계열은 좌우 반전으로 만든다.
# (이름, 화면 벡터) — 화면 x 오른쪽, y 아래
SCREEN_DIRS = [
	('s',  (0.0, 1.0)),
	('se', (1.0, 0.5)),
	('e',  (1.0, 0.0)),
	('ne', (1.0, -0.5)),
	('n',  (0.0, -1.0)),
]


def screen_to_blender_dir(sx, sy):
	"""화면 방향 -> 블렌더 수평 방향.
	게임: sx = (wx - wy)*36, sy = (wx + wy)*18. 화면 2:1 을 되돌린 뒤 wx, wy 를 구한다."""
	# 2:1 을 등각으로 되돌림
	ux, uy = sx, sy * 2.0
	wx = (ux + uy) * 0.5
	wy = (uy - ux) * 0.5
	v = Vector((wx, -wy, 0.0))
	return v.normalized()


def facing_rot_z(d):
	"""정면(-Y)이 수평 방향 d 를 보게 하는 Z 회전."""
	return math.atan2(d.x, -d.y)


# =============================================================================
# 1. 씬
# =============================================================================

def reset(samples=48):
	bpy.ops.wm.read_factory_settings(use_empty=True)
	addon_utils.enable('cycles', default_set=True)
	s = bpy.context.scene
	s.render.engine = 'CYCLES'
	s.cycles.device = 'CPU'
	s.cycles.samples = samples
	s.cycles.use_denoising = True
	s.cycles.denoiser = 'OPENIMAGEDENOISE'
	s.cycles.max_bounces = 6
	s.cycles.diffuse_bounces = 3
	s.cycles.glossy_bounces = 3
	s.cycles.transmission_bounces = 4
	s.cycles.use_adaptive_sampling = True
	s.render.film_transparent = True
	s.view_settings.view_transform = 'Standard'
	s.view_settings.look = 'None'
	s.render.fps = 24
	s.render.resolution_percentage = 100
	s.unit_settings.system = 'METRIC'

	w = bpy.data.worlds.new('GG_World')
	s.world = w
	nt = w.node_tree
	bg = nt.nodes.get('Background')
	# 따뜻한 회색 하늘빛. 그림자가 새까맣게 죽지 않게 하는 앰비언트
	bg.inputs['Color'].default_value = (0.62, 0.60, 0.58, 1.0)
	bg.inputs['Strength'].default_value = 0.55
	return s


def light_rig():
	"""모든 에셋이 공유하는 조명. 화면 좌상단·앞쪽에서 오는 키 라이트.

	엔진의 SDF 도형은 위가 밝고(그라디언트), 좌상단에 반사광을 찍는다.
	스프라이트가 같은 방향에서 빛을 받아야 섞여 있어도 한 장면으로 읽힌다."""
	def sun(name, direction_from, strength, color, angle_deg):
		ld = bpy.data.lights.new(name, 'SUN')
		ld.energy = strength
		ld.color = color
		ld.angle = math.radians(angle_deg)
		ob = bpy.data.objects.new(name, ld)
		bpy.context.collection.objects.link(ob)
		d = -Vector(direction_from).normalized()          # 빛이 진행하는 방향
		ob.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
		return ob

	# 키: 화면 왼쪽(-1,-1) + 카메라 쪽(1,-1)*0.5 + 높이
	sun('GG_Key',  (-0.5, -1.5, 1.9), 3.2, (1.0, 0.95, 0.86), 18.0)
	# 필: 화면 오른쪽 앞, 차갑고 약하게
	sun('GG_Fill', (1.4, -0.6, 0.7), 0.55, (0.80, 0.88, 1.0), 40.0)
	# 림: 화면 뒤(먼 쪽)에서. 실루엣을 배경에서 떼어 낸다
	sun('GG_Rim',  (-1.0, 1.2, 0.9), 1.3, (1.0, 0.88, 0.70), 10.0)


def ensure_dir(p):
	os.makedirs(p, exist_ok=True)
	return p


# =============================================================================
# 2. 메쉬
# =============================================================================

def link(ob, coll=None):
	(coll or bpy.context.scene.collection).objects.link(ob)
	return ob


def obj_from_bm(name, bm, smooth=True):
	me = bpy.data.meshes.new(name)
	bm.to_mesh(me)
	bm.free()
	if smooth:
		for p in me.polygons:
			p.use_smooth = True
	ob = bpy.data.objects.new(name, me)
	link(ob)
	return ob


def _xform(loc=(0, 0, 0), rot=(0, 0, 0), scale=(1, 1, 1)):
	return (Matrix.Translation(Vector(loc)) @
		Euler([math.radians(a) for a in rot], 'XYZ').to_matrix().to_4x4() @
		Matrix.Diagonal(Vector((scale[0], scale[1], scale[2], 1.0))))


def sphere(name, r=1.0, loc=(0, 0, 0), scale=(1, 1, 1), rot=(0, 0, 0), seg=32, rings=16):
	bm = bmesh.new()
	bmesh.ops.create_uvsphere(bm, u_segments=seg, v_segments=rings, radius=r)
	bmesh.ops.transform(bm, matrix=_xform(loc, rot, scale), verts=bm.verts)
	return obj_from_bm(name, bm)


def ico(name, r=1.0, loc=(0, 0, 0), scale=(1, 1, 1), rot=(0, 0, 0), sub=3):
	bm = bmesh.new()
	bmesh.ops.create_icosphere(bm, subdivisions=sub, radius=r)
	bmesh.ops.transform(bm, matrix=_xform(loc, rot, scale), verts=bm.verts)
	return obj_from_bm(name, bm)


def cylinder(name, r1=1.0, r2=None, depth=1.0, loc=(0, 0, 0), rot=(0, 0, 0),
		scale=(1, 1, 1), seg=24, caps=True, smooth=True):
	if r2 is None:
		r2 = r1
	bm = bmesh.new()
	bmesh.ops.create_cone(bm, cap_ends=caps, cap_tris=False, segments=seg,
		radius1=r1, radius2=r2, depth=depth)
	bmesh.ops.transform(bm, matrix=_xform(loc, rot, scale), verts=bm.verts)
	return obj_from_bm(name, bm, smooth)


def box(name, size=(1, 1, 1), loc=(0, 0, 0), rot=(0, 0, 0)):
	bm = bmesh.new()
	bmesh.ops.create_cube(bm, size=1.0)
	bmesh.ops.transform(bm, matrix=_xform(loc, rot, size), verts=bm.verts)
	return obj_from_bm(name, bm, smooth=False)


def torus(name, R=1.0, r=0.25, loc=(0, 0, 0), rot=(0, 0, 0), scale=(1, 1, 1),
		seg=32, ring=12):
	bm = bmesh.new()
	verts = []
	for i in range(seg):
		a = 2 * math.pi * i / seg
		row = []
		for j in range(ring):
			b = 2 * math.pi * j / ring
			x = (R + r * math.cos(b)) * math.cos(a)
			y = (R + r * math.cos(b)) * math.sin(a)
			z = r * math.sin(b)
			row.append(bm.verts.new((x, y, z)))
		verts.append(row)
	for i in range(seg):
		for j in range(ring):
			a = verts[i][j]; b = verts[(i + 1) % seg][j]
			c = verts[(i + 1) % seg][(j + 1) % ring]; d = verts[i][(j + 1) % ring]
			bm.faces.new((a, b, c, d))
	bmesh.ops.transform(bm, matrix=_xform(loc, rot, scale), verts=bm.verts)
	return obj_from_bm(name, bm)


def lathe(name, profile, seg=32, loc=(0, 0, 0), rot=(0, 0, 0), scale=(1, 1, 1), close_top=False,
		close_bottom=True):
	"""profile: [(radius, z), ...] 아래에서 위로. 그릇·솥·병 같은 회전체."""
	bm = bmesh.new()
	rings = []
	for (rr, z) in profile:
		row = []
		for i in range(seg):
			a = 2 * math.pi * i / seg
			row.append(bm.verts.new((rr * math.cos(a), rr * math.sin(a), z)))
		rings.append(row)
	for k in range(len(rings) - 1):
		for i in range(seg):
			a = rings[k][i]; b = rings[k][(i + 1) % seg]
			c = rings[k + 1][(i + 1) % seg]; d = rings[k + 1][i]
			bm.faces.new((a, b, c, d))
	if close_bottom and profile[0][0] > 1e-4:
		bm.faces.new(list(reversed(rings[0])))
	if close_top and profile[-1][0] > 1e-4:
		bm.faces.new(rings[-1])
	bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
	bmesh.ops.transform(bm, matrix=_xform(loc, rot, scale), verts=bm.verts)
	return obj_from_bm(name, bm)


def deform(ob, fn):
	"""fn(Vector) -> Vector. 로컬 좌표 정점마다."""
	for v in ob.data.vertices:
		v.co = fn(v.co.copy())
	ob.data.update()


def apply_mods(ob):
	"""모디파이어를 실제 메쉬로 굳힌다 (오퍼레이터 없이)."""
	dg = bpy.context.evaluated_depsgraph_get()
	ev = ob.evaluated_get(dg)
	me = bpy.data.meshes.new_from_object(ev, preserve_all_data_layers=True, depsgraph=dg)
	old = ob.data
	ob.modifiers.clear()
	ob.data = me
	bpy.data.meshes.remove(old)


def subsurf(ob, levels=2, apply=True):
	m = ob.modifiers.new('Sub', 'SUBSURF')
	m.levels = levels
	m.render_levels = levels
	if apply:
		apply_mods(ob)


def bevel(ob, width=0.02, segments=3, apply=True, angle=40.0):
	m = ob.modifiers.new('Bevel', 'BEVEL')
	m.width = width
	m.segments = segments
	m.limit_method = 'ANGLE'
	m.angle_limit = math.radians(angle)
	m.harden_normals = False
	if apply:
		apply_mods(ob)
		for p in ob.data.polygons:
			p.use_smooth = True


def solidify(ob, thickness=0.02, apply=True):
	m = ob.modifiers.new('Solid', 'SOLIDIFY')
	m.thickness = thickness
	m.offset = 0.0
	if apply:
		apply_mods(ob)


def bake_transform(ob):
	"""오브젝트 변환을 메쉬에 굽는다."""
	ob.data.transform(ob.matrix_basis)
	ob.matrix_basis = Matrix.Identity(4)


def set_active(ob):
	vl = bpy.context.view_layer
	for o in vl.objects:
		if o is not None:
			o.select_set(False)
	ob.select_set(True)
	vl.objects.active = ob


def join(objs, name):
	"""여러 메쉬를 하나로. 머티리얼 슬롯과 정점 그룹은 이름으로 합쳐진다."""
	objs = [o for o in objs if o is not None]
	for o in objs:
		bake_transform(o)
	vl = bpy.context.view_layer
	for o in vl.objects:
		o.select_set(False)
	for o in objs:
		o.select_set(True)
	vl.objects.active = objs[0]
	bpy.ops.object.join()
	ob = vl.objects.active
	ob.name = name
	ob.data.name = name
	return ob


def metaball(name, elements, resolution=0.03, threshold=0.6):
	"""클레이처럼 서로 녹아 붙는 덩어리. elements: [(type, co, radius, size_xyz, rot_deg)]
	type: 'BALL' | 'ELLIPSOID' | 'CAPSULE' | 'CUBE'. 메쉬로 변환해 돌려준다."""
	mb = bpy.data.metaballs.new(name)
	mb.resolution = resolution
	mb.render_resolution = resolution
	mb.threshold = threshold
	for (t, co, rad, size, rot) in elements:
		e = mb.elements.new(type=t)
		e.co = co
		e.radius = rad
		if size is not None:
			e.size_x, e.size_y, e.size_z = size
		if rot is not None:
			e.rotation = Euler([math.radians(a) for a in rot], 'XYZ').to_quaternion()
	ob = bpy.data.objects.new(name, mb)
	link(ob)
	set_active(ob)
	bpy.ops.object.convert(target='MESH')
	out = bpy.context.view_layer.objects.active
	out.name = name
	for p in out.data.polygons:
		p.use_smooth = True
	return out


def add_material(ob, mat):
	ob.data.materials.append(mat)
	return len(ob.data.materials) - 1


def set_faces_material(ob, idx, pred=None):
	"""pred(face_center, face) 가 참인 면에 머티리얼 인덱스. pred 없으면 전부."""
	me = ob.data
	for p in me.polygons:
		if pred is None or pred(p.center, p):
			p.material_index = idx


# =============================================================================
# 3. 머티리얼
# =============================================================================
#
# 소스 머티리얼은 프로시저럴 노드로 네 가지 채널을 만든다.
#   OUT_color   베이스 컬러 (리니어)
#   OUT_rough   러프니스
#   OUT_hmacro  매크로 높이 (0..1)  -> 노멀맵으로 변환
#   OUT_hmicro  마이크로 높이 (0..1) -> 범프맵 그대로
# 베이크 뒤에는 같은 이름의 머티리얼을 이미지 텍스처 기반 PBR 로 다시 짠다.
# 채널별 이미지 하나를 오브젝트의 모든 머티리얼 슬롯이 같이 쓴다 (UV 하나).

MAT_SPECS = {}   # 머티리얼 이름 -> 스펙 (최종 머티리얼의 스칼라 속성)


def _n(nt, typ, x=0, y=0, name=None):
	n = nt.nodes.new(typ)
	n.location = (x, y)
	if name:
		n.name = name
		n.label = name
	return n


def _pattern(nt, coord, spec, x, y):
	"""높이 패턴 하나. 반환: 0..1 범위 출력 소켓."""
	if spec is None:
		v = _n(nt, 'ShaderNodeValue', x, y)
		v.outputs[0].default_value = 0.5
		return v.outputs[0]
	kind = spec[0]
	scale = spec[1]
	if kind == 'noise':
		t = _n(nt, 'ShaderNodeTexNoise', x, y)
		t.inputs['Scale'].default_value = scale
		t.inputs['Detail'].default_value = spec[2] if len(spec) > 2 else 4.0
		t.inputs['Roughness'].default_value = spec[3] if len(spec) > 3 else 0.55
		nt.links.new(coord, t.inputs['Vector'])
		return t.outputs['Fac']
	if kind in ('cells', 'curls', 'pebbles'):
		t = _n(nt, 'ShaderNodeTexVoronoi', x, y)
		t.inputs['Scale'].default_value = scale
		if kind == 'cells':
			t.feature = 'DISTANCE_TO_EDGE'
		else:
			t.feature = 'F1'
		nt.links.new(coord, t.inputs['Vector'])
		out = t.outputs['Distance']
		m = _n(nt, 'ShaderNodeMath', x + 180, y)
		if kind == 'curls':
			# 둥근 고리: 거리의 사인. 송로 개의 곱슬털
			m.operation = 'SINE'
			mm = _n(nt, 'ShaderNodeMath', x + 90, y)
			mm.operation = 'MULTIPLY'
			mm.inputs[1].default_value = 9.0
			nt.links.new(out, mm.inputs[0])
			nt.links.new(mm.outputs[0], m.inputs[0])
			r = _n(nt, 'ShaderNodeMapRange', x + 270, y)
			r.inputs['From Min'].default_value = -1.0
			r.inputs['From Max'].default_value = 1.0
			nt.links.new(m.outputs[0], r.inputs['Value'])
			return r.outputs['Result']
		if kind == 'pebbles':
			# 볼록한 자갈: 1 - 거리^2
			m.operation = 'POWER'
			m.inputs[1].default_value = 2.0
			nt.links.new(out, m.inputs[0])
			inv = _n(nt, 'ShaderNodeMath', x + 270, y)
			inv.operation = 'SUBTRACT'
			inv.inputs[0].default_value = 1.0
			inv.use_clamp = True
			nt.links.new(m.outputs[0], inv.inputs[1])
			return inv.outputs[0]
		# cells: 가장자리에서 홈
		m.operation = 'MULTIPLY'
		m.inputs[1].default_value = spec[2] if len(spec) > 2 else 6.0
		m.use_clamp = True
		nt.links.new(out, m.inputs[0])
		return m.outputs[0]
	if kind == 'wave':
		t = _n(nt, 'ShaderNodeTexWave', x, y)
		t.inputs['Scale'].default_value = scale
		t.inputs['Distortion'].default_value = spec[2] if len(spec) > 2 else 2.0
		t.inputs['Detail'].default_value = 2.0
		t.bands_direction = spec[3] if len(spec) > 3 else 'Z'
		nt.links.new(coord, t.inputs['Vector'])
		return t.outputs['Fac']
	if kind == 'rings':
		t = _n(nt, 'ShaderNodeTexWave', x, y)
		t.wave_type = 'RINGS'
		t.rings_direction = spec[3] if len(spec) > 3 else 'Z'
		t.inputs['Scale'].default_value = scale
		t.inputs['Distortion'].default_value = spec[2] if len(spec) > 2 else 3.0
		nt.links.new(coord, t.inputs['Vector'])
		return t.outputs['Fac']
	if kind == 'grain':
		# 나뭇결: 한 축으로 늘인 노이즈
		mp = _n(nt, 'ShaderNodeMapping', x - 120, y)
		ax = spec[2] if len(spec) > 2 else 'Z'
		sc = {'X': (0.08, 1, 1), 'Y': (1, 0.08, 1), 'Z': (1, 1, 0.08)}[ax]
		mp.inputs['Scale'].default_value = sc
		nt.links.new(coord, mp.inputs['Vector'])
		t = _n(nt, 'ShaderNodeTexNoise', x, y)
		t.inputs['Scale'].default_value = scale
		t.inputs['Detail'].default_value = 6.0
		nt.links.new(mp.outputs[0], t.inputs['Vector'])
		return t.outputs['Fac']
	raise ValueError(kind)


def proc_mat(name, base, base2=None, var_scale=4.0, var_amt=0.0,
		rough=0.6, rough_var=0.1, macro=None, macro_amt=1.0, micro=None, cavity=0.25,
		grad_z=None, sss=0.0, sss_radius=(0.3, 0.12, 0.08), coat=0.0, coat_rough=0.1,
		sheen=0.0, metallic=0.0, emission=None, spec=0.5, bump_strength=0.25,
		normal_strength=1.0, height_scale=0.012, tint_bottom=None, transmission=0.0,
		alpha=1.0, var_edge=(0.35, 0.65)):
	"""프로시저럴 소스 머티리얼. 스칼라 속성은 MAT_SPECS 에 남겨 최종 머티리얼이 쓴다."""
	m = bpy.data.materials.new(name)
	nt = m.node_tree
	for n in list(nt.nodes):
		nt.nodes.remove(n)
	out = _n(nt, 'ShaderNodeOutputMaterial', 1400, 0, 'Out')
	bsdf = _n(nt, 'ShaderNodeBsdfPrincipled', 1100, 0, 'BSDF')
	nt.links.new(bsdf.outputs[0], out.inputs['Surface'])
	tc = _n(nt, 'ShaderNodeTexCoord', -900, 0)
	coord = tc.outputs['Object']

	# 높이
	def macro_h(c, x, y, name=None):
		raw = _pattern(nt, c, macro, x, y)
		h = _n(nt, 'ShaderNodeMath', x + 400, y, name)
		h.operation = 'MULTIPLY_ADD'
		h.inputs[1].default_value = macro_amt
		h.inputs[2].default_value = 0.5 * (1.0 - macro_amt)
		h.use_clamp = True
		nt.links.new(raw, h.inputs[0])
		return h

	hm = macro_h(coord, -600, 200, 'OUT_hmacro')
	hmic = _pattern(nt, coord, micro, -600, -300)

	# 매크로 높이 -> 셰이딩 노멀 (3D 유한차분).
	# Bump 노드는 화면 미분을 쓰기 때문에 베이크에서는 0 이 된다. 높이장을
	# 오브젝트 공간에서 직접 미분하면 UV 섬 경계와 무관한 노멀이 나오고,
	# 이걸 NORMAL(탄젠트) 로 구우면 그대로 노멀맵이 된다.
	geo = _n(nt, 'ShaderNodeNewGeometry', -900, -1200)
	N = geo.outputs['Normal']
	if macro is not None:
		eps = 0.0015
		comps = []
		for k, off in enumerate(((eps, 0, 0), (0, eps, 0), (0, 0, eps))):
			va = _n(nt, 'ShaderNodeVectorMath', -800, -1500 - k * 300)
			va.operation = 'ADD'
			va.inputs[1].default_value = off
			nt.links.new(coord, va.inputs[0])
			hk = macro_h(va.outputs[0], -600, -1500 - k * 300)
			d = _n(nt, 'ShaderNodeMath', 0, -1500 - k * 300)
			d.operation = 'SUBTRACT'
			nt.links.new(hk.outputs[0], d.inputs[0])
			nt.links.new(hm.outputs[0], d.inputs[1])
			sc = _n(nt, 'ShaderNodeMath', 150, -1500 - k * 300)
			sc.operation = 'MULTIPLY'
			sc.inputs[1].default_value = height_scale * normal_strength / eps
			nt.links.new(d.outputs[0], sc.inputs[0])
			comps.append(sc.outputs[0])
		g = _n(nt, 'ShaderNodeCombineXYZ', 300, -1500)
		for k, c in enumerate(comps):
			nt.links.new(c, g.inputs[k])
		# g_t = g - (g.N) N ; n' = normalize(N - g_t)
		dt = _n(nt, 'ShaderNodeVectorMath', 450, -1400)
		dt.operation = 'DOT_PRODUCT'
		nt.links.new(g.outputs[0], dt.inputs[0])
		nt.links.new(N, dt.inputs[1])
		sn = _n(nt, 'ShaderNodeVectorMath', 600, -1400)
		sn.operation = 'SCALE'
		nt.links.new(N, sn.inputs[0])
		nt.links.new(dt.outputs['Value'], sn.inputs['Scale'])
		gt = _n(nt, 'ShaderNodeVectorMath', 750, -1450)
		gt.operation = 'SUBTRACT'
		nt.links.new(g.outputs[0], gt.inputs[0])
		nt.links.new(sn.outputs[0], gt.inputs[1])
		np_ = _n(nt, 'ShaderNodeVectorMath', 900, -1400)
		np_.operation = 'SUBTRACT'
		nt.links.new(N, np_.inputs[0])
		nt.links.new(gt.outputs[0], np_.inputs[1])
		nn = _n(nt, 'ShaderNodeVectorMath', 1050, -1400, 'OUT_nrm')
		nn.operation = 'NORMALIZE'
		nt.links.new(np_.outputs[0], nn.inputs[0])
	else:
		nn = _n(nt, 'ShaderNodeVectorMath', 1050, -1400, 'OUT_nrm')
		nn.operation = 'NORMALIZE'
		nt.links.new(N, nn.inputs[0])
	hu = _n(nt, 'ShaderNodeMath', -200, -300, 'OUT_hmicro')
	hu.operation = 'ADD'
	hu.inputs[1].default_value = 0.0
	hu.use_clamp = True
	nt.links.new(hmic, hu.inputs[0])

	# 색: base 와 base2 를 노이즈로 섞고, 오목한 곳을 어둡게 (캐비티)
	var = _n(nt, 'ShaderNodeTexNoise', -600, 600)
	var.inputs['Scale'].default_value = var_scale
	var.inputs['Detail'].default_value = 3.0
	nt.links.new(coord, var.inputs['Vector'])
	vr = _n(nt, 'ShaderNodeMapRange', -400, 600)
	vr.inputs['From Min'].default_value = var_edge[0]
	vr.inputs['From Max'].default_value = var_edge[1]
	vr.inputs['To Max'].default_value = var_amt
	nt.links.new(var.outputs['Fac'], vr.inputs['Value'])
	mix = _n(nt, 'ShaderNodeMix', -200, 600)
	mix.data_type = 'RGBA'
	mix.inputs['A'].default_value = (*base, 1.0)
	mix.inputs['B'].default_value = (*(base2 or base), 1.0)
	nt.links.new(vr.outputs['Result'], mix.inputs['Factor'])
	col = mix.outputs['Result']

	if grad_z is not None:
		# 오브젝트 Z 기준 명도 그라디언트 (아래 어둡게). grad_z = (z0, z1, k0, k1)
		sep = _n(nt, 'ShaderNodeSeparateXYZ', -600, 900)
		nt.links.new(coord, sep.inputs[0])
		gr = _n(nt, 'ShaderNodeMapRange', -400, 900)
		gr.inputs['From Min'].default_value = grad_z[0]
		gr.inputs['From Max'].default_value = grad_z[1]
		gr.inputs['To Min'].default_value = grad_z[2]
		gr.inputs['To Max'].default_value = grad_z[3]
		nt.links.new(sep.outputs['Z'], gr.inputs['Value'])
		mul = _n(nt, 'ShaderNodeMix', 0, 800)
		mul.data_type = 'RGBA'
		mul.blend_type = 'MULTIPLY'
		mul.inputs['Factor'].default_value = 1.0
		nt.links.new(col, mul.inputs['A'])
		cg = _n(nt, 'ShaderNodeCombineColor', -200, 900)
		for k in ('Red', 'Green', 'Blue'):
			nt.links.new(gr.outputs['Result'], cg.inputs[k])
		if tint_bottom is not None:
			pass
		nt.links.new(cg.outputs[0], mul.inputs['B'])
		col = mul.outputs['Result']

	if cavity > 0.0 and macro is not None:
		cr = _n(nt, 'ShaderNodeMapRange', 100, 450)
		cr.inputs['To Min'].default_value = 1.0 - cavity
		cr.inputs['To Max'].default_value = 1.0
		nt.links.new(hm.outputs[0], cr.inputs['Value'])
		cc = _n(nt, 'ShaderNodeCombineColor', 250, 450)
		for k in ('Red', 'Green', 'Blue'):
			nt.links.new(cr.outputs['Result'], cc.inputs[k])
		mul2 = _n(nt, 'ShaderNodeMix', 400, 600)
		mul2.data_type = 'RGBA'
		mul2.blend_type = 'MULTIPLY'
		mul2.inputs['Factor'].default_value = 1.0
		nt.links.new(col, mul2.inputs['A'])
		nt.links.new(cc.outputs[0], mul2.inputs['B'])
		col = mul2.outputs['Result']

	cout = _n(nt, 'ShaderNodeMix', 600, 600, 'OUT_color')
	cout.data_type = 'RGBA'
	cout.inputs['Factor'].default_value = 0.0
	nt.links.new(col, cout.inputs['A'])

	# 러프니스
	rn = _n(nt, 'ShaderNodeTexNoise', -600, -700)
	rn.inputs['Scale'].default_value = var_scale * 2.3
	nt.links.new(coord, rn.inputs['Vector'])
	rr = _n(nt, 'ShaderNodeMapRange', -300, -700, 'OUT_rough')
	rr.inputs['To Min'].default_value = max(0.0, rough - rough_var)
	rr.inputs['To Max'].default_value = min(1.0, rough + rough_var)
	rr.inputs['From Min'].default_value = 0.3
	rr.inputs['From Max'].default_value = 0.7
	nt.links.new(rn.outputs['Fac'], rr.inputs['Value'])

	# 소스 상태에서도 미리보기가 되게 연결해 둔다
	nt.links.new(cout.outputs['Result'], bsdf.inputs['Base Color'])
	nt.links.new(rr.outputs['Result'], bsdf.inputs['Roughness'])

	MAT_SPECS[name] = dict(sss=sss, sss_radius=sss_radius, coat=coat, coat_rough=coat_rough,
		sheen=sheen, metallic=metallic, emission=emission, spec=spec,
		bump_strength=bump_strength, normal_strength=normal_strength,
		height_scale=height_scale, transmission=transmission, alpha=alpha,
		has_macro=macro is not None, has_micro=micro is not None)
	_apply_scalars(m, bsdf)
	return m


def _apply_scalars(m, bsdf):
	sp = MAT_SPECS[m.name]
	bsdf.inputs['Metallic'].default_value = sp['metallic']
	bsdf.inputs['Specular IOR Level'].default_value = sp['spec']
	bsdf.inputs['Subsurface Weight'].default_value = sp['sss']
	bsdf.inputs['Subsurface Radius'].default_value = sp['sss_radius']
	bsdf.inputs['Subsurface Scale'].default_value = 0.05
	bsdf.inputs['Coat Weight'].default_value = sp['coat']
	bsdf.inputs['Coat Roughness'].default_value = sp['coat_rough']
	bsdf.inputs['Sheen Weight'].default_value = sp['sheen']
	bsdf.inputs['Transmission Weight'].default_value = sp['transmission']
	bsdf.inputs['Alpha'].default_value = sp['alpha']
	if sp['emission'] is not None:
		c, st = sp['emission']
		bsdf.inputs['Emission Color'].default_value = (*c, 1.0)
		bsdf.inputs['Emission Strength'].default_value = st


def smart_uv(ob, margin=0.006):
	set_active(ob)
	bpy.ops.object.mode_set(mode='EDIT')
	bpy.ops.mesh.select_all(action='SELECT')
	bpy.ops.uv.smart_project(angle_limit=math.radians(55.0), island_margin=margin,
		area_weight=0.0, correct_aspect=True, scale_to_bounds=False)
	bpy.ops.object.mode_set(mode='OBJECT')


def _uv_stats(ob):
	me = ob.data
	uv = me.uv_layers.active.data
	area3 = 0.0
	area2 = 0.0
	for p in me.polygons:
		area3 += p.area
		pts = [uv[li].uv for li in p.loop_indices]
		a = 0.0
		for i in range(len(pts)):
			x0, y0 = pts[i]
			x1, y1 = pts[(i + 1) % len(pts)]
			a += x0 * y1 - x1 * y0
		area2 += abs(a) * 0.5
	return area3, max(area2, 1e-6)


def _bake_emit(ob, mats, socket_name, res, samples=8):
	"""모든 소스 머티리얼의 OUT_<socket> 을 에미션으로 걸고 float 이미지에 굽는다."""
	img = bpy.data.images.new('bake_' + socket_name, res, res, alpha=True, float_buffer=True)
	img.colorspace_settings.name = 'Non-Color'
	for m in mats:
		nt = m.node_tree
		src = nt.nodes.get('OUT_' + socket_name)
		em = nt.nodes.get('BAKE_EMIT') or _n(nt, 'ShaderNodeEmission', 1100, -400, 'BAKE_EMIT')
		for l in list(em.inputs['Color'].links):
			nt.links.remove(l)
		so = src.outputs['Result'] if 'Result' in src.outputs else src.outputs[0]
		nt.links.new(so, em.inputs['Color'])
		nt.links.new(em.outputs[0], nt.nodes['Out'].inputs['Surface'])
		tn = nt.nodes.get('BAKE_IMG') or _n(nt, 'ShaderNodeTexImage', 1100, -700, 'BAKE_IMG')
		tn.image = img
		nt.nodes.active = tn
	s = bpy.context.scene
	old = s.cycles.samples
	s.cycles.samples = samples
	s.render.bake.margin = 10
	s.render.bake.margin_type = 'EXTEND'
	set_active(ob)
	bpy.ops.object.bake(type='EMIT', margin=10, use_clear=True)
	s.cycles.samples = old
	a = np.array(img.pixels[:], dtype=np.float32).reshape(res, res, 4)
	bpy.data.images.remove(img)
	return a


def _bake_normal(ob, mats, res, samples=4):
	"""OUT_nrm 을 BSDF 노멀에 걸고 탄젠트 공간 노멀맵으로 굽는다."""
	img = bpy.data.images.new('bake_nrm', res, res, alpha=True, float_buffer=True)
	img.colorspace_settings.name = 'Non-Color'
	for m in mats:
		nt = m.node_tree
		bsdf = nt.nodes['BSDF']
		nt.links.new(nt.nodes['OUT_nrm'].outputs[0], bsdf.inputs['Normal'])
		nt.links.new(bsdf.outputs[0], nt.nodes['Out'].inputs['Surface'])
		tn = nt.nodes.get('BAKE_IMG') or _n(nt, 'ShaderNodeTexImage', 1100, -700, 'BAKE_IMG')
		tn.image = img
		nt.nodes.active = tn
	s = bpy.context.scene
	old = s.cycles.samples
	s.cycles.samples = samples
	set_active(ob)
	bpy.ops.object.bake(type='NORMAL', normal_space='TANGENT', margin=10, use_clear=True)
	s.cycles.samples = old
	a = np.array(img.pixels[:], dtype=np.float32).reshape(res, res, 4)
	bpy.data.images.remove(img)
	return a


def _srgb(x):
	x = np.clip(x, 0.0, 1.0)
	return np.where(x <= 0.0031308, x * 12.92, 1.055 * np.power(x, 1.0 / 2.4) - 0.055)


def _save_png(path, arr_bl):
	"""arr_bl: (H, W, C) 블렌더 행 순서(아래가 0). 0..1"""
	from PIL import Image
	a = np.flipud(np.clip(arr_bl, 0.0, 1.0))
	a = (a * 255.0 + 0.5).astype(np.uint8)
	mode = {1: 'L', 3: 'RGB', 4: 'RGBA'}[a.shape[2] if a.ndim == 3 else 1]
	if a.ndim == 3 and a.shape[2] == 1:
		a = a[..., 0]
	Image.fromarray(a, mode).save(path, optimize=True)


def bake_asset_textures(ob, asset, res=1024):
	"""ob 의 모든 소스 머티리얼을 이미지 텍스처 세트로 굽고 최종 PBR 머티리얼로 교체.

	출력: textures/<asset>/<asset>_{basecolor,roughness,height,normal,bump}.png"""
	tdir = ensure_dir(os.path.join(ROOT, 'textures', asset))
	if not ob.data.uv_layers:
		smart_uv(ob)
	mats = [m for m in ob.data.materials if m is not None and m.name in MAT_SPECS
		and m.node_tree.nodes.get('OUT_color') is not None]
	if not mats:
		return

	col = _bake_emit(ob, mats, 'color', res)
	rough = _bake_emit(ob, mats, 'rough', res)
	hmac = _bake_emit(ob, mats, 'hmacro', res)
	hmic = _bake_emit(ob, mats, 'hmicro', res)

	nrm = _bake_normal(ob, mats, res)[..., :3]

	_save_png(os.path.join(tdir, asset + '_basecolor.png'), _srgb(col[..., :3]))
	_save_png(os.path.join(tdir, asset + '_roughness.png'), rough[..., :1])
	_save_png(os.path.join(tdir, asset + '_height.png'), hmac[..., :1])
	_save_png(os.path.join(tdir, asset + '_normal.png'), nrm)
	_save_png(os.path.join(tdir, asset + '_bump.png'), hmic[..., :1])

	def load(kind, color):
		img = bpy.data.images.load(os.path.join(tdir, '%s_%s.png' % (asset, kind)), check_existing=True)
		img.colorspace_settings.name = 'sRGB' if color else 'Non-Color'
		return img

	imgs = dict(basecolor=load('basecolor', True), roughness=load('roughness', False),
		normal=load('normal', False), bump=load('bump', False))
	for m in mats:
		_final_mat(m, imgs)


def _final_mat(m, imgs):
	"""소스 노드를 지우고 이미지 텍스처 기반으로 다시 짠다."""
	sp = MAT_SPECS[m.name]
	nt = m.node_tree
	for n in list(nt.nodes):
		nt.nodes.remove(n)
	out = _n(nt, 'ShaderNodeOutputMaterial', 900, 0, 'Out')
	bsdf = _n(nt, 'ShaderNodeBsdfPrincipled', 600, 0, 'BSDF')
	nt.links.new(bsdf.outputs[0], out.inputs['Surface'])
	uv = _n(nt, 'ShaderNodeUVMap', -700, 0)

	def tex(img, x, y, label):
		t = _n(nt, 'ShaderNodeTexImage', x, y, label)
		t.image = img
		t.interpolation = 'Linear'
		nt.links.new(uv.outputs[0], t.inputs['Vector'])
		return t

	tb = tex(imgs['basecolor'], -400, 300, 'BaseColor')
	tr = tex(imgs['roughness'], -400, 0, 'Roughness')
	tn = tex(imgs['normal'], -400, -300, 'Normal')
	tu = tex(imgs['bump'], -400, -600, 'Bump')
	nt.links.new(tb.outputs['Color'], bsdf.inputs['Base Color'])
	nt.links.new(tr.outputs['Color'], bsdf.inputs['Roughness'])
	nm = _n(nt, 'ShaderNodeNormalMap', -100, -300, 'NormalMap')
	nm.inputs['Strength'].default_value = 1.0 if sp['has_macro'] else 0.0
	nt.links.new(tn.outputs['Color'], nm.inputs['Color'])
	bp = _n(nt, 'ShaderNodeBump', 200, -450, 'BumpMap')
	bp.inputs['Strength'].default_value = sp['bump_strength'] if sp['has_micro'] else 0.0
	bp.inputs['Distance'].default_value = 0.01
	nt.links.new(tu.outputs['Color'], bp.inputs['Height'])
	nt.links.new(nm.outputs['Normal'], bp.inputs['Normal'])
	nt.links.new(bp.outputs['Normal'], bsdf.inputs['Normal'])
	_apply_scalars(m, bsdf)


def simple_mat(name, color, rough=0.4, emission=None, coat=0.0, spec=0.5, metallic=0.0,
		sss=0.0):
	"""베이크하지 않는 단색 머티리얼 (눈, 유리, 하이라이트 같은 작은 부품)."""
	m = bpy.data.materials.new(name)
	b = m.node_tree.nodes.get('Principled BSDF')
	b.inputs['Base Color'].default_value = (*color, 1.0)
	b.inputs['Roughness'].default_value = rough
	b.inputs['Coat Weight'].default_value = coat
	b.inputs['Specular IOR Level'].default_value = spec
	b.inputs['Metallic'].default_value = metallic
	b.inputs['Subsurface Weight'].default_value = sss
	if emission:
		b.inputs['Emission Color'].default_value = (*emission[0], 1.0)
		b.inputs['Emission Strength'].default_value = emission[1]
	return m


# =============================================================================
# 4. 리그
# =============================================================================

def make_armature(name, bones):
	"""bones: [(name, head, tail, parent, deform, roll_deg, connect)]"""
	ad = bpy.data.armatures.new(name)
	ad.display_type = 'OCTAHEDRAL'
	ob = bpy.data.objects.new(name, ad)
	link(ob)
	ob.show_in_front = True
	set_active(ob)
	bpy.ops.object.mode_set(mode='EDIT')
	eb = ad.edit_bones
	for (bn, h, t, par, deform, roll, conn) in bones:
		b = eb.new(bn)
		b.head = h
		b.tail = t
		b.roll = math.radians(roll)
		b.use_deform = deform
		if par:
			b.parent = eb[par]
			b.use_connect = conn
	bpy.ops.object.mode_set(mode='OBJECT')
	for pb in ob.pose.bones:
		pb.rotation_mode = 'XYZ'
	return ob


def _seg_dist(p, a, b):
	ab = b - a
	t = max(0.0, min(1.0, (p - a).dot(ab) / max(ab.length_squared, 1e-9)))
	return (p - (a + ab * t)).length


def skin(ob, arm, candidates=None, power=6.0, max_inf=2, only=None, side_split=False):
	"""거리 기반 스키닝. 뼈 선분까지 거리의 역수^power 를 정규화한다.
	candidates: 후보 뼈 이름 목록 (없으면 변형 뼈 전부).
	only: 지정하면 해당 뼈 하나에 1.0 (머리·눈 같은 강체 부품)."""
	bones = arm.data.bones
	if only:
		vg = ob.vertex_groups.get(only) or ob.vertex_groups.new(name=only)
		vg.add([v.index for v in ob.data.vertices], 1.0, 'REPLACE')
		return
	names = candidates or [b.name for b in bones if b.use_deform]
	segs = []
	mw = arm.matrix_world
	for n in names:
		b = bones[n]
		segs.append((n, mw @ b.head_local, mw @ b.tail_local))
	groups = {n: (ob.vertex_groups.get(n) or ob.vertex_groups.new(name=n)) for n in names}
	om = ob.matrix_world
	for v in ob.data.vertices:
		p = om @ v.co
		ws = []
		for (n, a, b) in segs:
			if side_split:
				# 좌우 대칭 캐릭터: 반대쪽 팔다리 뼈는 후보에서 뺀다
				if p.x > 0.01 and n.endswith('.R'):
					continue
				if p.x < -0.01 and n.endswith('.L'):
					continue
			d = max(_seg_dist(p, a, b), 1e-4)
			ws.append((1.0 / d ** power, n))
		ws.sort(reverse=True)
		ws = ws[:max_inf]
		tot = sum(w for w, _ in ws)
		for w, n in ws:
			if w / tot > 0.01:
				groups[n].add([v.index], w / tot, 'REPLACE')


def bind(ob, arm):
	ob.parent = arm
	m = ob.modifiers.new('Armature', 'ARMATURE')
	m.object = arm


def ik(arm, bone, target, pole=None, chain=2, pole_angle=-90.0):
	pb = arm.pose.bones[bone]
	c = pb.constraints.new('IK')
	c.target = arm
	c.subtarget = target
	if pole:
		c.pole_target = arm
		c.pole_subtarget = pole
		c.pole_angle = math.radians(pole_angle)
	c.chain_count = chain
	return c


def copy_rot(arm, bone, target, space='WORLD'):
	c = arm.pose.bones[bone].constraints.new('COPY_ROTATION')
	c.target = arm
	c.subtarget = target
	c.target_space = space
	c.owner_space = space
	return c


# =============================================================================
# 5. 애니메이션
# =============================================================================

def new_action(ob, name):
	if ob.animation_data is None:
		ob.animation_data_create()
	act = bpy.data.actions.new(name)
	act.use_fake_user = True
	ob.animation_data.action = act
	return act


def use_action(ob, act):
	# 액션이 키를 넣지 않은 채널은 직전 포즈가 남는다. 매번 레스트에서 시작한다
	if ob.type == 'ARMATURE':
		reset_pose(ob)
	ob.animation_data.action = act
	try:
		if ob.animation_data.action_slot is None and len(act.slots) > 0:
			ob.animation_data.action_slot = act.slots[0]
	except AttributeError:
		pass


def reset_pose(arm):
	for pb in arm.pose.bones:
		pb.location = (0, 0, 0)
		pb.rotation_euler = (0, 0, 0)
		pb.scale = (1, 1, 1)


def key(arm, frame, bone, rot=None, loc=None, scale=None):
	"""rot 은 도(degree), 뼈 로컬 XYZ 오일러."""
	pb = arm.pose.bones[bone]
	if rot is not None:
		pb.rotation_euler = [math.radians(a) for a in rot]
		pb.keyframe_insert('rotation_euler', frame=frame)
	if loc is not None:
		pb.location = loc
		pb.keyframe_insert('location', frame=frame)
	if scale is not None:
		pb.scale = scale
		pb.keyframe_insert('scale', frame=frame)


def key_cycle(arm, bone, frames, fn, channels=('rot',)):
	"""fn(t) -> dict(rot=..., loc=..., scale=...) t 는 0..1. 마지막 프레임 = 처음 (루프)."""
	n = len(frames)
	for i, f in enumerate(frames):
		d = fn(i / (n - 1) if n > 1 else 0.0)
		key(arm, f, bone, d.get('rot'), d.get('loc'), d.get('scale'))


def anim_fn(arm, name, f0, f1, step, bone_fns):
	"""bone_fns: {bone: fn(t)->dict}. t = 0..1 (f0..f1). step 프레임마다 키."""
	act = new_action(arm, name)
	reset_pose(arm)
	frames = list(range(f0, f1 + 1, step))
	if frames[-1] != f1:
		frames.append(f1)
	for bone, fn in bone_fns.items():
		for f in frames:
			t = (f - f0) / float(f1 - f0)
			d = fn(t)
			key(arm, f, bone, d.get('rot'), d.get('loc'), d.get('scale'))
	act.frame_range = (f0, f1)
	try:
		act.use_frame_range = True
	except AttributeError:
		pass
	return act


# =============================================================================
# 6. 스프라이트 렌더
# =============================================================================

def iso_camera(canvas_w, canvas_h, pivot_x, pivot_y, scale=SCALE, target=(0, 0, 0)):
	"""canvas: 스프라이트 크기 (px, scale 배율 적용 후). pivot: 월드 원점(발)이 놓일 픽셀.

	1 BU 짜리 바닥 정사각형이 정확히 72x36 * scale 마름모로 찍힌다."""
	s = bpy.context.scene
	s.render.resolution_x = canvas_w
	s.render.resolution_y = canvas_h
	cam = bpy.data.objects.get('GG_Cam')
	if cam is None:
		cd = bpy.data.cameras.new('GG_Cam')
		cam = bpy.data.objects.new('GG_Cam', cd)
		link(cam)
	cd = cam.data
	cd.type = 'ORTHO'
	maxd = float(max(canvas_w, canvas_h))
	cd.ortho_scale = maxd / (PX_PER_BU * scale)
	cd.sensor_fit = 'AUTO'
	cd.shift_x = -(pivot_x - canvas_w * 0.5) / maxd
	cd.shift_y = (pivot_y - canvas_h * 0.5) / maxd
	cd.clip_start = 0.1
	cd.clip_end = 200.0
	cam.rotation_euler = CAM_ROT
	fwd = CAM_ROT.to_matrix() @ Vector((0, 0, -1))
	cam.location = Vector(target) - fwd * 40.0
	s.camera = cam
	return cam


def render_exr(path):
	s = bpy.context.scene
	isf = s.render.image_settings
	isf.media_type = 'MULTI_LAYER_IMAGE'
	isf.file_format = 'OPEN_EXR_MULTILAYER'
	isf.color_depth = '16'
	isf.exr_codec = 'ZIP'
	bpy.context.view_layer.use_pass_normal = True
	s.render.filepath = path
	bpy.ops.render.render(write_still=True)


def exr_to_sprites(path):
	"""EXR -> (color RGBA uint8 straight alpha, normal RGBA uint8). 행: 위가 0."""
	import OpenEXR
	f = OpenEXR.File(path)
	comb = None
	nx = ny = nz = None
	for p in f.parts:
		for k, v in p.channels.items():
			if k.endswith('.Combined'):
				comb = v.pixels.astype(np.float32)
			elif k.endswith('Normal.X'):
				nx = v.pixels.astype(np.float32)
			elif k.endswith('Normal.Y'):
				ny = v.pixels.astype(np.float32)
			elif k.endswith('Normal.Z'):
				nz = v.pixels.astype(np.float32)
	a = np.clip(comb[..., 3], 0.0, 1.0)
	rgb = comb[..., :3] / np.maximum(a[..., None], 1e-4)
	rgb = _srgb(rgb)
	col = np.dstack([rgb, a])
	# 월드 노멀 -> 카메라 공간 (x 오른쪽, y 위, z 화면 밖)
	R = CAM_ROT.to_matrix()
	Rt = np.array(R, dtype=np.float32).T
	nw = np.stack([nx, ny, nz], axis=-1)
	nc = nw @ Rt.T
	ln = np.linalg.norm(nc, axis=-1, keepdims=True)
	nc = np.where(ln > 1e-4, nc / np.maximum(ln, 1e-4), np.array([0, 0, 1], np.float32))
	nrm = np.dstack([nc * 0.5 + 0.5, a])
	nrm[a < 0.004, :3] = (0.5, 0.5, 1.0)
	to8 = lambda x: (np.clip(x, 0, 1) * 255.0 + 0.5).astype(np.uint8)
	return to8(col), to8(nrm)


def save_rgba(path, arr8):
	from PIL import Image
	Image.fromarray(arr8, 'RGBA').save(path, optimize=True)


def render_sprite(out_dir, name, keep_exr=False):
	"""현재 씬을 렌더해 <name>.png (컬러)와 <name>_n.png (노멀)를 쓴다."""
	ensure_dir(out_dir)
	exr = os.path.join(out_dir, name + '.exr')
	render_exr(exr)
	c, n = exr_to_sprites(exr)
	save_rgba(os.path.join(out_dir, name + '.png'), c)
	save_rgba(os.path.join(out_dir, name + '_n.png'), n)
	if not keep_exr:
		os.remove(exr)


def write_sprite_meta(out_dir, meta):
	with open(os.path.join(out_dir, 'meta.json'), 'w') as f:
		json.dump(meta, f, indent=1, ensure_ascii=False)


def render_character(root_obj, arm, asset, canvas, pivot, anims, out_root=None, scale=SCALE):
	"""anims: [(anim_name, action, frames_list)]. 5방향 x 각 프레임.
	root_obj: Z 회전으로 방향을 바꿀 최상위 오브젝트 (보통 아마추어)."""
	out_dir = ensure_dir(os.path.join(out_root or os.path.join(ROOT, 'renders'), asset))
	iso_camera(canvas[0], canvas[1], pivot[0], pivot[1], scale=scale)
	s = bpy.context.scene
	meta = dict(asset=asset, canvas=list(canvas), pivot=list(pivot), scale=scale,
		dirs=[d for d, _ in SCREEN_DIRS], anims=[])
	base_rz = root_obj.rotation_euler.z
	for (an, act, frames, fps, loop) in anims:
		use_action(arm, act)
		meta['anims'].append(dict(name=an, frames=len(frames), fps=fps, loop=loop))
		for di, (dn, sv) in enumerate(SCREEN_DIRS):
			d = screen_to_blender_dir(*sv)
			root_obj.rotation_euler.z = base_rz + facing_rot_z(d)
			for fi, fr in enumerate(frames):
				s.frame_set(fr)
				render_sprite(out_dir, '%s.%s.%s.%d' % (asset, an, dn, fi))
	root_obj.rotation_euler.z = base_rz
	write_sprite_meta(out_dir, meta)
	return meta


def render_static(asset, canvas, pivot, name=None, out_root=None, frame=1, scale=SCALE):
	out_dir = ensure_dir(os.path.join(out_root or os.path.join(ROOT, 'renders'), asset))
	iso_camera(canvas[0], canvas[1], pivot[0], pivot[1], scale=scale)
	bpy.context.scene.frame_set(frame)
	nm = name or asset
	render_sprite(out_dir, nm)
	meta = dict(asset=asset, canvas=list(canvas), pivot=list(pivot), scale=scale, name=nm,
		static=True)
	write_sprite_meta(out_dir, meta) if not os.path.exists(os.path.join(out_dir, 'meta.json')) else None
	return meta


def save_blend(asset):
	bdir = ensure_dir(os.path.join(ROOT, 'blend'))
	p = os.path.join(bdir, asset + '.blend')
	# 카메라·조명은 남긴다 — 열었을 때 바로 게임과 같은 화면이 보이게
	bpy.ops.wm.save_as_mainfile(filepath=p)
	bpy.ops.file.make_paths_relative()
	bpy.ops.wm.save_mainfile()
	return p


def contact_sheet(paths, out, cols=8, bg=(70, 66, 62)):
	"""검수용 이어붙인 시트."""
	from PIL import Image
	ims = [Image.open(p).convert('RGBA') for p in paths]
	w = max(i.width for i in ims)
	h = max(i.height for i in ims)
	rows = (len(ims) + cols - 1) // cols
	sheet = Image.new('RGBA', (w * cols, h * rows), (*bg, 255))
	for k, im in enumerate(ims):
		x = (k % cols) * w + (w - im.width) // 2
		y = (k // cols) * h + (h - im.height) // 2
		sheet.alpha_composite(im, (x, y))
	sheet.save(out)


# ---- 메타볼 요소 헬퍼 (threshold 0.6 기준 실측: 표면 = 0.574 * radius) ----
MB_K = 0.574


def mball(co, R):
	return ('BALL', co, R / MB_K, None, None)


def mell(co, half):
	return ('ELLIPSOID', co, 1.0 / MB_K, (half[0], half[1], half[2]), None)


def mcap(p0, p1, R):
	"""p0-p1 선분을 따라 반지름 R 인 캡슐."""
	a = Vector(p0); b = Vector(p1)
	d = b - a
	q = Vector((1, 0, 0)).rotation_difference(d.normalized())
	e = q.to_euler('XYZ')
	return ('CAPSULE', tuple((a + b) * 0.5), R / MB_K, (d.length * 0.5, 1.0, 1.0),
		tuple(math.degrees(x) for x in e))


def lin(c):
	"""sRGB 표시값 -> 리니어 (엔진 색 상수를 그대로 가져오기 위해)."""
	def f(x):
		return x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4
	return tuple(f(x) for x in c)


def raycast_place(target, origin, direction, offset=0.0):
	"""target 메쉬 표면에 origin 에서 direction 으로 쏜 첫 교점 (월드). 노멀도 함께."""
	mw = target.matrix_world
	inv = mw.inverted()
	o = inv @ Vector(origin)
	d = (inv.to_3x3() @ Vector(direction)).normalized()
	ok, loc, nrm, idx = target.ray_cast(o, d)
	if not ok:
		return None, None
	n = (mw.to_3x3() @ nrm).normalized()
	return mw @ loc + n * offset, n


def dominant_group(ob, v):
	best = None
	bw = -1.0
	for g in v.groups:
		if g.weight > bw:
			bw = g.weight
			best = ob.vertex_groups[g.group].name
	return best
