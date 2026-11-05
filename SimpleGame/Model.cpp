#include "stdafx.h"

#include <cstdio>
#include <cmath>
#include <direct.h>

#include "Model.h"

const float HUMAN_ROOT_HEIGHT = 0.0f;

namespace
{
	const char* CACHE_PATH = "./Data/models.bin";
	const int   CACHE_MAGIC = 0x314D4747;   // 'GGM1'
	const int   CACHE_VERSION = 13;

	Model g_Models[MODEL_COUNT];
	bool  g_Loaded = false;

	// ---- 빌드 헬퍼 ---------------------------------------------------------

	int AddJoint(Model& m, int parent, float ox, float oy)
	{
		ModelJoint j;
		j.parent = parent;
		j.ox = ox;
		j.oy = oy;
		m.joints.push_back(j);
		return (int)m.joints.size() - 1;
	}

	void AddPart(Model& m, int joint, int shape,
		float ox, float oy, float w, float h,
		float pvx, float pvy, float radius, float rim,
		int slot, float shadeTop, float shadeBot,
		int order, int flags)
	{
		ModelPart p;
		p.joint    = joint;
		p.shape    = shape;
		p.ox       = ox;
		p.oy       = oy;
		p.w        = w;
		p.h        = h;
		p.pivotX   = pvx;
		p.pivotY   = pvy;
		p.radius   = radius;
		p.rim      = rim;
		p.slot     = slot;
		p.shadeTop = shadeTop;
		p.shadeBot = shadeBot;
		p.fixedTop = Col(1.0f, 1.0f, 1.0f);
		p.fixedBot = Col(1.0f, 1.0f, 1.0f);
		p.order    = order;
		p.flags    = flags;
		m.parts.push_back(p);
	}

	void AddPartFixed(Model& m, int joint, int shape,
		float ox, float oy, float w, float h,
		float pvx, float pvy, float radius, float rim,
		const Col& top, const Col& bot,
		int order, int flags)
	{
		AddPart(m, joint, shape, ox, oy, w, h, pvx, pvy, radius, rim,
			CS_FIXED, 1.0f, 1.0f, order, flags);
		m.parts.back().fixedTop = top;
		m.parts.back().fixedBot = bot;
	}

	void SortByOrder(Model& m)
	{
		// 파트 수가 수십 개라 삽입 정렬이면 충분하고, 같은 order 의 입력 순서가
		// 보존되어 결과가 결정적이다.
		for (size_t i = 1; i < m.parts.size(); ++i)
		{
			ModelPart key = m.parts[i];
			size_t j = i;
			while (j > 0 && m.parts[j - 1].order > key.order)
			{
				m.parts[j] = m.parts[j - 1];
				--j;
			}
			m.parts[j] = key;
		}
	}

	// ---- 사람 ---------------------------------------------------------------
	//
	// 팔다리 뼈가 없다. 프리미티브로 인체를 흉내내면 관절을 쪼갤수록
	// 언캐니 밸리로 들어가고, 타원으로 만든 팔은 아무리 잘 돌려도 대벌레가
	// 된다. 그래서 형태를 단순화했다 — 큰 머리, 둥근 몸통, 팔 없이 떠 있는
	// 손, 다리 없이 발만.
	//
	// 루트는 발바닥에 있다.

	void BuildHuman(Model& m)
	{
		AddJoint(m, -1,       0.0f,   0.0f);   // HJ_ROOT    발바닥
		AddJoint(m, HJ_ROOT,  0.0f, -17.0f);   // HJ_BODY    몸통 중심
		AddJoint(m, HJ_BODY,  0.0f,  -7.0f);   // HJ_HEAD    머리 밑동
		// 손은 몸통 옆구리에 붙인다. 가장 넓은 높이(몸통 중앙)에 바깥으로
		// 두면 양팔을 벌린 실루엣이 되어, 팔이 없는데도 팔을 휘두르는 것처럼
		// 보인다. 안쪽으로 당기고 아래로 내려 허리에 얹힌 모양으로 만든다.
		AddJoint(m, HJ_BODY, -12.5f,  2.0f);   // HJ_HAND_B
		AddJoint(m, HJ_BODY,  12.5f,  2.0f);   // HJ_HAND_F
		AddJoint(m, HJ_ROOT,  -7.0f,  -4.0f);  // HJ_FOOT_B
		AddJoint(m, HJ_ROOT,   7.0f,  -4.0f);  // HJ_FOOT_F

		// 뒤쪽 발과 손 — 몸통보다 먼저 그려 뒤에 깔린다
		AddPart(m, HJ_FOOT_B, PART_ELLIPSE, 0.0f, 0.0f, 15.0f,  9.0f, 0.0f, 0.0f, 0.0f, 0.0f,  CS_SHOE, 0.84f, 0.60f, 0, PF_NONE);
		// 뒤 레이어 — 두 손 중 뒤로 간 쪽이 여기로 온다.
		// 음영은 손의 정체가 아니라 레이어를 따른다. 가까운 손이 더 밝다.
		AddPart(m, HJ_HAND_B, PART_ELLIPSE, 0.0f, 0.0f, 12.0f, 12.0f, 0.0f, 0.0f, 0.0f, 0.10f, CS_SKIN, 0.84f, 0.66f, 0, PF_HANDS_NORMAL);
		AddPart(m, HJ_HAND_F, PART_ELLIPSE, 0.0f, 0.0f, 12.0f, 12.0f, 0.0f, 0.0f, 0.0f, 0.10f, CS_SKIN, 0.84f, 0.66f, 0, PF_HANDS_SWAP);

		AddPart(m, HJ_FOOT_F, PART_ELLIPSE, 0.0f, 0.0f, 15.0f,  9.0f, 0.0f, 0.0f, 0.0f, 0.0f,  CS_SHOE, 1.02f, 0.74f, 1, PF_NONE);

		// 몸통 — 하나의 둥근 덩어리
		AddPart(m, HJ_BODY, PART_ELLIPSE, 0.0f, 0.0f, 31.0f, 29.0f, 0.0f, 0.0f, 0.0f, 0.16f, CS_CLOTH, 1.12f, 0.68f, 2, PF_NONE);

		// 앞치마
		AddPart(m, HJ_BODY, PART_RECT, 0.0f,   3.0f, 24.0f, 21.0f, 0.0f, 0.0f, 0.36f, 0.0f, CS_ACCENT, 1.00f, 0.78f, 3, PF_PLAYER);
		AddPart(m, HJ_BODY, PART_RECT, 0.0f, -10.0f, 26.0f,  3.4f, 0.0f, 0.0f, 0.50f, 0.0f, CS_ACCENT, 0.92f, 0.84f, 3, PF_PLAYER);

		// 앞 레이어
		AddPart(m, HJ_HAND_F, PART_ELLIPSE, 0.0f, 0.0f, 12.5f, 12.5f, 0.0f, 0.0f, 0.0f, 0.14f, CS_SKIN, 1.06f, 0.84f, 4, PF_HANDS_NORMAL);
		AddPart(m, HJ_HAND_B, PART_ELLIPSE, 0.0f, 0.0f, 12.5f, 12.5f, 0.0f, 0.0f, 0.0f, 0.14f, CS_SKIN, 1.06f, 0.84f, 4, PF_HANDS_SWAP);

		// 머리 — 크게. 이 비율이 귀여움의 대부분을 만든다
		AddPart(m, HJ_HEAD, PART_ELLIPSE, 0.0f, -16.5f, 37.0f, 33.0f, 0.0f, 0.5f, 0.0f, 0.18f, CS_SKIN, 1.10f, 0.86f, 5, PF_NONE);

		Col blush(0.95f, 0.58f, 0.52f, 0.45f);
		AddPartFixed(m, HJ_HEAD, PART_ELLIPSE, -10.5f, -10.0f, 9.5f, 6.0f, 0.0f, 0.5f, 0.0f, 0.0f, blush, blush, 6, PF_FACE_ONLY);
		AddPartFixed(m, HJ_HEAD, PART_ELLIPSE,  10.5f, -10.0f, 9.5f, 6.0f, 0.0f, 0.5f, 0.0f, 0.0f, blush, blush, 6, PF_FACE_ONLY);

		// 주민 머리카락
		AddPart(m, HJ_HEAD, PART_ELLIPSE,   0.0f, -27.0f, 38.0f, 20.0f, 0.0f, 0.5f, 0.0f, 0.12f, CS_HAIR, 1.30f, 0.88f, 7, PF_VILLAGER);
		AddPart(m, HJ_HEAD, PART_ELLIPSE, -16.0f, -18.0f,  8.0f, 17.0f, 0.0f, 0.5f, 0.0f, 0.0f,  CS_HAIR, 1.05f, 0.76f, 7, PF_VILLAGER);
		AddPart(m, HJ_HEAD, PART_ELLIPSE,  16.0f, -18.0f,  8.0f, 17.0f, 0.0f, 0.5f, 0.0f, 0.0f,  CS_HAIR, 1.00f, 0.72f, 7, PF_VILLAGER);

		// 셰프 모자 — 플레이어를 한눈에 식별시키는 실루엣
		Col toqueA(0.97f, 0.96f, 0.93f);
		Col toqueB(0.80f, 0.79f, 0.76f);
		AddPartFixed(m, HJ_HEAD, PART_ELLIPSE, -10.0f, -38.0f, 20.0f, 18.0f, 0.0f, 0.5f, 0.0f, 0.10f, toqueA, toqueB, 7, PF_PLAYER);
		AddPartFixed(m, HJ_HEAD, PART_ELLIPSE,  10.0f, -38.0f, 20.0f, 18.0f, 0.0f, 0.5f, 0.0f, 0.10f, toqueA, toqueB, 7, PF_PLAYER);
		AddPartFixed(m, HJ_HEAD, PART_ELLIPSE,   0.0f, -43.0f, 22.0f, 20.0f, 0.0f, 0.5f, 0.0f, 0.16f, toqueA, toqueB, 8, PF_PLAYER);
		AddPartFixed(m, HJ_HEAD, PART_RECT,      0.0f, -30.0f, 34.0f,  8.0f, 0.0f, 0.5f, 0.45f, 0.0f, toqueA, toqueB, 9, PF_PLAYER);

		// 눈 — 크고 단순하게
		Col eye(0.16f, 0.13f, 0.14f);
		Col wht(1.0f, 1.0f, 1.0f, 0.92f);
		AddPartFixed(m, HJ_HEAD, PART_ELLIPSE, -8.5f, -18.0f, 8.5f, 10.0f, 0.0f, 0.5f, 0.0f, 0.0f, eye, eye, 10, PF_FACE_ONLY | PF_EYE_OPEN);
		AddPartFixed(m, HJ_HEAD, PART_ELLIPSE,  8.5f, -18.0f, 8.5f, 10.0f, 0.0f, 0.5f, 0.0f, 0.0f, eye, eye, 10, PF_FACE_ONLY | PF_EYE_OPEN);
		AddPartFixed(m, HJ_HEAD, PART_ELLIPSE, -10.0f, -20.5f, 3.2f, 3.2f, 0.0f, 0.5f, 0.0f, 0.0f, wht, wht, 11, PF_FACE_ONLY | PF_EYE_OPEN);
		AddPartFixed(m, HJ_HEAD, PART_ELLIPSE,   7.0f, -20.5f, 3.2f, 3.2f, 0.0f, 0.5f, 0.0f, 0.0f, wht, wht, 11, PF_FACE_ONLY | PF_EYE_OPEN);

		AddPartFixed(m, HJ_HEAD, PART_RECT, -8.5f, -17.0f, 9.0f, 2.4f, 0.0f, 0.5f, 0.5f, 0.0f, eye, eye, 10, PF_FACE_ONLY | PF_EYE_SHUT);
		AddPartFixed(m, HJ_HEAD, PART_RECT,  8.5f, -17.0f, 9.0f, 2.4f, 0.0f, 0.5f, 0.5f, 0.0f, eye, eye, 10, PF_FACE_ONLY | PF_EYE_SHUT);

		Col mouth(0.42f, 0.24f, 0.24f, 0.85f);
		AddPartFixed(m, HJ_HEAD, PART_ELLIPSE, 0.0f, -8.0f, 7.0f, 4.2f, 0.0f, 0.5f, 0.0f, 0.0f, mouth, mouth, 11, PF_FACE_ONLY);

		SortByOrder(m);
	}

	// ---- 토마토마 -----------------------------------------------------------

	void BuildTomatoma(Model& m)
	{
		int root = AddJoint(m, -1, 0.0f, 0.0f);      // 발밑
		int body = AddJoint(m, root, 0.0f, -17.0f);

		AddPart(m, root, PART_ELLIPSE, -9.0f, -3.0f, 11.0f,  7.0f, 0.0f, 0.0f, 0.0f, 0.0f, CS_DARK,  1.00f, 0.86f, 0, PF_NONE);
		AddPart(m, root, PART_ELLIPSE,  9.0f, -3.0f, 11.0f,  7.0f, 0.0f, 0.0f, 0.0f, 0.0f, CS_DARK,  1.00f, 0.86f, 0, PF_NONE);

		AddPart(m, body, PART_ELLIPSE,  0.0f,  0.0f, 39.0f, 29.0f, 0.0f, 0.0f, 0.0f, 0.20f, CS_CLOTH, 1.12f, 0.76f, 1, PF_NONE);
		AddPart(m, body, PART_ELLIPSE, -11.0f, 1.0f, 12.0f, 25.0f, 0.0f, 0.0f, 0.0f, 0.0f,  CS_CLOTH, 0.86f, 0.78f, 2, PF_NONE);
		AddPart(m, body, PART_ELLIPSE,  11.0f, 1.0f, 12.0f, 25.0f, 0.0f, 0.0f, 0.0f, 0.0f,  CS_CLOTH, 0.86f, 0.78f, 2, PF_NONE);

		Col hi(1.0f, 1.0f, 1.0f, 0.30f);
		AddPartFixed(m, body, PART_ELLIPSE, -9.0f, -8.0f, 13.0f, 9.0f, 0.0f, 0.0f, 0.0f, 0.0f, hi, hi, 3, PF_NONE);

		// 꼭지 — 어깨를 감싸며 바깥으로 눕는 별 모양.
		// 열매에 비해 꼭지가 크면 장식이 본체를 이겨서 시선이 위로 쏠린다.
		float seat = -11.6f;
		float cap = seat - 5.2f;
		AddPart(m, body, PART_ELLIPSE, 0.0f, seat + 1.0f, 15.0f, 6.0f, 0.0f, 0.0f, 0.0f, 0.0f, CS_CLOTH, 0.70f, 0.66f, 4, PF_NONE);

		const float spread[5] = { -1.0f, -0.55f, 0.0f, 0.55f, 1.0f };
		for (int k = 0; k < 5; ++k)
		{
			float f = spread[k];
			float lx = f * 12.0f;
			float ly = cap + 1.8f + fabsf(f) * 2.9f;
			float lw = 11.0f - fabsf(f) * 2.6f;
			float lh = 6.3f - fabsf(f) * 1.2f;
			AddPart(m, body, PART_ELLIPSE, lx, ly, lw, lh, 0.0f, 0.0f, 0.0f, 0.0f,
				CS_ACCENT, 1.12f - fabsf(f) * 0.12f, 0.76f, 5, PF_NONE);
		}
		AddPart(m, body, PART_ELLIPSE, 0.0f, cap,        13.2f, 6.0f, 0.0f, 0.0f, 0.0f, 0.12f, CS_ACCENT, 1.20f, 0.88f, 6, PF_NONE);
		AddPart(m, body, PART_RECT,    0.0f, cap - 5.2f,  4.0f, 10.4f, 0.0f, 0.5f, 0.45f, 0.0f, CS_ACCENT, 0.94f, 0.64f, 6, PF_NONE);
		AddPart(m, body, PART_ELLIPSE, 0.0f, cap - 10.4f, 5.5f,  3.8f, 0.0f, 0.0f, 0.0f, 0.0f, CS_ACCENT, 1.06f, 0.80f, 6, PF_NONE);

		Col eye(0.14f, 0.11f, 0.10f);
		Col wht(1.0f, 1.0f, 1.0f, 0.9f);
		AddPartFixed(m, body, PART_ELLIPSE, -6.5f, 1.0f, 6.5f, 7.5f, 0.0f, 0.0f, 0.0f, 0.0f, eye, eye, 7, PF_NONE);
		AddPartFixed(m, body, PART_ELLIPSE,  7.0f, 1.0f, 6.5f, 7.5f, 0.0f, 0.0f, 0.0f, 0.0f, eye, eye, 7, PF_NONE);
		AddPartFixed(m, body, PART_ELLIPSE, -7.4f, -0.8f, 2.4f, 2.4f, 0.0f, 0.0f, 0.0f, 0.0f, wht, wht, 8, PF_NONE);
		AddPartFixed(m, body, PART_ELLIPSE,  6.1f, -0.8f, 2.4f, 2.4f, 0.0f, 0.0f, 0.0f, 0.0f, wht, wht, 8, PF_NONE);

		SortByOrder(m);
	}

	// ---- 알밤멧돼지 ---------------------------------------------------------
	// 레벨 1 의 선공 몬스터. 다리 넷이 각각 관절을 가져 총총거리며 달린다.

	void BuildBoar(Model& m)
	{
		int root = AddJoint(m, -1, 0.0f, 0.0f);
		int body = AddJoint(m, root, 0.0f, -20.0f);
		int head = AddJoint(m, body, 17.0f, -4.0f);
		int l0 = AddJoint(m, body, -13.0f, 8.0f);
		int l1 = AddJoint(m, body,  -4.0f, 9.0f);
		int l2 = AddJoint(m, body,   6.0f, 9.0f);
		int l3 = AddJoint(m, body,  14.0f, 8.0f);

		int legs[4] = { l0, l1, l2, l3 };
		for (int i = 0; i < 4; ++i)
		{
			AddPart(m, legs[i], PART_RECT,    0.0f, 6.0f,  6.4f, 12.0f, 0.0f, -0.5f, 0.45f, 0.0f, CS_DARK, 0.94f, 0.64f, 0, PF_NONE);
			AddPart(m, legs[i], PART_ELLIPSE, 0.0f, 12.5f, 7.4f,  4.6f, 0.0f,  0.0f, 0.0f,  0.0f, CS_DARK, 0.80f, 0.60f, 0, PF_NONE);
		}

		AddPart(m, body, PART_ELLIPSE, 0.0f,  0.0f, 48.0f, 30.0f, 0.0f, 0.0f, 0.0f, 0.18f, CS_CLOTH, 1.14f, 0.72f, 1, PF_NONE);
		AddPart(m, body, PART_ELLIPSE, -8.0f, -7.0f, 26.0f, 12.0f, 0.0f, 0.0f, 0.0f, 0.0f, CS_ACCENT, 1.05f, 0.85f, 2, PF_NONE);

		AddPart(m, head, PART_ELLIPSE,  0.0f,  0.0f, 26.0f, 21.0f, 0.0f, 0.0f, 0.0f, 0.16f, CS_CLOTH, 1.18f, 0.80f, 3, PF_NONE);
		AddPart(m, head, PART_ELLIPSE,  9.0f,  3.0f, 13.0f, 11.0f, 0.0f, 0.0f, 0.0f, 0.0f,  CS_ACCENT, 1.10f, 0.88f, 4, PF_NONE);
		AddPart(m, head, PART_TRI,     -6.0f, -12.0f, 8.0f, 10.0f, 0.0f, 0.0f, 0.0f, 0.0f,  CS_CLOTH, 0.92f, 0.70f, 3, PF_NONE);
		AddPart(m, head, PART_TRI,      3.0f, -13.0f, 8.0f, 10.0f, 0.0f, 0.0f, 0.0f, 0.0f,  CS_CLOTH, 0.88f, 0.66f, 3, PF_NONE);

		Col tusk(0.94f, 0.92f, 0.84f);
		AddPartFixed(m, head, PART_TRI, 12.0f, 8.0f, 5.0f, 9.0f, 0.0f, 0.0f, 0.0f, 0.0f, tusk, Shade(tusk, 0.8f), 5, PF_NONE);
		AddPartFixed(m, head, PART_TRI,  5.0f, 9.0f, 4.5f, 8.0f, 0.0f, 0.0f, 0.0f, 0.0f, tusk, Shade(tusk, 0.8f), 5, PF_NONE);

		Col eye(0.92f, 0.32f, 0.22f);
		AddPartFixed(m, head, PART_ELLIPSE, 5.0f, -3.0f, 4.6f, 5.0f, 0.0f, 0.0f, 0.0f, 0.0f, eye, eye, 6, PF_NONE);

		SortByOrder(m);
	}

	// ---- 초목 / 바위 ---------------------------------------------------------
	//
	// 나무 크기는 v0.4 에서 한 번 4배로 키웠다가 되돌렸다. 수관이 화면을
	// 덮어서 플레이가 불가능했다. 쿼터뷰에서 카메라가 플레이어에 붙어 있는
	// 한, 나무는 사람보다 조금 큰 정도가 상한이다.

	void BuildTree(Model& m)
	{
		int root = AddJoint(m, -1, 0.0f, 0.0f);
		int top  = AddJoint(m, root, 0.0f, -31.0f);   // 바람에 흔들리는 관절

		AddPart(m, root, PART_RECT, 0.0f, -15.0f, 12.0f, 33.0f, 0.0f, 0.5f, 0.30f, 0.0f, CS_DARK, 1.22f, 0.84f, 0, PF_NONE);
		AddPart(m, top, PART_ELLIPSE, -12.0f,  -7.0f, 37.0f, 30.0f, 0.0f, 0.5f, 0.0f, 0.0f,  CS_CLOTH, 1.04f, 0.84f, 1, PF_NONE);
		AddPart(m, top, PART_ELLIPSE,  12.0f, -11.0f, 35.0f, 28.0f, 0.0f, 0.5f, 0.0f, 0.0f,  CS_CLOTH, 1.00f, 0.80f, 1, PF_NONE);
		AddPart(m, top, PART_ELLIPSE,   0.0f, -24.0f, 42.0f, 37.0f, 0.0f, 0.5f, 0.0f, 0.16f, CS_CLOTH, 1.18f, 0.94f, 2, PF_NONE);

		SortByOrder(m);
	}

	// 침엽수 — 활엽수와 높이·실루엣이 달라야 숲 능선이 단조롭지 않다.
	void BuildPine(Model& m)
	{
		int root = AddJoint(m, -1, 0.0f, 0.0f);
		int top  = AddJoint(m, root, 0.0f, -20.0f);

		AddPart(m, root, PART_RECT, 0.0f, -12.0f, 11.0f, 24.0f, 0.0f, 0.5f, 0.24f, 0.0f, CS_DARK, 1.20f, 0.82f, 0, PF_NONE);

		AddPart(m, top, PART_TRI, 0.0f, -11.0f, 39.0f, 31.0f, 0.0f, 0.5f, 0.0f, 0.0f,  CS_CLOTH, 1.02f, 0.80f, 1, PF_NONE);
		AddPart(m, top, PART_TRI, 0.0f, -29.0f, 32.0f, 27.0f, 0.0f, 0.5f, 0.0f, 0.0f,  CS_CLOTH, 1.12f, 0.88f, 2, PF_NONE);
		AddPart(m, top, PART_TRI, 0.0f, -44.0f, 24.0f, 24.0f, 0.0f, 0.5f, 0.0f, 0.14f, CS_CLOTH, 1.24f, 0.98f, 3, PF_NONE);

		SortByOrder(m);
	}

	// 덤불 — 나무와 바닥 사이의 빈 공간을 메우는 낮은 초목.
	void BuildBush(Model& m)
	{
		int root = AddJoint(m, -1, 0.0f, 0.0f);

		AddPart(m, root, PART_ELLIPSE, -7.0f,  -7.0f, 22.0f, 17.0f, 0.0f, 0.5f, 0.0f, 0.0f,  CS_CLOTH, 0.94f, 0.68f, 0, PF_NONE);
		AddPart(m, root, PART_ELLIPSE,  8.0f,  -8.0f, 21.0f, 16.0f, 0.0f, 0.5f, 0.0f, 0.0f,  CS_CLOTH, 1.00f, 0.72f, 0, PF_NONE);
		AddPart(m, root, PART_ELLIPSE,  0.0f, -14.0f, 24.0f, 19.0f, 0.0f, 0.5f, 0.0f, 0.12f, CS_CLOTH, 1.16f, 0.86f, 1, PF_NONE);

		SortByOrder(m);
	}

	void BuildRock(Model& m)
	{
		int root = AddJoint(m, -1, 0.0f, 0.0f);
		AddPart(m, root, PART_ELLIPSE,  0.0f,  -8.0f, 30.0f, 20.0f, 0.0f, 0.0f, 0.0f, 0.10f, CS_CLOTH, 1.10f, 0.76f, 0, PF_NONE);
		AddPart(m, root, PART_ELLIPSE, -5.0f, -14.0f, 16.0f, 11.0f, 0.0f, 0.0f, 0.0f, 0.0f,  CS_CLOTH, 1.24f, 0.98f, 1, PF_NONE);
		SortByOrder(m);
	}

	void BuildAll()
	{
		for (int i = 0; i < MODEL_COUNT; ++i)
		{
			g_Models[i].joints.clear();
			g_Models[i].parts.clear();
		}
		BuildHuman(g_Models[MODEL_HUMAN]);
		BuildTomatoma(g_Models[MODEL_TOMATOMA]);
		BuildBoar(g_Models[MODEL_BOAR]);
		BuildTree(g_Models[MODEL_TREE]);
		BuildPine(g_Models[MODEL_PINE]);
		BuildBush(g_Models[MODEL_BUSH]);
		BuildRock(g_Models[MODEL_ROCK]);
	}

	// ---- 캐시 입출력 ---------------------------------------------------------

	bool SaveCache()
	{
		_mkdir("./Data");

		FILE* f = NULL;
		if (fopen_s(&f, CACHE_PATH, "wb") != 0 || f == NULL) return false;

		int magic = CACHE_MAGIC;
		int ver = CACHE_VERSION;
		int count = MODEL_COUNT;
		int sj = (int)sizeof(ModelJoint);
		int sp = (int)sizeof(ModelPart);

		fwrite(&magic, sizeof(int), 1, f);
		fwrite(&ver,   sizeof(int), 1, f);
		fwrite(&sj,    sizeof(int), 1, f);
		fwrite(&sp,    sizeof(int), 1, f);
		fwrite(&count, sizeof(int), 1, f);

		for (int i = 0; i < MODEL_COUNT; ++i)
		{
			int nj = (int)g_Models[i].joints.size();
			int np = (int)g_Models[i].parts.size();
			fwrite(&nj, sizeof(int), 1, f);
			fwrite(&np, sizeof(int), 1, f);
			if (nj > 0) fwrite(&g_Models[i].joints[0], sizeof(ModelJoint), nj, f);
			if (np > 0) fwrite(&g_Models[i].parts[0],  sizeof(ModelPart),  np, f);
		}

		fclose(f);
		return true;
	}

	bool LoadCache()
	{
		FILE* f = NULL;
		if (fopen_s(&f, CACHE_PATH, "rb") != 0 || f == NULL) return false;

		int magic = 0, ver = 0, sj = 0, sp = 0, count = 0;
		bool ok =
			fread(&magic, sizeof(int), 1, f) == 1 &&
			fread(&ver,   sizeof(int), 1, f) == 1 &&
			fread(&sj,    sizeof(int), 1, f) == 1 &&
			fread(&sp,    sizeof(int), 1, f) == 1 &&
			fread(&count, sizeof(int), 1, f) == 1;

		// 구조체 크기까지 검사한다. 파트 구조가 바뀌면 캐시는 자동으로 폐기된다.
		if (!ok || magic != CACHE_MAGIC || ver != CACHE_VERSION ||
			sj != (int)sizeof(ModelJoint) || sp != (int)sizeof(ModelPart) ||
			count != MODEL_COUNT)
		{
			fclose(f);
			return false;
		}

		for (int i = 0; i < MODEL_COUNT; ++i)
		{
			int nj = 0, np = 0;
			if (fread(&nj, sizeof(int), 1, f) != 1 ||
				fread(&np, sizeof(int), 1, f) != 1 ||
				nj < 0 || np < 0 || nj > 256 || np > 4096)
			{
				fclose(f);
				return false;
			}

			g_Models[i].joints.resize(nj);
			g_Models[i].parts.resize(np);

			if (nj > 0 && fread(&g_Models[i].joints[0], sizeof(ModelJoint), nj, f) != (size_t)nj)
			{
				fclose(f);
				return false;
			}
			if (np > 0 && fread(&g_Models[i].parts[0], sizeof(ModelPart), np, f) != (size_t)np)
			{
				fclose(f);
				return false;
			}
		}

		fclose(f);
		return true;
	}

	// ---- 관절 변환 -----------------------------------------------------------

	struct JointXform
	{
		float x, y, a, c, s;
	};

	JointXform g_Xform[64];

	// ---- 광원 -----------------------------------------------------------------

	// 파트마다 shadeTop/shadeBot 를 다시 잡는 대신, 위아래 명암 폭을 여기서
	// 한 번에 벌린다. 평균을 유지하며 벌리므로 전체 밝기는 그대로고 입체감만
	// 올라간다. 1.0 이면 원래대로.
	static const float LIGHT_SPREAD = 1.18f;

	inline void Spread1(float t, float b, float& outT, float& outB)
	{
		float mid = (t + b) * 0.5f;
		outT = mid + (t - mid) * LIGHT_SPREAD;
		outB = mid + (b - mid) * LIGHT_SPREAD;
		if (outT < 0.0f) outT = 0.0f; else if (outT > 1.0f) outT = 1.0f;
		if (outB < 0.0f) outB = 0.0f; else if (outB > 1.0f) outB = 1.0f;
	}

	void SpreadLight(Col& top, Col& bot)
	{
		Spread1(top.r, bot.r, top.r, bot.r);
		Spread1(top.g, bot.g, top.g, bot.g);
		Spread1(top.b, bot.b, top.b, bot.b);
	}
}

namespace Models
{

void Load()
{
	if (g_Loaded) return;

	if (LoadCache())
	{
		printf("Models: loaded from %s\n", CACHE_PATH);
	}
	else
	{
		BuildAll();
		if (SaveCache()) printf("Models: built and cached to %s\n", CACHE_PATH);
		else             printf("Models: built (cache write failed)\n");
	}
	g_Loaded = true;
}

const Model& Get(int modelId)
{
	if (modelId < 0 || modelId >= MODEL_COUNT) modelId = 0;
	return g_Models[modelId];
}

// 관절 트리를 루트부터 풀어 g_Xform 에 스크린 좌표와 누적 각을 채운다.
// Draw 와 JointPosition 이 같은 계산을 공유해야 손에 쥔 물건이 손에서
// 어긋나지 않는다. 반환값은 유효한 관절 수.
static int SolveJoints(const Model& m, float originX, float originY,
	const float* angles, const float* offsets, float sclX, float sclY, float flipX)
{
	int nj = (int)m.joints.size();
	if (nj > 64) nj = 64;

	for (int i = 0; i < nj; ++i)
	{
		const ModelJoint& j = m.joints[i];
		float local = (angles != NULL) ? angles[i] * flipX : 0.0f;

		float addX = (offsets != NULL) ? offsets[i * 2 + 0] : 0.0f;
		float addY = (offsets != NULL) ? offsets[i * 2 + 1] : 0.0f;

		float lx = (j.ox + addX) * sclX * flipX;
		float ly = (j.oy + addY) * sclY;

		if (j.parent < 0 || j.parent >= i)
		{
			// 부모가 뒤에 오는 경우도 루트로 취급해 순환을 막는다.
			g_Xform[i].x = originX + lx;
			g_Xform[i].y = originY + ly;
			g_Xform[i].a = local;
		}
		else
		{
			const JointXform& p = g_Xform[j.parent];
			g_Xform[i].x = p.x + lx * p.c - ly * p.s;
			g_Xform[i].y = p.y + lx * p.s + ly * p.c;
			g_Xform[i].a = p.a + local;
		}
		g_Xform[i].c = cosf(g_Xform[i].a);
		g_Xform[i].s = sinf(g_Xform[i].a);
	}
	return nj;
}

static void DrawImpl(Renderer* r, int modelId, float originX, float originY,
	const float* angles, const float* offsets, const Palette& pal,
	float scale, float squash, float flipX, int flagMask, float alphaMul,
	int onlyJoint, int swayJoint, float swayPhase, float swayAmp, float swayFreq)
{
	const Model& m = Get(modelId);

	// 스쿼시는 부피를 대충 보존하는 방향으로 x 와 y 를 반대로 민다.
	if (squash < 0.25f) squash = 0.25f;
	const float sclX = scale / squash;
	const float sclY = scale * squash;

	int nj = SolveJoints(m, originX, originY, angles, offsets, sclX, sclY, flipX);

	for (size_t pi = 0; pi < m.parts.size(); ++pi)
	{
		const ModelPart& p = m.parts[pi];
		if ((p.flags & flagMask) != p.flags) continue;
		if (p.joint < 0 || p.joint >= nj) continue;
		if (onlyJoint >= 0 && p.joint != onlyJoint) continue;

		// 흔들림은 GPU 가 푼다. 흔들리는 관절 아래 파트에만 피벗을 넘긴다
		if (swayJoint >= 0)
		{
			bool under = false;
			for (int jj = p.joint; jj >= 0 && jj < nj; jj = m.joints[jj].parent)
			{
				if (jj == swayJoint) { under = true; break; }
				if (m.joints[jj].parent >= jj) break;
			}
			if (under)
				r->SetSway(g_Xform[swayJoint].x, g_Xform[swayJoint].y,
					swayPhase, swayAmp, swayFreq);
			else
				r->ClearSway();
		}

		const JointXform& j = g_Xform[p.joint];

		float lx = p.ox * sclX * flipX;
		float ly = p.oy * sclY;
		float cx = j.x + lx * j.c - ly * j.s;
		float cy = j.y + lx * j.s + ly * j.c;

		Col top, bot;
		if (p.slot >= 0 && p.slot < CS_COUNT)
		{
			top = Shade(pal.c[p.slot], p.shadeTop);
			bot = Shade(pal.c[p.slot], p.shadeBot);
		}
		else
		{
			top = p.fixedTop;
			bot = p.fixedBot;
		}
		SpreadLight(top, bot);
		top.a *= alphaMul;
		bot.a *= alphaMul;

		r->SetRotation(j.a, p.pivotX * flipX, p.pivotY);

		float w = p.w * sclX;
		float h = p.h * sclY;

		switch (p.shape)
		{
		case PART_RECT:    r->DrawRect(cx, cy, w, h, top, bot, p.radius, p.rim); break;
		case PART_ELLIPSE: r->DrawEllipse(cx, cy, w, h, top, bot, p.rim);        break;
		case PART_DIAMOND: r->DrawDiamond(cx, cy, w, h, top, bot);               break;
		case PART_TRI:     r->DrawTriangle(cx, cy, w, h, top, bot, p.rim);       break;
		default: break;
		}
	}

	r->ClearRotation();
	if (swayJoint >= 0) r->ClearSway();
}

void Draw(Renderer* r, int modelId, float originX, float originY,
	const float* angles, const float* offsets, const Palette& pal,
	float scale, float squash, float flipX, int flagMask, float alphaMul)
{
	DrawImpl(r, modelId, originX, originY, angles, offsets, pal,
		scale, squash, flipX, flagMask, alphaMul, -1, -1, 0.0f, 0.0f, 0.0f);
}

void DrawJoint(Renderer* r, int modelId, float originX, float originY,
	const float* angles, const float* offsets, const Palette& pal,
	float scale, float squash, float flipX, int flagMask, float alphaMul, int joint)
{
	DrawImpl(r, modelId, originX, originY, angles, offsets, pal,
		scale, squash, flipX, flagMask, alphaMul, joint, -1, 0.0f, 0.0f, 0.0f);
}

void DrawSwaying(Renderer* r, int modelId, float originX, float originY,
	const Palette& pal, float scale, float alphaMul,
	int swayJoint, float phase, float amplitude, float freq)
{
	DrawImpl(r, modelId, originX, originY, NULL, NULL, pal,
		scale, 1.0f, 1.0f, 0xFF, alphaMul, -1, swayJoint, phase, amplitude, freq);
}

void JointPosition(int modelId, float originX, float originY,
	const float* angles, const float* offsets, float scale, float squash, float flipX,
	int joint, float* outX, float* outY, float* outAngle)
{
	const Model& m = Get(modelId);

	if (squash < 0.25f) squash = 0.25f;
	int nj = SolveJoints(m, originX, originY, angles, offsets,
		scale / squash, scale * squash, flipX);

	if (joint < 0 || joint >= nj)
	{
		*outX = originX;
		*outY = originY;
		if (outAngle) *outAngle = 0.0f;
		return;
	}
	*outX = g_Xform[joint].x;
	*outY = g_Xform[joint].y;
	if (outAngle) *outAngle = g_Xform[joint].a;
}

float HumanPose(float* a, float* off, float* squash, int* poseFlags,
	float phase, bool walking, float attackT)
{
	*poseFlags = PF_HANDS_NORMAL;

	for (int i = 0; i < HJ_COUNT; ++i)
	{
		a[i] = 0.0f;
		off[i * 2 + 0] = 0.0f;
		off[i * 2 + 1] = 0.0f;
	}
	*squash = 1.0f;

	float bob = 0.0f;

	if (walking)
	{
		float s = sinf(phase);

		// 좌우 흔들림은 걸음당 한 번이라, 값이 조금만 커도 금세 뒤뚱거리는
		// 인상이 된다.
		const float TILT = 0.055f;
		float bodyTilt = s * TILT;

		// 머리의 "지연"은 추가 회전이 아니다. HEAD 는 BODY 의 자식이라
		// 로컬 각이 부모 각 위에 누적되므로, 목표 월드 각에서 부모 각을 빼야
		// 실제로 늦게 따라오는 것처럼 보인다. 이 보정을 빼면 머리가 몸통과
		// 같은 방향으로 두 배 기운다.
		//
		// 부호가 음수인 이유: 머리가 몸통을 따라가는 게 아니라 반대로 버틴다.
		// 목이 관성으로 버티는 모양이라 이쪽이 더 자연스럽다.
		float headWorld = -sinf(phase - 0.50f) * TILT * 0.80f;

		a[HJ_BODY] = bodyTilt;
		a[HJ_HEAD] = headWorld - bodyTilt;

		// 걸음마다 한 번이 아니라 두 번 튄다
		bob = fabsf(sinf(phase)) * 3.6f;
		*squash = 1.0f - fabsf(sinf(phase)) * 0.055f;

		// 발은 번갈아 들린다. 무릎이 없으니 꺾일 일도 없다.
		float liftB = sinf(phase);
		float liftF = sinf(phase + 3.14159265f);
		off[HJ_FOOT_B * 2 + 1] = (liftB > 0.0f ? -liftB : 0.0f) * 6.0f;
		off[HJ_FOOT_F * 2 + 1] = (liftF > 0.0f ? -liftF : 0.0f) * 6.0f;
		off[HJ_FOOT_B * 2 + 0] = liftB * 3.4f;
		off[HJ_FOOT_F * 2 + 0] = liftF * 3.4f;

		// 손은 좌우로 움직이지 않는다. 위아래 관성만 주고, 앞뒤 교차는
		// 그리기 순서로만 표현한다. 좌우로 흔들면 팔이 없는 형태에서는
		// 손이 몸통 위를 미끄러지는 것처럼 보인다.
		float swing = sinf(phase + 0.9f);

		off[HJ_HAND_B * 2 + 1] = -fabsf(swing) * 0.7f + swing * 1.2f;
		off[HJ_HAND_F * 2 + 1] = -fabsf(swing) * 0.7f - swing * 1.2f;

		// 앞으로 나온 쪽을 몸통 앞에 그린다
		if (swing > 0.0f) *poseFlags = PF_HANDS_SWAP;
	}
	else
	{
		float t = phase;

		// 숨쉬기는 스쿼시로만 표현한다
		*squash = 1.0f + sinf(t * 1.5f) * 0.030f;
		bob = sinf(t * 1.5f) * 0.9f;

		a[HJ_BODY] = sinf(t * 0.55f) * 0.030f;

		// 가끔 고개를 갸웃한다
		float tiltCycle = t * 0.11f;
		float tw = tiltCycle - floorf(tiltCycle);
		float tilt = (tw > 0.82f) ? sinf((tw - 0.82f) / 0.18f * 3.14159265f) * 0.17f : 0.0f;
		a[HJ_HEAD] = sinf(t * 0.45f) * 0.045f + tilt;

		off[HJ_HAND_B * 2 + 1] = sinf(t * 1.4f + 0.6f) * 1.3f;
		off[HJ_HAND_F * 2 + 1] = sinf(t * 1.4f) * 1.3f;
	}

	// 공격 — 몸을 뒤로 젖혔다가 앞으로 채고, 손이 따라 휘둘린다.
	// 팔이 없으니 손의 궤적만 그리면 된다.
	if (attackT >= 0.0f)
	{
		float lean;
		if (attackT < 0.34f) lean = -0.34f * (attackT / 0.34f);
		else                 lean = -0.34f + 0.80f * ((attackT - 0.34f) / 0.66f);

		a[HJ_BODY] = lean;
		a[HJ_HEAD] = lean * 0.45f;

		float reach = (attackT < 0.34f) ? -(attackT / 0.34f) * 6.0f
		                                : -6.0f + 22.0f * ((attackT - 0.34f) / 0.66f);
		off[HJ_HAND_F * 2 + 0] = reach;
		off[HJ_HAND_F * 2 + 1] = -fabsf(reach) * 0.22f;
		off[HJ_HAND_B * 2 + 0] = -reach * 0.25f;

		*squash = 1.0f - fabsf(lean) * 0.10f;
	}

	return bob;
}

Palette MakeHumanPalette(const Col& cloth, bool player)
{
	Palette p;
	p.c[CS_CLOTH]    = cloth;
	p.c[CS_SKIN]     = Col(0.89f, 0.73f, 0.59f);
	p.c[CS_HAIR]     = Col(0.23f, 0.16f, 0.12f);
	p.c[CS_TROUSERS] = Col(0.29f, 0.26f, 0.28f);
	p.c[CS_SHOE]     = Col(0.30f, 0.21f, 0.15f);
	p.c[CS_ACCENT]   = player ? Col(0.93f, 0.90f, 0.84f) : cloth;
	p.c[CS_DARK]     = Shade(cloth, 0.55f);
	return p;
}

} // namespace Models
