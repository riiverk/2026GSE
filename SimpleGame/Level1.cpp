#include "stdafx.h"

#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <cwchar>

#include "Level1.h"
#include "Renderer.h"
#include "Model.h"
#include "Actor.h"
#include "SceneGraph.h"
#include "Profiler.h"
#include "JsonWriter.h"

// =============================================================================
// 레벨 1 — 사냥터
//
// 이 파일은 UTF-8 BOM 으로 저장되어 있다. 인코딩을 바꾸지 말 것.
// =============================================================================

namespace Level1 {

// -----------------------------------------------------------------------------
// 상수
// -----------------------------------------------------------------------------

static const int   MAP_W = 72;
static const int   MAP_H = 72;

static const float TILE_W = 72.0f;
static const float TILE_H = 36.0f;

static const float PI_F = 3.14159265f;

// 카메라 줌.
//
// 나무 키우기와 시야 확보는 서로 반대 방향이라 나무 크기만으로는 둘 다
// 만족시킬 수 없었다. 나무를 사람의 두 배 넘게 키우고 화면 전체를 줌아웃
// 하면 둘 다 성립한다 — 나무는 높아 보이고, 화면에 들어오는 타일 수는
// 오히려 늘어난다. 이게 "크기감"의 정체다.
//
// 1.0 이 원래 배율. 낮출수록 멀리서 본다.
static const float WORLD_ZOOM = 0.74f;

// 나무 크기 — 모델 치수는 건드리지 말고 여기만 만진다.
// 모델 기준 높이는 활엽수 169px, 침엽수 186px (사람은 60~77px).
static const float TREE_SCALE = 0.92f;
static const float TREE_JITTER = 0.18f;

// 대기 원근. 먼 것(= 화면 위쪽)을 하늘색으로 빼준다. 쿼터뷰에서 쓸 수 있는
// 가장 강한 깊이 단서이고, 비용은 색 보간 한 번이다.
static const Col   C_HAZE(0.68f, 0.75f, 0.80f);
static const float HAZE_STRENGTH = 0.32f;

// 플레이어를 가리는 나무를 비친다. 나무를 키우면 반드시 필요해진다.
static const float SEETHRU_X     = 52.0f;
static const float SEETHRU_UP    = 180.0f;
static const float SEETHRU_ALPHA = 0.34f;

static Renderer* R = 0;
static int SCR_W = 1280;
static int SCR_H = 720;

// -----------------------------------------------------------------------------
// 팔레트
// -----------------------------------------------------------------------------

// 예전 팔레트는 풀·길·숲이 전부 황토 계열이라, 채도를 올리면 화면 전체가
// 누렇게 떴다. 녹색은 녹색으로, 흙은 중립 회갈색으로 갈라 둔다.
static const Col C_SKY    (0.74f, 0.78f, 0.80f);
static const Col C_GRASS  (0.42f, 0.56f, 0.36f);
static const Col C_TRAIL  (0.64f, 0.59f, 0.50f);
static const Col C_THICK  (0.22f, 0.31f, 0.25f);   // 숲(벽)
static const Col C_WATER  (0.26f, 0.47f, 0.56f);
static const Col C_EMBER  (0.38f, 0.35f, 0.31f);

// -----------------------------------------------------------------------------
// 타일
// -----------------------------------------------------------------------------

enum TileType
{
	T_THICK = 0,   // 빽빽한 숲. 통행 불가
	T_GRASS,
	T_TRAIL,
	T_WATER,
	T_EMBER
};

static unsigned char g_Tile[MAP_H][MAP_W];
static bool          g_Solid[MAP_H][MAP_W];

// -----------------------------------------------------------------------------
// 난수 — 시드를 찍어 두면 같은 맵을 다시 볼 수 있다
// -----------------------------------------------------------------------------

static unsigned int g_Rng = 1;
static unsigned int g_Seed = 1;

static unsigned int NextRand()
{
	g_Rng = g_Rng * 1664525u + 1013904223u;
	return g_Rng;
}

static int RandInt(int lo, int hi)          // [lo, hi]
{
	if (hi <= lo) return lo;
	return lo + (int)(NextRand() % (unsigned int)(hi - lo + 1));
}

static float RandF()
{
	return (float)(NextRand() >> 8) / 16777216.0f;
}

// -----------------------------------------------------------------------------
// 엔티티 데이터
// -----------------------------------------------------------------------------

enum Species { SP_TOMATOMA = 0, SP_BOAR, SP_COUNT };

struct SpeciesInfo
{
	const wchar_t* name;
	int   model;
	int   maxHp;
	int   xp;
	int   contactDamage;   // 0 이면 비선공
	float speed;
	float aggroRange;
	float scale;
	Col   body;
	Col   accent;
};

static const SpeciesInfo g_Species[SP_COUNT] =
{
	{ L"토마토마",   MODEL_TOMATOMA, 14, 8,  0, 1.10f, 0.0f, 1.00f,
	  Col(0.82f, 0.28f, 0.24f), Col(0.42f, 0.60f, 0.30f) },
	{ L"알밤멧돼지", MODEL_BOAR,     34, 22, 7, 2.35f, 6.5f, 1.00f,
	  Col(0.46f, 0.33f, 0.24f), Col(0.72f, 0.58f, 0.36f) },
};

// 선공 몬스터의 행동 상태.
//
// 이전에는 상태 없이 매 프레임 플레이어 쪽으로 직진하기만 했다. 그러면
// 도착한 뒤에도 계속 밀고 들어와 몸을 비비게 되고, 여러 마리가 같은 목표를
// 향하니 한 점에 겹쳐 쌓인다. 접근 -> 준비 -> 돌진 -> 경직 사이클로 끊어야
// 각자 다른 타이밍에 움직이면서 공간이 생긴다.
enum MonState
{
	MS_WANDER = 0,
	MS_CHASE,
	MS_WINDUP,
	MS_CHARGE,
	MS_RECOVER,
	MS_FLEE        // 비선공 전용
};

enum ItemKind { IK_BERRY = 0, IK_MEAT, IK_CORE, IK_COUNT };

struct ItemInfo
{
	const wchar_t* name;
	Col            color;
};

static const ItemInfo g_Items[IK_COUNT] =
{
	{ L"토마토마 열매", Col(0.86f, 0.34f, 0.28f) },
	{ L"멧돼지 등심",   Col(0.78f, 0.46f, 0.44f) },
	{ L"미식핵",        Col(0.95f, 0.84f, 0.42f) },
};

// 무기. 판정 거리와 부채꼴 폭은 무기가 정한다.
//
// 맨손은 1.75 타일 / 120도였는데, 선공 몬스터가 빙빙 도는 대기 거리
// (1.35~2.3 타일)보다 짧아서 돌진이 끝날 때까지 칠 수가 없었다.

// 국자 치수. 길이는 "지면과 평행한 월드 방향으로 잰 px" 이고, 화면에는
// 아이소 투영으로 눕혀서 그린다 (정면 방향은 절반으로 짧아진다).
static const float LADLE_HAND_R = 14.0f;   // 휘두를 때 몸통 중심에서 손까지
static const float LADLE_HANDLE = 56.0f;   // 자루 길이
static const float LADLE_GRIP   = 8.0f;    // 자루 끝에서 손까지
static const float LADLE_R      = 12.0f;   // 머리(그릇) 반지름
static const float LADLE_DEPTH  = 11.0f;   // 그릇 깊이 (화면 px)
// 실제 국자는 자루가 그릇 위로 꺾여 올라가 있어서, 자루를 수평으로 쥐어도
// 그릇이 자루 끝보다 아래에 매달린다. 자루 기준 "아래" 방향으로 이만큼 내린다.
static const float LADLE_NECK_DROP = 5.0f;
// 그릇을 자루 끝 쪽으로 당겨 붙이는 양 (자루 방향 px). 0 이면 입구 테두리가
// 자루 끝에 딱 닿는다. 키울수록 자루가 그릇 안으로 파고들어 짧고 뭉툭해 보인다
static const float LADLE_HEAD_TUCK = 5.0f;

// 판정 거리는 그림에서 나온다. 국자 머리 바깥 끝까지의 길이를 타일로 바꾸고
// 몬스터 몸 반경만큼 더한다. 지면 방향 1 타일 = TILE_W / sqrt(2) px.
static const float LADLE_REACH_TILES =
	(LADLE_HAND_R + LADLE_HANDLE - LADLE_GRIP + LADLE_R * 2.0f - LADLE_HEAD_TUCK)
		/ (TILE_W * 0.70710678f)
	+ 0.45f;

enum WeaponKind { WP_LADLE = 0, WP_COUNT };

struct WeaponInfo
{
	const wchar_t* name;
	float range;    // 타일
	float arcDot;   // 정면과의 내적 하한. 0.30 이면 좌우 약 72도
};

// 국자는 머리 위로 들어올렸다가 정면으로 내려친다. 가로로 쓸던 때(좌우
// 72도)보다 궤적이 좁으므로 판정 부채꼴도 좌우 60도로 줄인다.
static const WeaponInfo g_Weapons[WP_COUNT] =
{
	{ L"국자", LADLE_REACH_TILES, 0.50f },
};

enum PropType
{
	PROP_TREE = 0,   // 활엽수
	PROP_ROCK,
	PROP_FIRE,       // 모닥불
	PROP_PINE,       // 침엽수
	PROP_BUSH
};

// =============================================================================
// Actor
//
// 화면에 놓이는 것은 전부 여기 있는 Actor 다. 위치는 Actor 가 갖고, 나머지
// 게임 상태는 각 클래스의 필드로 둔다. 위치는 부모 기준이지만 이 레벨의
// 그룹 노드는 전부 원점에 있으므로 GetX/GetY 가 곧 월드 좌표다.
// =============================================================================

// 지면 전체. 타일 데이터(g_Tile)는 맵 생성과 충돌 판정이 직접 쓰므로 레벨이
// 갖고, 이 Actor 는 그것을 그리는 일만 한다.
//
// 지면은 두 겹이다.
//   바닥  TerrainLayer. 타일마다 마름모를 그리지 않고, 셰이더가 픽셀마다
//         주변 타일 종류를 섞고 경계를 노이즈로 휘게 만든다. 격자 선이 없고
//         풀·길·물·숲의 경계가 곡선이 된다. draw call 1.
//   장식  잡초·자갈. 움직이지 않고 순서가 섞이지 않으므로 정적 배치. draw call 1.
class GroundActor : public Actor
{
public:
	GroundActor();

protected:
	virtual void OnRender(const RenderContext& rc, float sx, float sy);

private:
	void Bake(const RenderContext& rc);

	TerrainLayer m_Terrain;
	StaticBatch  m_Decor;
	bool         m_Baked;
};

class PropActor : public Actor
{
public:
	PropActor(int type, float x, float y, float phase, float scale, float tint);

	int   type;
	float phase;
	float scale;   // 개체별 크기 편차. 같은 크기가 늘어서면 숲으로 안 보인다
	float tint;    // 개체별 색 변주 (0..1). 월드 좌표 해시라 셀 기준 로컬 좌표로는 못 구한다

protected:
	virtual void OnRender(const RenderContext& rc, float sx, float sy);

private:
	// 프롭은 움직이지 않지만 몬스터·플레이어와 깊이 순서가 섞이므로 정적
	// 배치로 뺄 수 없다. 대신 도형 목록을 처음 한 번 기록해 두고 매 프레임
	// 재생한다 (모델 관절 계산, 팔레트, 해시를 건너뛴다). 바람 흔들림과
	// 비침·대기 원근은 GPU 가 풀기 때문에 기록이 낡지 않는다.
	void Record(const RenderContext& rc);
	void DrawShapes(float sx, float sy);

	std::vector<ShapeInstance> m_Cache;
	bool m_Cached;
};

class MonsterActor : public Actor
{
public:
	MonsterActor(int species, float x, float y);

	int   species;
	float homeX, homeY;
	float tgtX, tgtY;
	float wait;
	float phase;
	float faceX;
	bool  moving;
	int   hp;
	float hurtFlash;
	float knockX, knockY;

	int   state;
	float stateT;      // 현재 상태 경과 시간
	float chargeX;     // 돌진 시작 시 고정한 방향
	float chargeY;
	float atkCd;

protected:
	virtual void OnUpdate(float dt);
	virtual void OnRender(const RenderContext& rc, float sx, float sy);

private:
	void SetState(int s);
	void Move(float vx, float vy, float dt);
	void WanderStep(float speed, float dt, float* mvx, float* mvy);
};

class DropActor : public Actor
{
public:
	DropActor(int kind, float x, float y);

	int   kind;
	float bob;

protected:
	virtual void OnUpdate(float dt);
	virtual void OnRender(const RenderContext& rc, float sx, float sy);
};

// 한 프레임의 사람 포즈. 몸과 손에 쥔 무기가 같은 값을 써야 무기가 손에서
// 떨어지지 않는다.
struct PlayerPose
{
	float ang[HJ_COUNT];
	float off[HJ_COUNT * 2];
	float squash;
	int   flags;       // 손 앞뒤 플래그
	float bob;
	float weaponDir;   // 국자가 뻗은 월드 방향 (라디안, 지면 평면)
	float weaponPitch; // 지면에서 올라간 각 (라디안). 음수면 머리가 아래
	float side;        // 손 쪽 회전 부호
	bool  swinging;    // 궤적을 그릴 구간 (내려치는 중)
};

class PlayerActor : public Actor
{
public:
	PlayerActor();

	void    GetPose(PlayerPose* out) const;
	Palette MakePalette() const;

	float faceX, faceY;
	float flipX;        // 마지막으로 향한 화면 좌우. 정면/후면 이동 중엔 유지된다
	float walkPhase;
	bool  walking;

	int   hp;
	int   level;
	int   xp;
	int   statPoints;

	int   str;   // 공격력
	int   vit;   // 최대 체력
	int   agi;   // 공격 속도 / 이동 속도

	float attackTimer;    // 남은 모션 시간
	float attackCooldown;
	bool  attackHitDone;

	float hurtFlash;
	float invuln;

	int   weapon;
	int   bag[IK_COUNT];

protected:
	virtual void OnUpdate(float dt);
	virtual void OnRender(const RenderContext& rc, float sx, float sy);
};

// 플레이어가 쥔 무기. 플레이어의 자식 노드라 위치는 부모를 따라오고, 모양은
// 부모의 포즈에서 앞손 관절 위치를 받아 그린다. 손이 몸통 앞에 있으면
// 플레이어 바로 뒤에, 뒤로 돌아가 있으면 바로 앞에 그려지도록 깊이
// 바이어스를 매 프레임 바꾼다.
class WeaponActor : public Actor
{
public:
	WeaponActor();

protected:
	virtual void OnUpdate(float dt);
	virtual void OnRender(const RenderContext& rc, float sx, float sy);
};

// 떠오르는 글자. 월드에 붙어 있지만 줌과 사후처리를 받으면 안 되므로
// 오버레이 레이어로 EndFrame 뒤에 그린다.
class FloatTextActor : public Actor
{
public:
	FloatTextActor(float x, float y, const wchar_t* s, const Col& c);

	float   life;
	wchar_t text[24];
	Col     color;

protected:
	virtual void OnUpdate(float dt);
	virtual void OnRender(const RenderContext& rc, float sx, float sy);
};

// -----------------------------------------------------------------------------
// 씬 그래프
//
//   root
//    +- ground          GroundActor
//    +- props           (비활성 — 갱신할 것이 없다)
//    |   +- cell[0,0]   PropActor ...     셀 원점 기준 로컬 좌표
//    |   +- cell[1,0]   PropActor ...
//    |   ...
//    +- player          PlayerActor
//    +- monsters        MonsterActor ...
//    +- drops           DropActor ...
//    +- texts           FloatTextActor ...
//
// 형제 순서가 곧 갱신 순서다. 플레이어가 먼저 움직이고 공격 판정을 낸 뒤
// 몬스터가 반응해야 이전 동작과 같다.
//
// 프롭은 수가 가장 많고(천 개 이상) 움직이지 않으므로 PROP_CELL 타일 단위
// 셀로 묶는다. 셀의 서브트리 경계는 한 번 계산하면 바뀌지 않고, 화면 밖
// 셀은 안의 프롭을 하나도 보지 않고 버려진다. 모닥불 회복 같은 주변 탐색도
// 주변 셀만 뒤진다.
//
// 몬스터·드롭은 움직이므로 셀에 넣으면 경계를 넘을 때마다 부모를 옮겨야
// 한다. 수가 적어 지금은 평평한 그룹으로 두고, 개체 단위 경계로만 컬링한다.
// -----------------------------------------------------------------------------

static const int PROP_CELL = 8;
static const int CELLS_X = (MAP_W + PROP_CELL - 1) / PROP_CELL;
static const int CELLS_Y = (MAP_H + PROP_CELL - 1) / PROP_CELL;

static SceneGraph   g_Scene;
static PlayerActor* g_Player   = 0;
static Actor*       g_PropGroup = 0;
static Actor*       g_PropCell[CELLS_Y][CELLS_X];
static Actor*       g_MonGroup  = 0;
static Actor*       g_DropGroup = 0;
static Actor*       g_TextGroup = 0;

static int   XpToNext(int level)           { return 25 + (level - 1) * 18; }
static int   MaxHp(const PlayerActor& p)   { return 40 + p.vit * 12; }
static int   AtkPower(const PlayerActor& p){ return 6 + p.str * 3; }
static float AtkCooldown(const PlayerActor& p)
{
	float cd = 0.55f - p.agi * 0.025f;
	return cd < 0.22f ? 0.22f : cd;
}
static float MoveSpeed(const PlayerActor& p) { return 4.20f + p.agi * 0.12f; }

static const float ATTACK_MOTION = 0.30f;

// 판정 시점(모션 비율). 내려친 국자가 바닥에 닿는 순간이다 (LadlePitch 의
// 내려치기 구간 끝). 들어올리는 동안에는 맞지 않는다.
static const float ATTACK_HIT_T  = 0.54f;

// -----------------------------------------------------------------------------
// 상태
// -----------------------------------------------------------------------------

static float g_Time;
static bool  g_Key[256];
static bool  g_StatPanel;
static int   g_Kills;
static int   g_MonSpawned[SP_COUNT];   // 모니터용 — 이번 맵에 처음 놓인 수


// -----------------------------------------------------------------------------
// 유틸
// -----------------------------------------------------------------------------

static float Hash(int x, int y)
{
	float n = sinf(x * 12.9898f + y * 78.233f) * 43758.5453f;
	return n - floorf(n);
}

static void IsoToScreen(float wx, float wy, float* sx, float* sy)
{
	*sx = (wx - wy) * (TILE_W * 0.5f);
	*sy = (wx + wy) * (TILE_H * 0.5f);
}

// 줌은 렌더러가 제출 직전에 걸어 준다. 하지만 텍스트처럼 EndFrame 뒤에
// 그리는 것은 줌이 꺼진 상태라, 좌표를 직접 옮겨 줘야 자리가 맞는다.
static void ZoomPoint(float* sx, float* sy)
{
	float ox = SCR_W * 0.5f;
	float oy = SCR_H * 0.5f;
	*sx = ox + (*sx - ox) * WORLD_ZOOM;
	*sy = oy + (*sy - oy) * WORLD_ZOOM;
}

static bool IsSolid(float wx, float wy)
{
	int tx = (int)floorf(wx);
	int ty = (int)floorf(wy);
	if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return true;
	return g_Solid[ty][tx];
}

static void AddText(float x, float y, const wchar_t* s, const Col& c)
{
	g_TextGroup->AddChild(new FloatTextActor(x, y, s, c));
}

static int CellIndex(float w, int count)
{
	int c = (int)floorf(w) / PROP_CELL;
	if (c < 0) c = 0;
	if (c >= count) c = count - 1;
	return c;
}

// 월드 좌표로 받아 해당 셀 아래에 셀 기준 로컬 좌표로 붙인다
static void AddProp(int type, float wx, float wy, float phase, float scale)
{
	Actor* cell = g_PropCell[CellIndex(wy, CELLS_Y)][CellIndex(wx, CELLS_X)];
	float tint = Hash((int)(wx * 7.0f), (int)(wy * 13.0f));
	cell->AddChild(new PropActor(type, wx - cell->GetX(), wy - cell->GetY(),
		phase, scale, tint));
}


// =============================================================================
// 맵 생성
// =============================================================================

struct Room
{
	int x, y, w, h;
};

static std::vector<Room> g_Rooms;

static void CarveRect(int x0, int y0, int x1, int y1, unsigned char tile)
{
	if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
	if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
	for (int y = y0; y <= y1; ++y)
	{
		for (int x = x0; x <= x1; ++x)
		{
			if (x < 1 || y < 1 || x >= MAP_W - 1 || y >= MAP_H - 1) continue;
			g_Tile[y][x] = tile;
		}
	}
}

static Room CarveRoom(int ax, int ay, int aw, int ah)
{
	int margin = 2;
	int w = RandInt(7, aw - margin * 2);
	int h = RandInt(7, ah - margin * 2);
	if (w < 5) w = 5;
	if (h < 5) h = 5;

	int x = ax + margin + RandInt(0, (aw - margin * 2) - w);
	int y = ay + margin + RandInt(0, (ah - margin * 2) - h);

	Room r;
	r.x = x; r.y = y; r.w = w; r.h = h;
	CarveRect(x, y, x + w - 1, y + h - 1, T_GRASS);
	g_Rooms.push_back(r);
	return r;
}

static void CarveCorridor(int x0, int y0, int x1, int y1)
{
	// L 자 통로. 폭 2 로 뚫어야 대각선 이동에 걸리지 않는다.
	if (RandInt(0, 1) == 0)
	{
		CarveRect(x0, y0, x1, y0 + 1, T_TRAIL);
		CarveRect(x1, y0, x1 + 1, y1, T_TRAIL);
	}
	else
	{
		CarveRect(x0, y0, x0 + 1, y1, T_TRAIL);
		CarveRect(x0, y1, x1, y1 + 1, T_TRAIL);
	}
}

// BSP. 부모가 두 자식의 대표 방을 반드시 이어 주므로 구조적으로 연결이 보장된다.
static Room SplitArea(int ax, int ay, int aw, int ah, int depth)
{
	if (depth <= 0 || (aw < 20 && ah < 20))
	{
		return CarveRoom(ax, ay, aw, ah);
	}

	bool horizontal = (aw >= ah);
	if (aw < 20) horizontal = false;
	if (ah < 20) horizontal = true;

	Room a, b;
	if (horizontal)
	{
		int cut = aw / 2 + RandInt(-aw / 8, aw / 8);
		if (cut < 10) cut = 10;
		if (cut > aw - 10) cut = aw - 10;
		a = SplitArea(ax, ay, cut, ah, depth - 1);
		b = SplitArea(ax + cut, ay, aw - cut, ah, depth - 1);
	}
	else
	{
		int cut = ah / 2 + RandInt(-ah / 8, ah / 8);
		if (cut < 10) cut = 10;
		if (cut > ah - 10) cut = ah - 10;
		a = SplitArea(ax, ay, aw, cut, depth - 1);
		b = SplitArea(ax, ay + cut, aw, ah - cut, depth - 1);
	}

	CarveCorridor(a.x + a.w / 2, a.y + a.h / 2, b.x + b.w / 2, b.y + b.h / 2);
	return (RandInt(0, 1) == 0) ? a : b;
}

// 생성 후 검증. 시작점에서 닿지 않는 바닥이 있으면 숲으로 되돌려 버린다.
// BSP 구조상 나올 일이 거의 없지만, "접근 불가한 영역이 없을 것"은 검증으로
// 보장해야지 설계로 믿을 일이 아니다.
static int FloodFillAndSeal(int startX, int startY)
{
	static bool seen[MAP_H][MAP_W];
	for (int y = 0; y < MAP_H; ++y)
		for (int x = 0; x < MAP_W; ++x)
			seen[y][x] = false;

	std::vector<int> stack;
	stack.push_back(startY * MAP_W + startX);
	seen[startY][startX] = true;

	int reached = 0;
	const int dx[4] = { 1, -1, 0, 0 };
	const int dy[4] = { 0, 0, 1, -1 };

	while (!stack.empty())
	{
		int idx = stack.back();
		stack.pop_back();
		int cx = idx % MAP_W;
		int cy = idx / MAP_W;
		++reached;

		for (int i = 0; i < 4; ++i)
		{
			int nx = cx + dx[i];
			int ny = cy + dy[i];
			if (nx < 0 || ny < 0 || nx >= MAP_W || ny >= MAP_H) continue;
			if (seen[ny][nx] || g_Solid[ny][nx]) continue;
			seen[ny][nx] = true;
			stack.push_back(ny * MAP_W + nx);
		}
	}

	int sealed = 0;
	for (int y = 0; y < MAP_H; ++y)
	{
		for (int x = 0; x < MAP_W; ++x)
		{
			if (!g_Solid[y][x] && !seen[y][x])
			{
				g_Tile[y][x] = T_THICK;
				g_Solid[y][x] = true;
				++sealed;
			}
		}
	}

	printf("Level1: reachable=%d sealed=%d\n", reached, sealed);
	return reached;
}

static void PlaceWaterAndFires()
{
	// 방 몇 개에 연못과 모닥불을 놓는다. 연못은 통행 불가라 방 전체를 덮지
	// 않도록 크기를 제한한다.
	for (size_t i = 0; i < g_Rooms.size(); ++i)
	{
		const Room& r = g_Rooms[i];

		if (r.w >= 9 && r.h >= 9 && RandF() < 0.45f)
		{
			int pw = RandInt(3, r.w / 2);
			int ph = RandInt(2, r.h / 2);
			int px = r.x + 1 + RandInt(0, r.w - pw - 2);
			int py = r.y + 1 + RandInt(0, r.h - ph - 2);
			for (int y = py; y < py + ph; ++y)
				for (int x = px; x < px + pw; ++x)
					g_Tile[y][x] = T_WATER;
		}

		if (RandF() < 0.5f)
		{
			int fx = r.x + r.w / 2;
			int fy = r.y + r.h / 2;
			if (g_Tile[fy][fx] == T_GRASS)
			{
				g_Tile[fy][fx] = T_EMBER;
				AddProp(PROP_FIRE, fx + 0.5f, fy + 0.5f, RandF() * 6.28f, 1.0f);
			}
		}
	}
}

static void RebuildSolid()
{
	for (int y = 0; y < MAP_H; ++y)
	{
		for (int x = 0; x < MAP_W; ++x)
		{
			unsigned char t = g_Tile[y][x];
			g_Solid[y][x] = (t == T_THICK || t == T_WATER);
		}
	}
}

static void PlaceProps()
{
	// 바닥과 맞닿은 숲 타일에만 나무를 세운다. 숲 내부까지 세우면 보이지도
	// 않는 개체 수천 개를 정렬하게 된다.
	for (int y = 1; y < MAP_H - 1; ++y)
	{
		for (int x = 1; x < MAP_W - 1; ++x)
		{
			if (g_Tile[y][x] != T_THICK) continue;

			bool edge =
				g_Tile[y][x - 1] != T_THICK || g_Tile[y][x + 1] != T_THICK ||
				g_Tile[y - 1][x] != T_THICK || g_Tile[y + 1][x] != T_THICK;
			if (!edge) continue;

			float r = Hash(x * 7, y * 13);

			int type;
			if (r > 0.86f)      type = PROP_ROCK;
			else if (r > 0.55f) type = PROP_PINE;
			else                type = PROP_TREE;

			// 타일 중앙에 정확히 꽂으면 격자가 드러난다
			float px = x + 0.30f + Hash(x * 3, y * 11) * 0.40f;
			float py = y + 0.30f + Hash(x * 11, y * 3) * 0.40f;
			float phase = Hash(x, y) * 6.28f;
			float scale = TREE_SCALE + Hash(x * 5, y * 17) * TREE_JITTER;
			AddProp(type, px, py, phase, scale);
		}
	}

	// 빈 땅 — 덤불과 돌멩이. 큰 나무만 있고 바닥이 비면 세트장처럼 보인다.
	for (int y = 1; y < MAP_H - 1; ++y)
	{
		for (int x = 1; x < MAP_W - 1; ++x)
		{
			if (g_Tile[y][x] != T_GRASS) continue;
			float r = Hash(x * 23, y * 31);
			if (r < 0.90f) continue;

			int type = (r > 0.965f) ? PROP_ROCK : PROP_BUSH;
			float px = x + 0.18f + Hash(x * 13, y * 7) * 0.64f;
			float py = y + 0.18f + Hash(x * 7, y * 13) * 0.64f;
			float phase = r * 6.28f;
			float scale = (type == PROP_ROCK ? 0.50f : 0.80f) + Hash(x * 19, y * 5) * 0.35f;
			AddProp(type, px, py, phase, scale);
		}
	}
}

static void SpawnMonsters()
{
	for (size_t i = 0; i < g_Rooms.size(); ++i)
	{
		const Room& r = g_Rooms[i];
		int count = RandInt(2, 5);

		for (int k = 0; k < count; ++k)
		{
			float mx = (float)(r.x + 1) + RandF() * (r.w - 2.0f);
			float my = (float)(r.y + 1) + RandF() * (r.h - 2.0f);
			if (IsSolid(mx, my)) continue;

			int species = (RandF() < 0.62f) ? SP_TOMATOMA : SP_BOAR;
			g_MonGroup->AddChild(new MonsterActor(species, mx, my));
			g_MonSpawned[species]++;
		}
	}
}

void Reset(unsigned int seed)
{
	double t0 = Profiler::NowMs();

	if (seed == 0) seed = (unsigned int)time(NULL);
	g_Seed = seed;
	g_Rng = seed;

	g_Rooms.clear();

	// 씬을 통째로 새로 짠다. 그룹을 붙이는 순서가 곧 갱신 순서다
	g_Scene.Clear();
	g_Scene.GetRoot()->AddChild(new GroundActor());
	g_PropGroup = g_Scene.CreateGroup();
	g_PropGroup->SetActive(false);
	for (int cy = 0; cy < CELLS_Y; ++cy)
	{
		for (int cx = 0; cx < CELLS_X; ++cx)
		{
			Actor* cell = g_Scene.CreateGroup(g_PropGroup);
			cell->SetPosition((float)(cx * PROP_CELL), (float)(cy * PROP_CELL));
			g_PropCell[cy][cx] = cell;
		}
	}
	g_Player    = new PlayerActor();
	g_Player->AddChild(new WeaponActor());   // 기본 무기 — 국자
	g_Scene.GetRoot()->AddChild(g_Player);
	g_MonGroup  = g_Scene.CreateGroup();
	g_DropGroup = g_Scene.CreateGroup();
	g_TextGroup = g_Scene.CreateGroup();

	for (int y = 0; y < MAP_H; ++y)
		for (int x = 0; x < MAP_W; ++x)
			g_Tile[y][x] = T_THICK;

	SplitArea(1, 1, MAP_W - 2, MAP_H - 2, 4);
	PlaceWaterAndFires();
	RebuildSolid();

	// 시작 지점은 첫 번째 방의 중앙
	int sx = MAP_W / 2, sy = MAP_H / 2;
	if (!g_Rooms.empty())
	{
		sx = g_Rooms[0].x + g_Rooms[0].w / 2;
		sy = g_Rooms[0].y + g_Rooms[0].h / 2;
	}
	if (g_Solid[sy][sx])
	{
		g_Tile[sy][sx] = T_GRASS;
		g_Solid[sy][sx] = false;
	}

	FloodFillAndSeal(sx, sy);

	PlaceProps();
	for (int s = 0; s < SP_COUNT; ++s) g_MonSpawned[s] = 0;
	SpawnMonsters();

	// 플레이어 — 스탯과 경험치는 유지하지 않고 레벨마다 새로 시작한다.
	// 스탯 초기값은 생성자가 잡는다
	g_Player->SetPosition(sx + 0.5f, sy + 0.5f);

	g_Time = 0.0f;
	g_Kills = 0;
	g_StatPanel = false;
	for (int i = 0; i < 256; ++i) g_Key[i] = false;

	g_Scene.Flush();

	int props = 0;
	for (int cy = 0; cy < CELLS_Y; ++cy)
		for (int cx = 0; cx < CELLS_X; ++cx)
			props += (int)g_PropCell[cy][cx]->GetChildren().size();

	printf("Level1: seed=%u rooms=%d monsters=%d props=%d actors=%d\n",
		g_Seed, (int)g_Rooms.size(),
		(int)g_MonGroup->GetChildren().size(), props,
		g_Scene.CountActors());

	// 맵 생성은 프레임 안에서 일어나면 (R 키) 그 프레임이 그대로 히치가 된다
	char buf[160];
	snprintf(buf, sizeof(buf),
		"\"seed\":%u,\"rooms\":%d,\"monsters\":%d,\"props\":%d,\"actors\":%d,\"ms\":%.3f",
		g_Seed, (int)g_Rooms.size(), (int)g_MonGroup->GetChildren().size(), props,
		g_Scene.CountActors(), Profiler::NowMs() - t0);
	Profiler::Event("level1.reset", buf);
}

void Init(Renderer* renderer, int screenW, int screenH)
{
	R = renderer;
	SCR_W = screenW;
	SCR_H = screenH;
	Models::Load();
	g_Scene.SetProjection(TILE_W, TILE_H, 1.0f);
	Reset(0);
}

// =============================================================================
// Actor 생성
// =============================================================================

PropActor::PropActor(int type_, float x, float y, float phase_, float scale_, float tint_)
	: type(type_), phase(phase_), scale(scale_), tint(tint_), m_Cached(false)
{
	static const ActorProfile prof = SceneGraph::RegisterActorProfile("prop");
	SetProfile(prof);
	SetPosition(x, y);

	// 나무가 크므로 위쪽 여유를 나무 높이만큼 잡는다. 밑동이 화면 아래로
	// 내려가도 수관은 아직 보인다.
	SetBounds(200.0f, 200.0f, 300.0f, 240.0f);
}

MonsterActor::MonsterActor(int species_, float x, float y)
	: species(species_),
	homeX(x), homeY(y), tgtX(x), tgtY(y),
	wait(RandF() * 2.0f), phase(RandF() * 6.28f),
	faceX(1.0f), moving(false),
	hp(g_Species[species_].maxHp),
	hurtFlash(0.0f), knockX(0.0f), knockY(0.0f),
	state(MS_WANDER), stateT(0.0f), chargeX(0.0f), chargeY(0.0f),
	atkCd(RandF() * 0.8f)
{
	static const ActorProfile prof = SceneGraph::RegisterActorProfile("monster");
	SetProfile(prof);
	SetPosition(x, y);
	SetBounds(200.0f, 200.0f, 220.0f, 220.0f);
}

DropActor::DropActor(int kind_, float x, float y)
	: kind(kind_), bob(RandF() * 6.28f)
{
	static const ActorProfile prof = SceneGraph::RegisterActorProfile("drop");
	SetProfile(prof);
	SetPosition(x, y);
	SetBounds(20.0f, 20.0f, 48.0f, 8.0f);
}

PlayerActor::PlayerActor()
	: faceX(0.0f), faceY(1.0f), flipX(1.0f),
	walkPhase(0.0f), walking(false),
	hp(0), level(1), xp(0), statPoints(0),
	str(1), vit(1), agi(1),
	attackTimer(-1.0f), attackCooldown(0.0f), attackHitDone(false),
	hurtFlash(0.0f), invuln(0.0f), weapon(WP_LADLE)
{
	static const ActorProfile prof = SceneGraph::RegisterActorProfile("player");
	SetProfile(prof);

	hp = MaxHp(*this);
	for (int i = 0; i < IK_COUNT; ++i) bag[i] = 0;
	SetBounds(60.0f, 60.0f, 110.0f, 20.0f);
}

WeaponActor::WeaponActor()
{
	static const ActorProfile prof = SceneGraph::RegisterActorProfile("weapon");
	SetProfile(prof);

	// 머리 위로 치켜든 국자(발에서 약 110px 위)와 내려친 궤적까지
	SetBounds(120.0f, 120.0f, 150.0f, 80.0f);
}

FloatTextActor::FloatTextActor(float x, float y, const wchar_t* s, const Col& c)
	: life(1.1f), color(c)
{
	static const ActorProfile prof = SceneGraph::RegisterActorProfile("float_text");
	SetProfile(prof);
	SetPosition(x, y);
	SetLayer(LAYER_OVERLAY);
	SetBounds(80.0f, 80.0f, 90.0f, 10.0f);
	wcsncpy_s(text, 24, s, _TRUNCATE);
}

// =============================================================================
// 전투 / 성장
// =============================================================================

static void GainXp(int amount)
{
	PlayerActor& p = *g_Player;
	p.xp += amount;

	while (p.xp >= XpToNext(p.level))
	{
		p.xp -= XpToNext(p.level);
		p.level++;
		p.statPoints += 2;
		p.hp = MaxHp(p);          // 레벨업 시 전체 회복

		wchar_t buf[24];
		swprintf_s(buf, 24, L"레벨 %d !", p.level);
		AddText(p.GetX(), p.GetY(), buf, Col(1.0f, 0.88f, 0.45f));
	}
}

static void DropLoot(const MonsterActor& m)
{
	int kind = (m.species == SP_BOAR) ? IK_MEAT : IK_BERRY;
	g_DropGroup->AddChild(new DropActor(kind, m.GetX(), m.GetY()));

	// 미식핵은 드물게
	if (RandF() < 0.10f)
	{
		g_DropGroup->AddChild(new DropActor(IK_CORE,
			m.GetX() + 0.35f, m.GetY() + 0.25f));
	}
}

static void DoAttackHit()
{
	const PlayerActor& p = *g_Player;
	const WeaponInfo& wp = g_Weapons[p.weapon];
	int dmg = AtkPower(p);
	float fx = p.faceX;
	float fy = p.faceY;
	float flen = sqrtf(fx * fx + fy * fy);
	if (flen < 0.001f) { fx = 0.0f; fy = 1.0f; flen = 1.0f; }
	fx /= flen;
	fy /= flen;

	bool hitAny = false;

	const std::vector<Actor*>& mons = g_MonGroup->GetChildren();
	for (size_t i = 0; i < mons.size(); ++i)
	{
		if (mons[i]->IsDestroyed()) continue;
		MonsterActor& m = *static_cast<MonsterActor*>(mons[i]);

		float dx = m.GetX() - p.GetX();
		float dy = m.GetY() - p.GetY();
		float d = sqrtf(dx * dx + dy * dy);
		if (d > wp.range || d < 0.001f) continue;

		// 정면 부채꼴. 폭은 무기가 정한다
		float dot = (dx / d) * fx + (dy / d) * fy;
		if (dot < wp.arcDot) continue;

		int roll = dmg + RandInt(-dmg / 6, dmg / 6);
		if (roll < 1) roll = 1;

		m.hp -= roll;
		m.hurtFlash = 0.22f;
		m.knockX = (dx / d) * 3.4f;
		m.knockY = (dy / d) * 3.4f;
		hitAny = true;

		wchar_t buf[24];
		swprintf_s(buf, 24, L"%d", roll);
		AddText(m.GetX(), m.GetY(), buf, Col(1.0f, 0.92f, 0.72f));

		if (m.hp <= 0)
		{
			m.Destroy();
			g_Kills++;
			GainXp(g_Species[m.species].xp);
			DropLoot(m);
		}
	}

	if (!hitAny)
	{
		// 허공을 갈라도 피드백은 준다
		AddText(p.GetX() + fx * wp.range * 0.7f, p.GetY() + fy * wp.range * 0.7f, L"휙",
			Col(0.85f, 0.85f, 0.80f, 0.8f));
	}
}

static void HurtPlayer(int amount)
{
	PlayerActor& p = *g_Player;
	if (p.invuln > 0.0f) return;

	p.hp -= amount;
	p.hurtFlash = 0.35f;
	p.invuln = 0.7f;

	wchar_t buf[24];
	swprintf_s(buf, 24, L"-%d", amount);
	AddText(p.GetX(), p.GetY() - 0.3f, buf, Col(1.0f, 0.50f, 0.42f));

	if (p.hp <= 0)
	{
		// 사망 — 경험치는 유지하고 시작 지점에서 부활한다.
		// 경험치 쌓기가 목적인 레벨이라 사망 페널티는 두지 않는다.
		p.hp = MaxHp(p);
		if (!g_Rooms.empty())
		{
			p.SetPosition(g_Rooms[0].x + g_Rooms[0].w * 0.5f,
				g_Rooms[0].y + g_Rooms[0].h * 0.5f);
		}
		AddText(p.GetX(), p.GetY(), L"쓰러졌다", Col(1.0f, 0.6f, 0.5f));
	}
}

static void SpendStat(int which)
{
	PlayerActor& p = *g_Player;
	if (p.statPoints <= 0) return;

	if (which == 0) p.str++;
	else if (which == 1) { p.vit++; p.hp += 12; }
	else if (which == 2) p.agi++;
	else return;

	p.statPoints--;
}

// =============================================================================
// 갱신
// =============================================================================

static const float MON_SEP_RADIUS   = 1.15f;   // 이 안으로 들어오면 서로 밀어낸다
static const float MON_SEP_STRENGTH = 2.6f;
static const float PLAYER_CLEARANCE = 0.72f;   // 플레이어 몸 안으로 못 들어온다

static const float BOAR_STANDOFF = 1.55f;
static const float BOAR_WINDUP_T = 0.34f;
static const float BOAR_CHARGE_T = 0.34f;
static const float BOAR_CHARGE_SPEED = 6.6f;
static const float BOAR_RECOVER_T = 0.80f;
static const float BOAR_ATK_CD = 1.10f;

// 비선공 도망. 진입/해제 거리를 벌려 경계에서 떨지 않게 한다.
static const float FLEE_NEAR  = 1.80f;
static const float FLEE_FAR   = 3.40f;
static const float FLEE_MAX_T = 2.40f;

// ---- 플레이어 -----------------------------------------------------------

void PlayerActor::OnUpdate(float dt)
{
	if (hurtFlash > 0.0f) hurtFlash -= dt;
	if (invuln > 0.0f)    invuln -= dt;
	if (attackCooldown > 0.0f) attackCooldown -= dt;

	// 공격 모션 진행. 모션 중간(40%)에 판정이 한 번 들어간다.
	if (attackTimer >= 0.0f)
	{
		attackTimer += dt;
		float t = attackTimer / ATTACK_MOTION;
		if (!attackHitDone && t >= ATTACK_HIT_T)
		{
			attackHitDone = true;
			DoAttackHit();
		}
		if (t >= 1.0f) attackTimer = -1.0f;
	}

	float mx = 0.0f, my = 0.0f;
	if (g_Key['w'] || g_Key['W']) { mx -= 1.0f; my -= 1.0f; }
	if (g_Key['s'] || g_Key['S']) { mx += 1.0f; my += 1.0f; }
	if (g_Key['a'] || g_Key['A']) { mx -= 1.0f; my += 1.0f; }
	if (g_Key['d'] || g_Key['D']) { mx += 1.0f; my -= 1.0f; }

	float len = sqrtf(mx * mx + my * my);
	walking = false;

	if (len > 0.0001f && !g_StatPanel)
	{
		mx /= len;
		my /= len;
		faceX = mx;
		faceY = my;

		// 화면 좌우는 월드 축이 아니라 (x - y) 다. 이 값이 0인 정면/후면
		// 이동에서는 직전 방향을 유지해야 제자리에서 뒤집히지 않는다.
		float screenH = mx - my;
		if (screenH < -0.05f)     flipX = -1.0f;
		else if (screenH > 0.05f) flipX =  1.0f;

		float step = MoveSpeed(*this) * dt;
		float nx = GetX() + mx * step;
		float ny = GetY() + my * step;
		if (!IsSolid(nx, GetY())) SetX(nx);
		if (!IsSolid(GetX(), ny)) SetY(ny);

		walking = true;
		walkPhase += dt * 9.5f;
	}
	else
	{
		walkPhase += dt * 1.6f;
	}

	// 모닥불 근처에서는 체력이 천천히 찬다. 회복 반경(2.5 타일)이 셀보다
	// 작으므로 주변 3x3 셀만 보면 된다
	int pcx = CellIndex(GetX(), CELLS_X);
	int pcy = CellIndex(GetY(), CELLS_Y);
	bool nearFire = false;
	for (int cy = pcy - 1; cy <= pcy + 1 && !nearFire; ++cy)
	{
		for (int cx = pcx - 1; cx <= pcx + 1 && !nearFire; ++cx)
		{
			if (cx < 0 || cy < 0 || cx >= CELLS_X || cy >= CELLS_Y) continue;
			const Actor* cell = g_PropCell[cy][cx];
			const std::vector<Actor*>& props = cell->GetChildren();
			for (size_t i = 0; i < props.size(); ++i)
			{
				const PropActor* pr = static_cast<const PropActor*>(props[i]);
				if (pr->type != PROP_FIRE) continue;
				float dx = cell->GetX() + pr->GetX() - GetX();
				float dy = cell->GetY() + pr->GetY() - GetY();
				if (dx * dx + dy * dy < 6.0f) { nearFire = true; break; }
			}
		}
	}
	if (nearFire)
	{
		static float heal = 0.0f;
		heal += dt;
		if (heal > 0.7f)
		{
			heal = 0.0f;
			if (hp < MaxHp(*this)) hp += 2;
		}
	}
}

// ---- 몬스터 -------------------------------------------------------------

void MonsterActor::SetState(int s)
{
	state = s;
	stateT = 0.0f;
}

void MonsterActor::Move(float vx, float vy, float dt)
{
	float nx = GetX() + vx * dt;
	float ny = GetY() + vy * dt;
	if (!IsSolid(nx, GetY())) SetX(nx);
	if (!IsSolid(GetX(), ny)) SetY(ny);
}

// 자기 영역 안에서 목적지를 찍고 걸어간다. 도착하면 잠깐 쉰다.
void MonsterActor::WanderStep(float speed, float dt, float* mvx, float* mvy)
{
	if (wait > 0.0f)
	{
		wait -= dt;
		return;
	}

	float dx = tgtX - GetX();
	float dy = tgtY - GetY();
	float d = sqrtf(dx * dx + dy * dy);

	if (d < 0.18f)
	{
		float a = RandF() * 6.28f;
		float rr = 0.8f + RandF() * 2.6f;
		tgtX = homeX + cosf(a) * rr;
		tgtY = homeY + sinf(a) * rr;
		wait = 0.6f + RandF() * 2.2f;
	}
	else
	{
		*mvx = dx / d * speed;
		*mvy = dy / d * speed;
	}
}

void MonsterActor::OnUpdate(float dt)
{
	const SpeciesInfo& si = g_Species[species];
	const PlayerActor& p = *g_Player;

	stateT += dt;
	if (atkCd > 0.0f) atkCd -= dt;
	if (hurtFlash > 0.0f) hurtFlash -= dt;
	phase += dt * (moving ? 7.0f : 2.2f);

	// 넉백은 의도한 이동과 별개로 바로 밀어낸다
	if (knockX != 0.0f || knockY != 0.0f)
	{
		Move(knockX, knockY, dt);

		float decay = 1.0f - dt * 7.0f;
		if (decay < 0.0f) decay = 0.0f;
		knockX *= decay;
		knockY *= decay;
		if (fabsf(knockX) < 0.05f) knockX = 0.0f;
		if (fabsf(knockY) < 0.05f) knockY = 0.0f;
	}

	float pdx = p.GetX() - GetX();
	float pdy = p.GetY() - GetY();
	float pd = sqrtf(pdx * pdx + pdy * pdy);
	float pnx = (pd > 0.001f) ? pdx / pd : 0.0f;
	float pny = (pd > 0.001f) ? pdy / pd : 1.0f;

	float mvx = 0.0f, mvy = 0.0f;

	if (si.contactDamage > 0)
	{
		// 개체마다 대기 거리와 도는 방향을 다르게 준다. 전부 같은 값이면
		// 한 무리가 같은 반지름의 원에 몰려 서로를 밀어내며 떨린다.
		// phase 는 스폰 때 무작위로 잡히므로 그대로 개체 식별자로 쓴다.
		float side = (phase > PI_F) ? 1.0f : -1.0f;
		float standoff = 1.35f + (phase / 6.2832f) * 0.95f;

		switch (state)
		{
		case MS_WANDER:
			WanderStep(si.speed * 0.45f, dt, &mvx, &mvy);
			if (pd < si.aggroRange) SetState(MS_CHASE);
			break;

		case MS_CHASE:
			if (pd > si.aggroRange * 1.4f)
			{
				SetState(MS_WANDER);
			}
			else if (pd > standoff)
			{
				mvx = pnx * si.speed;
				mvy = pny * si.speed;
			}
			else if (atkCd <= 0.0f)
			{
				SetState(MS_WINDUP);
			}
			else
			{
				// 사거리 안이지만 아직 못 덤빈다. 파고들지 말고 옆으로 돈다.
				mvx = -pny * si.speed * 0.55f * side;
				mvy =  pnx * si.speed * 0.55f * side;
			}
			break;

		case MS_WINDUP:
			// 제자리에서 움츠린다. 예고 동작이 있어야 돌진이 읽힌다.
			if (stateT >= BOAR_WINDUP_T)
			{
				// 정면으로 조준하면 플레이어 몸 한가운데로 박히고, 밀어내기와
				// 맞물려 돌진 내내 몸에 갈리게 된다. 옆으로 빗겨 조준해서
				// 스쳐 지나가도록 만든다.
				float ax = pnx - pny * 0.45f * side;
				float ay = pny + pnx * 0.45f * side;
				float al = sqrtf(ax * ax + ay * ay);
				if (al < 0.001f) { ax = pnx; ay = pny; al = 1.0f; }
				chargeX = ax / al;
				chargeY = ay / al;
				SetState(MS_CHARGE);
			}
			break;

		case MS_CHARGE:
			// 방향을 고정하고 직진한다. 매 프레임 조준하면 플레이어를
			// 따라붙어 비비는 모양이 된다.
			mvx = chargeX * BOAR_CHARGE_SPEED;
			mvy = chargeY * BOAR_CHARGE_SPEED;
			if (pd < 1.0f) HurtPlayer(si.contactDamage);
			if (stateT >= BOAR_CHARGE_T)
			{
				// 쿨다운에 편차를 준다. 고정값이면 한 번 동기화된 무리가
				// 계속 같은 박자로 달려든다.
				atkCd = BOAR_ATK_CD + RandF() * 0.9f;
				SetState(MS_RECOVER);
			}
			break;

		case MS_RECOVER:
		{
			// 관성으로 지나쳐 간 뒤 서서히 멈춘다. 곧장 플레이어 쪽으로
			// 방향을 되돌리면 다시 달라붙는다.
			float k = 1.0f - (stateT / BOAR_RECOVER_T);
			if (k < 0.0f) k = 0.0f;
			mvx = chargeX * si.speed * 1.10f * k;
			mvy = chargeY * si.speed * 1.10f * k;
			if (stateT >= BOAR_RECOVER_T) SetState(MS_CHASE);
			break;
		}

		default:
			SetState(MS_WANDER);
			break;
		}
	}
	else
	{
		// 비선공 — 가까이 오면 물러난다.
		//
		// 예전에는 pd < 2.0 한 줄로 도망 여부를 매 프레임 다시 판단했다.
		// 문제가 둘이었다. 경계선에서 도망과 배회가 프레임마다 번갈아
		// 켜지면서 방향이 뒤집혔고, 도망치는 동안 배회 목표(tgt)가
		// 플레이어 뒤쪽에 그대로 남아 있어서 도망이 끝나자마자 플레이어
		// 쪽으로 되돌아왔다. 플레이어가 가만히 서 있어도 다가왔다
		// 물러나기를 끝없이 반복하던 것의 정체가 이것이다.
		//
		// 상태로 분리하고, 진입 거리보다 해제 거리를 훨씬 크게 잡는다.
		if (state != MS_FLEE && pd < FLEE_NEAR && pd > 0.01f)
		{
			SetState(MS_FLEE);
			wait = 0.0f;
		}

		if (state == MS_FLEE)
		{
			if (pd > FLEE_FAR || stateT > FLEE_MAX_T)
			{
				SetState(MS_WANDER);

				// 도망친 자리를 새 영역으로 삼는다. 원래 영역을 그대로
				// 두면 플레이어를 다시 통과해 돌아온다.
				homeX = GetX();
				homeY = GetY();
				tgtX = GetX();
				tgtY = GetY();
				wait = 0.5f + RandF() * 1.2f;
			}
			else
			{
				float ax = -pnx;
				float ay = -pny;

				// 벽에 몰리면 벽을 따라 미끄러진다. 막힌 방향으로 계속
				// 밀면 제자리에서 떤다.
				if (IsSolid(GetX() + ax * 0.7f, GetY() + ay * 0.7f))
				{
					float tx = -ay, ty = ax;
					if (IsSolid(GetX() + tx * 0.7f, GetY() + ty * 0.7f))
					{
						tx = ay; ty = -ax;
					}
					ax = tx; ay = ty;
				}

				// 가까울수록 다급하게
				float boost = 1.0f + (FLEE_NEAR - pd) * 0.75f;
				if (boost > 2.20f) boost = 2.20f;
				mvx = ax * si.speed * boost;
				mvy = ay * si.speed * boost;
			}
		}
		else
		{
			WanderStep(si.speed, dt, &mvx, &mvy);
		}
	}

	// 서로 밀어내기. 같은 목표를 향해 몰려오면 한 점에 쌓여 한 마리처럼
	// 보이고, 겹친 채로 서로의 이동을 방해해 떨림이 생긴다.
	float sepX = 0.0f, sepY = 0.0f;
	const std::vector<Actor*>& mons = GetParent()->GetChildren();

	// 몬스터끼리 전부 비교한다 (N^2). 수가 늘면 제일 먼저 터질 곳이라
	// 비교 횟수를 그대로 남긴다. 20,000 을 넘으면 공간 격자가 필요하다.
	PROFILE_COUNT("level1.monster_sep_pairs", mons.size() - 1);
	for (size_t k = 0; k < mons.size(); ++k)
	{
		const Actor* o = mons[k];
		if (o == this || o->IsDestroyed()) continue;

		float dx = GetX() - o->GetX();
		float dy = GetY() - o->GetY();
		float d2 = dx * dx + dy * dy;
		if (d2 > MON_SEP_RADIUS * MON_SEP_RADIUS || d2 < 0.0001f) continue;

		float d = sqrtf(d2);
		float push = (MON_SEP_RADIUS - d) / MON_SEP_RADIUS;
		sepX += (dx / d) * push;
		sepY += (dy / d) * push;
	}
	mvx += sepX * MON_SEP_STRENGTH;
	mvy += sepY * MON_SEP_STRENGTH;

	moving = (fabsf(mvx) > 0.05f || fabsf(mvy) > 0.05f);
	if (moving) Move(mvx, mvy, dt);

	// 플레이어 몸 안으로 파고들지 못하게 되민다.
	//
	// 단 돌진 중에는 끈다. 돌진은 2.2 타일을 달려 플레이어를 통과하는
	// 동작인데 여기서 막아 버리면, 밀어내기와 전진이 맞서면서 돌진 내내
	// 몸에 밀착해 갈리는 모양이 된다.
	pdx = p.GetX() - GetX();
	pdy = p.GetY() - GetY();
	pd = sqrtf(pdx * pdx + pdy * pdy);
	if (state != MS_CHARGE && pd < PLAYER_CLEARANCE && pd > 0.001f)
	{
		float back = PLAYER_CLEARANCE - pd;
		float bx = GetX() - (pdx / pd) * back;
		float by = GetY() - (pdy / pd) * back;
		if (!IsSolid(bx, GetY())) SetX(bx);
		if (!IsSolid(GetX(), by)) SetY(by);
	}

	// 방향 전환에 여유를 둔다. 임계값이 0.02 면 정지 근처의 미세한
	// 떨림에도 좌우가 뒤집혀 깜빡이는 것처럼 보인다.
	if (mvx > 0.35f)       faceX =  1.0f;
	else if (mvx < -0.35f) faceX = -1.0f;
}

// ---- 드롭 / 글자 --------------------------------------------------------

void DropActor::OnUpdate(float dt)
{
	PlayerActor& p = *g_Player;

	bob += dt * 3.0f;

	float dx = p.GetX() - GetX();
	float dy = p.GetY() - GetY();
	float dist = sqrtf(dx * dx + dy * dy);

	// 가까우면 빨려 들어온다. 줍는 조작을 따로 두지 않는다.
	if (dist < 1.6f && dist > 0.001f)
	{
		Translate((dx / dist) * 6.0f * dt, (dy / dist) * 6.0f * dt);
	}

	if (dist < 0.45f)
	{
		Destroy();
		p.bag[kind]++;

		if (kind == IK_CORE)
		{
			// 미식핵은 스탯 포인트를 직접 준다
			p.statPoints++;
			AddText(GetX(), GetY(), L"미식핵 +1", Col(0.98f, 0.86f, 0.44f));
		}
		else
		{
			GainXp(3);
			AddText(GetX(), GetY(), g_Items[kind].name, Col(0.86f, 0.92f, 0.80f));
		}
	}
}

void FloatTextActor::OnUpdate(float dt)
{
	life -= dt;
	Translate(0.0f, -dt * 0.5f);
	if (life <= 0.0f) Destroy();
}

// ---- 진입점 -------------------------------------------------------------

void Update(float dt)
{
	PROFILE_SCOPE("level1.update");

	g_Time += dt;
	g_Scene.Update(dt);

	PROFILE_GAUGE("level1.monsters_alive", g_MonGroup->GetChildren().size());
	PROFILE_GAUGE("level1.drops", g_DropGroup->GetChildren().size());
	PROFILE_GAUGE("level1.float_texts", g_TextGroup->GetChildren().size());
}

void SetKey(unsigned char key, bool down)
{
	g_Key[key] = down;
}

void KeyPressed(unsigned char key)
{
	PlayerActor& p = *g_Player;

	if (key == ' ' || key == 'e' || key == 'E')
	{
		if (p.attackCooldown <= 0.0f && p.attackTimer < 0.0f && !g_StatPanel)
		{
			p.attackTimer = 0.0f;
			p.attackHitDone = false;
			p.attackCooldown = AtkCooldown(p);
		}
	}
	else if (key == '\t')
	{
		g_StatPanel = !g_StatPanel;
	}
	else if (key == '1') SpendStat(0);
	else if (key == '2') SpendStat(1);
	else if (key == '3') SpendStat(2);
	else if (key == 'r' || key == 'R') Reset(0);
}


// =============================================================================
// 그리기
// =============================================================================

// ---- 곡선 지형 --------------------------------------------------------------
//
// 경계가 얼마나 휘는지(타일 단위)와 얼마나 또렷한지. 휨을 0.5 가까이 올리면
// 한 칸짜리 길이 끊어져 보이기 시작한다.
static const float TERRAIN_WARP  = 0.38f;
static const float TERRAIN_SHARP = 7.0f;
static const int   TERRAIN_TYPES = 5;   // T_THICK .. T_EMBER

// 아래 세 함수는 Shaders/Ground.fs 의 Hash / Noise / 가중치 계산과 똑같아야
// 한다. 화면에 보이는 곡선 경계와 잡초를 심을 자리를 맞추는 데 쓴다.
static float TerrainHash(int x, int y)
{
	unsigned int h = (unsigned int)x * 1597334677u ^ (unsigned int)y * 3812015801u;
	h = (h ^ (h >> 15)) * 2246822519u;
	h = (h ^ (h >> 13)) * 3266489917u;
	h ^= h >> 16;
	return (float)(h & 0x00FFFFFFu) / 16777215.0f;
}

static float TerrainNoise(float x, float y)
{
	float ix = floorf(x), iy = floorf(y);
	float fx = x - ix, fy = y - iy;
	fx = fx * fx * (3.0f - 2.0f * fx);
	fy = fy * fy * (3.0f - 2.0f * fy);
	int cx = (int)ix, cy = (int)iy;
	float a = TerrainHash(cx, cy);
	float b = TerrainHash(cx + 1, cy);
	float d = TerrainHash(cx, cy + 1);
	float e = TerrainHash(cx + 1, cy + 1);
	float lo = a + (b - a) * fx;
	float hi = d + (e - d) * fx;
	return lo + (hi - lo) * fy;
}

static int TileAtClamped(int x, int y)
{
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x >= MAP_W) x = MAP_W - 1;
	if (y >= MAP_H) y = MAP_H - 1;
	return g_Tile[y][x];
}

// 월드 좌표(타일 단위)에서 화면에 보이는 지형 종류
static int TerrainTypeAt(float wx, float wy)
{
	float px = wx + (TerrainNoise(wx * 0.9f, wy * 0.9f) - 0.5f) * 2.0f * TERRAIN_WARP;
	float py = wy + (TerrainNoise(wx * 0.9f + 17.3f, wy * 0.9f + 5.1f) - 0.5f) * 2.0f * TERRAIN_WARP;

	float qx = px - 0.5f, qy = py - 0.5f;
	float ix = floorf(qx), iy = floorf(qy);
	float fx = qx - ix, fy = qy - iy;
	fx = fx * fx * (3.0f - 2.0f * fx);
	fy = fy * fy * (3.0f - 2.0f * fy);
	int cx = (int)ix, cy = (int)iy;

	float wt[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	wt[TileAtClamped(cx, cy)]         += (1.0f - fx) * (1.0f - fy);
	wt[TileAtClamped(cx + 1, cy)]     += fx * (1.0f - fy);
	wt[TileAtClamped(cx, cy + 1)]     += (1.0f - fx) * fy;
	wt[TileAtClamped(cx + 1, cy + 1)] += fx * fy;

	int best = 0;
	for (int k = 1; k < TERRAIN_TYPES; ++k)
		if (wt[k] > wt[best]) best = k;
	return best;
}

GroundActor::GroundActor()
	: m_Baked(false)
{
	static const ActorProfile prof = SceneGraph::RegisterActorProfile("ground");
	SetProfile(prof);

	SetLayer(LAYER_GROUND);

	// 맵 전체가 투영되는 마름모의 외접 사각형 (원점 = 타일 (0,0) 모서리)
	SetBounds(MAP_H * TILE_W * 0.5f + TILE_W, MAP_W * TILE_W * 0.5f + TILE_W,
		TILE_H, (MAP_W + MAP_H) * TILE_H * 0.5f + TILE_H);
}

void GroundActor::Bake(const RenderContext& rc)
{
	double t0 = Profiler::NowMs();

	// ---- 바닥 -----------------------------------------------------------------
	m_Terrain.SetTiles(&g_Tile[0][0], MAP_W, MAP_H);
	m_Terrain.SetTypeCount(TERRAIN_TYPES);
	m_Terrain.SetTypeColor(T_THICK, C_THICK);
	m_Terrain.SetTypeColor(T_GRASS, C_GRASS);
	m_Terrain.SetTypeColor(T_TRAIL, C_TRAIL);
	m_Terrain.SetTypeColor(T_WATER, C_WATER);
	m_Terrain.SetTypeColor(T_EMBER, C_EMBER);
	m_Terrain.SetWaterType(T_WATER);
	m_Terrain.SetShape(TERRAIN_WARP, TERRAIN_SHARP);

	// ---- 장식 -----------------------------------------------------------------
	// 자리는 예전처럼 타일 해시로 정하되, 곡선 경계를 따라 실제로 그 지형인
	// 곳에만 심는다. 안 그러면 경계가 휘어 들어간 물 위에 잡초가 뜬다.
	const float hw = TILE_W * 0.5f;
	const float hh = TILE_H * 0.5f;
	int planted = 0, skipped = 0;

	R->BeginRecord(&m_Decor);

	for (int y = 0; y < MAP_H; ++y)
	{
		for (int x = 0; x < MAP_W; ++x)
		{
			unsigned char t = g_Tile[y][x];
			if (t != T_GRASS && t != T_TRAIL) continue;

			float sx, sy;
			rc.Project((float)x + 0.5f, (float)y + 0.5f, 0.0f, &sx, &sy);

			if (t == T_GRASS)
			{
				// 잡초는 타일당 최대 세 포기. 한 포기씩만 찍으면 오히려
				// 격자 간격이 드러난다.
				for (int k = 0; k < 3; ++k)
				{
					if (Hash(x * 3 + k * 37, y * 5 + k * 91) < 0.62f) continue;
					float ox = (Hash(x * 7 + k * 13, y + k * 53) - 0.5f) * 44.0f;
					float oy = (Hash(x + k * 71, y * 9 + k * 17) - 0.5f) * 20.0f;

					// 화면 오프셋 -> 월드 오프셋 (밑동 기준)
					float wx = x + 0.5f + (ox / hw + oy / hh) * 0.5f;
					float wy = y + 0.5f + (oy / hh - ox / hw) * 0.5f;
					if (TerrainTypeAt(wx, wy) != T_GRASS) { ++skipped; continue; }

					float th = 7.0f + Hash(x * 5 + k, y * 3 + k) * 6.0f;
					Col tuft = Shade(C_GRASS, 1.10f + Hash(x + k, y + k) * 0.30f);
					R->DrawTriangle(sx + ox, sy + oy - th * 0.4f, 4.5f, th, tuft);
					++planted;
				}
			}
			else
			{
				for (int k = 0; k < 2; ++k)
				{
					if (Hash(x * 9 + k * 29, y * 3 + k * 61) < 0.70f) continue;
					float ox = (Hash(x * 5 + k * 7, y * 7 + k) - 0.5f) * 46.0f;
					float oy = (Hash(x * 2 + k, y * 5 + k * 3) - 0.5f) * 20.0f;

					float wx = x + 0.5f + (ox / hw + oy / hh) * 0.5f;
					float wy = y + 0.5f + (oy / hh - ox / hw) * 0.5f;
					if (TerrainTypeAt(wx, wy) != T_TRAIL) { ++skipped; continue; }

					R->DrawEllipse(sx + ox, sy + oy, 7.0f + k * 3.0f, 4.0f,
						Shade(C_TRAIL, 0.84f));
					++planted;
				}
			}
		}
	}

	R->EndRecord();
	m_Baked = true;

	char buf[128];
	snprintf(buf, sizeof(buf), "\"decor\":%d,\"decor_skipped\":%d,\"ms\":%.3f",
		planted, skipped, Profiler::NowMs() - t0);
	Profiler::Event("level1.ground_bake", buf);
}

void GroundActor::OnRender(const RenderContext& rc, float, float)
{
	if (!m_Baked) Bake(rc);

	R->DrawTerrain(m_Terrain, rc.offX, rc.offY, TILE_W, TILE_H);
	R->DrawStatic(m_Decor, rc.offX, rc.offY);
}

// 도형 목록을 원점 기준(카메라 없이) 좌표로 그린다. 기록할 때 한 번만 쓴다.
void PropActor::DrawShapes(float sx, float sy)
{
	Palette pal;
	float k = scale;
	float v = tint;
	float depth = GetDepth();

	// 비침은 나무만 — 바위와 덤불은 플레이어를 가릴 만큼 크지 않다
	bool tall = (type == PROP_TREE || type == PROP_PINE);
	int shadowFlags = tall ? ANCHOR_SEETHRU : 0;
	int bodyFlags = ANCHOR_HAZE | (tall ? ANCHOR_SEETHRU : 0);

	if (type == PROP_TREE || type == PROP_PINE || type == PROP_BUSH)
	{
		// 활엽수는 노랑을 뺀 중간 녹색, 침엽수는 더 어둡고 푸른 녹색.
		// 둘의 색온도가 갈려야 숲이 한 덩어리로 안 보인다.
		if (type == PROP_PINE)
			pal.c[CS_CLOTH] = Col(0.25f + v * 0.06f, 0.41f + v * 0.09f, 0.32f + v * 0.06f);
		else
			pal.c[CS_CLOTH] = Col(0.33f + v * 0.10f, 0.51f + v * 0.12f, 0.33f + v * 0.07f);
		pal.c[CS_DARK] = Col(0.38f, 0.30f, 0.23f);

		if (type == PROP_BUSH)
		{
			R->DrawShadow(sx + 2.0f, sy + 1.0f, 28.0f * k, 13.0f * k, 0.26f, 0.28f);
			R->SetAnchor(sx, sy, depth, bodyFlags);
			Models::Draw(R, MODEL_BUSH, sx, sy, NULL, NULL, pal, k, 1.0f, 1.0f, 0xFF, 1.0f);
		}
		else
		{
			// 그림자는 줄기 밑동 기준. 수관 폭으로 깔면 나무가 떠 보인다.
			R->SetAnchor(sx, sy, depth, shadowFlags);
			R->DrawShadow(sx + 4.0f * k, sy + 2.0f * k, 44.0f * k, 20.0f * k, 0.30f, 0.26f);

			// 바람 — 수관 관절(1)을 축으로 sin(t * 0.55 + phase) * 0.03 rad
			R->SetAnchor(sx, sy, depth, bodyFlags);
			Models::DrawSwaying(R, type == PROP_PINE ? MODEL_PINE : MODEL_TREE,
				sx, sy, pal, k, 1.0f, 1, phase, 0.030f, 0.55f);
		}
	}
	else
	{
		pal.c[CS_CLOTH] = Col(0.60f + v * 0.08f, 0.60f + v * 0.07f, 0.58f + v * 0.07f);
		R->DrawShadow(sx + 2.0f * k, sy + 1.0f, 32.0f * k, 15.0f * k, 0.28f, 0.26f);
		R->SetAnchor(sx, sy, depth, bodyFlags);
		Models::Draw(R, MODEL_ROCK, sx, sy, NULL, NULL, pal, k, 1.0f, 1.0f, 0xFF, 1.0f);
	}

	R->ClearAnchor();
}

void PropActor::Record(const RenderContext& rc)
{
	float x0, y0;
	rc.Project(GetWorldX(), GetWorldY(), GetWorldZ(), &x0, &y0);

	R->BeginRecord(&m_Cache);
	DrawShapes(x0, y0);
	R->EndRecord();
	m_Cached = true;

	PROFILE_COUNT("level1.prop_cache_builds", 1);
	PROFILE_COUNT("level1.prop_cache_instances", m_Cache.size());
}

void PropActor::OnRender(const RenderContext& rc, float sx, float sy)
{
	if (type == PROP_FIRE)
	{
		// 모닥불은 불빛 세기가 매 프레임 CPU 에서 변하므로 기록하지 않는다.
		// 몇 개 안 된다.
		R->DrawShadow(sx, sy, 46.0f, 22.0f, 0.34f, 0.22f);
		R->DrawEllipse(sx, sy - 4.0f, 40.0f, 16.0f,
			Col(0.34f, 0.30f, 0.27f), Col(0.20f, 0.18f, 0.16f));
		for (int i = 0; i < 3; ++i)
		{
			float a = phase + i * 2.1f;
			R->DrawRect(sx + cosf(a) * 11.0f, sy - 7.0f, 16.0f, 5.0f,
				Col(0.36f, 0.26f, 0.18f), Col(0.24f, 0.17f, 0.12f), 0.5f);
		}
		R->DrawFlame(sx, sy - 34.0f, 40.0f, 58.0f, rc.time + phase, 1.05f, 1.0f);
		R->DrawFlame(sx + 7.0f, sy - 26.0f, 22.0f, 34.0f, rc.time * 1.3f + phase, 0.85f, 0.85f);
		R->DrawEllipse(sx, sy - 6.0f, 78.0f, 34.0f,
			Fade(Col(1.0f, 0.66f, 0.28f), 0.16f + sinf(rc.time * 6.0f) * 0.03f));
		return;
	}

	if (!m_Cached) Record(rc);
	R->SubmitCached(m_Cache, rc.offX, rc.offY);
}

void MonsterActor::OnRender(const RenderContext&, float sx, float sy)
{
	const SpeciesInfo& si = g_Species[species];

	Palette pal;
	pal.c[CS_CLOTH]  = si.body;
	pal.c[CS_ACCENT] = si.accent;
	pal.c[CS_DARK]   = Shade(si.body, 0.62f);
	pal.c[CS_SKIN]   = si.body;
	pal.c[CS_HAIR]   = si.accent;
	pal.c[CS_TROUSERS] = si.body;
	pal.c[CS_SHOE]   = Shade(si.body, 0.6f);

	if (hurtFlash > 0.0f)
	{
		float k = hurtFlash / 0.22f;
		for (int i = 0; i < CS_COUNT; ++i)
			pal.c[i] = Mix(pal.c[i], Col(1.0f, 0.92f, 0.88f), k * 0.75f);
	}

	float hop = moving ? fabsf(sinf(phase)) * 7.0f : fabsf(sinf(phase * 0.4f)) * 1.8f;

	R->DrawShadow(sx + 2.0f, sy + 1.0f, 36.0f - hop * 0.5f, 16.0f, 0.30f, 0.24f);

	// 몸에만 대기 원근을 건다. 그림자와 체력 게이지는 예전에도 안 걸었다
	R->SetAnchor(sx, sy, GetDepth(), ANCHOR_HAZE);

	if (species == SP_BOAR)
	{
		float legs[7] = { 0, 0, 0, 0, 0, 0, 0 };
		float sp = phase;
		legs[3] = sinf(sp) * 0.55f;
		legs[4] = sinf(sp + 2.0f) * 0.55f;
		legs[5] = sinf(sp + 3.14f) * 0.55f;
		legs[6] = sinf(sp + 5.1f) * 0.55f;
		legs[1] = sinf(sp * 2.0f) * 0.03f;
		legs[2] = sinf(sp) * 0.05f;
		// 움츠렸다가 튀어나가는 게 보여야 돌진을 피할 수 있다
		float sq = 1.0f;
		if (state == MS_WINDUP)      sq = 1.0f - 0.18f * (stateT / BOAR_WINDUP_T);
		else if (state == MS_CHARGE) sq = 1.08f;

		Models::Draw(R, MODEL_BOAR, sx, sy - hop * 0.35f, legs, NULL, pal,
			si.scale, sq, faceX, 0xFF, 1.0f);
	}
	else
	{
		float ang[2] = { 0.0f, sinf(phase * 0.8f) * 0.06f };
		float sq = 1.0f - (hop / 7.0f) * 0.08f;
		Models::Draw(R, MODEL_TOMATOMA, sx, sy - hop, ang, NULL, pal,
			si.scale, sq, faceX, 0xFF, 1.0f);
	}

	R->ClearAnchor();

	// 체력 게이지 — 다친 개체만
	if (hp < si.maxHp)
	{
		float w = 38.0f;
		float ratio = (float)hp / (float)si.maxHp;
		R->DrawRect(sx, sy - 62.0f, w + 2.0f, 6.0f, Fade(Col(0.08f, 0.06f, 0.06f), 0.75f), 0.5f);
		R->DrawRect(sx - w * 0.5f + w * ratio * 0.5f, sy - 62.0f, w * ratio, 4.0f,
			Col(0.85f, 0.35f, 0.30f), Col(0.62f, 0.22f, 0.20f), 0.5f);
	}
}

// ---- 국자 궤적 ------------------------------------------------------------
//
// 국자 방향은 (지면 위 방위 dir, 지면에서 올라간 각 pitch) 두 개로 잡는다.
// 평소에는 손 쪽으로 비껴 들고 머리를 살짝 떨군다. 공격은 정면 쪽으로
// 방위를 모으면서 머리 위로 치켜들었다가(pitch > 90도, 머리 뒤로 넘어감)
// 가속하며 내려쳐 지면 아래쪽까지 꽂는다.

static const float LADLE_IDLE_ROT    = 0.90f;   // 평소 — 정면에서 손 쪽으로 비껴 든다
static const float LADLE_ATTACK_ROT  = 0.30f;   // 내려칠 때 — 정면에 가깝게. 0 이면 몸에 가린다
static const float LADLE_IDLE_PITCH  = -0.20f;  // 평소 — 머리가 손잡이보다 약간 아래
static const float LADLE_RAISE_PITCH = 1.95f;   // 치켜든 끝. 수직을 넘겨 머리 뒤로 젖힌다
// 내려친 끝. 손이 허리 높이라 수평 아래로 조금만 내려가도 머리가 바닥에 닿는다.
// 더 깊게 두면 국자가 땅에 박혀 보인다
static const float LADLE_STRIKE_PITCH = -0.35f;

// 공격 구간 (모션 비율). 0 ~ RAISE 들어올림, ~ STRIKE 내려침, ~ HOLD 찍은 채
// 멈춤, ~ 1 평소 자세로 복귀
static const float LADLE_T_RAISE  = 0.36f;
static const float LADLE_T_STRIKE = 0.54f;   // = ATTACK_HIT_T
static const float LADLE_T_HOLD   = 0.68f;

// 들어올릴 때 손이 도는 중심 (몸통 관절 기준, 평소 손 높이에서 위로)
static const float LADLE_SHOULDER_UP = 10.0f;
static const float LADLE_ARM         = 16.0f;

static float Smooth01(float k)
{
	if (k < 0.0f) k = 0.0f;
	if (k > 1.0f) k = 1.0f;
	return k * k * (3.0f - 2.0f * k);
}

// 지면 위 월드 방향 -> 화면 벡터. 수평(화면 좌우) 방향이 길이 1, 정면
// (카메라 쪽) 방향이 0.5 가 되도록 아이소 투영을 정규화한 것.
static void LadleProject(float dir, float* px, float* py)
{
	float c = cosf(dir), s = sinf(dir);
	*px = (c - s) * 0.70710678f;
	*py = (c + s) * 0.35355339f;
}

// 3D 방향(방위 + 올라간 각) -> 화면 벡터. 수직은 줄지 않고 그대로 위로 간다.
static void LadleVec(float dir, float pitch, float* px, float* py)
{
	float hx, hy;
	LadleProject(dir, &hx, &hy);
	float c = cosf(pitch), s = sinf(pitch);
	*px = hx * c;
	*py = hy * c - s;
}

// 자루 기준 "아래" (자루에 수직, 수직면 안). 그릇이 매달리는 쪽이다.
static void LadleDown(float dir, float pitch, float* px, float* py)
{
	float hx, hy;
	LadleProject(dir, &hx, &hy);
	float c = cosf(pitch), s = sinf(pitch);
	*px = hx * s;
	*py = hy * s + c;
}

// 모션 비율 -> (정면 기준 방위 회전량, 올라간 각). +side 가 손 쪽.
static void LadleAngles(float attackT, float* rot, float* pitch, bool* swinging)
{
	*swinging = false;
	if (attackT < 0.0f)
	{
		*rot = LADLE_IDLE_ROT;
		*pitch = LADLE_IDLE_PITCH;
		return;
	}

	if (attackT < LADLE_T_RAISE)
	{
		// 끝에서 느려지게 — 치켜든 정점에서 한 박자 머문다
		float k = Smooth01(attackT / LADLE_T_RAISE);
		*rot = LADLE_IDLE_ROT + (LADLE_ATTACK_ROT - LADLE_IDLE_ROT) * k;
		*pitch = LADLE_IDLE_PITCH + (LADLE_RAISE_PITCH - LADLE_IDLE_PITCH) * k;
		return;
	}
	if (attackT < LADLE_T_STRIKE)
	{
		// 가속 곡선. 내려칠수록 빨라져야 무게가 실린다
		*swinging = true;
		float k = (attackT - LADLE_T_RAISE) / (LADLE_T_STRIKE - LADLE_T_RAISE);
		*rot = LADLE_ATTACK_ROT;
		*pitch = LADLE_RAISE_PITCH + (LADLE_STRIKE_PITCH - LADLE_RAISE_PITCH) * k * k;
		return;
	}
	if (attackT < LADLE_T_HOLD)
	{
		// 찍은 반동으로 살짝 튀어 오른다
		float k = (attackT - LADLE_T_STRIKE) / (LADLE_T_HOLD - LADLE_T_STRIKE);
		*rot = LADLE_ATTACK_ROT;
		*pitch = LADLE_STRIKE_PITCH + sinf(k * PI_F) * 0.12f;
		return;
	}
	float k = Smooth01((attackT - LADLE_T_HOLD) / (1.0f - LADLE_T_HOLD));
	*rot = LADLE_ATTACK_ROT + (LADLE_IDLE_ROT - LADLE_ATTACK_ROT) * k;
	*pitch = LADLE_STRIKE_PITCH + (LADLE_IDLE_PITCH - LADLE_STRIKE_PITCH) * k;
}

void PlayerActor::GetPose(PlayerPose* out) const
{
	// 몸은 평소(걷기/숨쉬기) 포즈 위에 공격용 기울기만 얹는다. HumanPose 의
	// 공격 모션은 손을 앞으로 내미는 동작이라 치켜드는 손과 맞지 않는다.
	out->squash = 1.0f;
	out->flags = 0;
	out->bob = Models::HumanPose(out->ang, out->off, &out->squash, &out->flags,
		walkPhase, walking, -1.0f);

	float attackT = (attackTimer >= 0.0f) ? (attackTimer / ATTACK_MOTION) : -1.0f;

	// 바라보는 월드 방향
	float fx = faceX, fy = faceY;
	float fl = sqrtf(fx * fx + fy * fy);
	if (fl < 0.001f) { fx = 0.0f; fy = 1.0f; fl = 1.0f; }
	float faceDir = atan2f(fy / fl, fx / fl);

	// 손 쪽 = 앞손이 있는 화면 쪽(flipX)이면서 카메라 쪽. 두 후보 중 더
	// 그쪽을 향하는 회전 부호를 고른다.
	float side = 1.0f;
	{
		float ax, ay, bx, by;
		LadleProject(faceDir + 1.0f, &ax, &ay);
		LadleProject(faceDir - 1.0f, &bx, &by);
		float sa = ax * flipX + ay;
		float sb = bx * flipX + by;
		side = (sa >= sb) ? 1.0f : -1.0f;
	}

	bool swinging = false;
	float rot, pitch;
	LadleAngles(attackT, &rot, &pitch, &swinging);
	float dir = faceDir + side * rot;
	out->weaponDir = dir;
	out->weaponPitch = pitch;
	out->side = side;
	out->swinging = swinging;

	if (attackT < 0.0f) return;

	// ---- 공격 중: 앞손을 어깨 높이 원 위, 국자 방향으로 옮긴다 ------------
	// 처음과 끝은 평소 손 위치와 섞어서 튀지 않게 한다.
	float w = 1.0f;
	if (attackT < 0.12f)      w = Smooth01(attackT / 0.12f);
	else if (attackT > 0.80f) w = 1.0f - Smooth01((attackT - 0.80f) / 0.20f);

	float px, py;
	LadleVec(dir, pitch, &px, &py);

	// 치켜들 때 몸을 뒤로 젖히고, 내려치면서 앞으로 숙인다. 앞 = 바라보는
	// 방향의 화면 좌우. Draw 가 각에 flipX 를 곱하므로 미리 곱해 둔다
	float fsx, fsy;
	LadleProject(faceDir, &fsx, &fsy);
	float lean = -0.18f + 0.38f * (1.0f - (pitch - LADLE_STRIKE_PITCH)
		/ (LADLE_RAISE_PITCH - LADLE_STRIKE_PITCH));
	out->ang[HJ_BODY] += lean * fsx * flipX * w;
	if (attackT >= LADLE_T_STRIKE && attackT < LADLE_T_HOLD)
		out->squash *= 0.95f;   // 찍는 순간 몸이 눌린다

	const Model& hm = Models::Get(MODEL_HUMAN);
	const ModelJoint& hj = hm.joints[HJ_HAND_F];

	float sq = (out->squash < 0.25f) ? 0.25f : out->squash;
	float sclX = 1.0f / sq;
	float sclY = sq;

	float bx, by, ba;
	Models::JointPosition(MODEL_HUMAN, 0.0f, 0.0f, out->ang, out->off,
		1.0f, out->squash, flipX, HJ_BODY, &bx, &by, &ba);

	// 목표 = 앞손 옆 어깨 높이에서 국자 방향으로 LADLE_ARM. 어깨를 머리 중앙이
	// 아니라 앞손 쪽에 두어야 치켜든 손이 얼굴에 완전히 묻히지 않는다
	float sx0 = bx + hj.ox * flipX * sclX;
	float sy0 = by + hj.oy * sclY - LADLE_SHOULDER_UP;
	float tx = sx0 + px * LADLE_ARM;
	float ty = sy0 + py * LADLE_ARM;

	// Draw 의 관절 계산을 거꾸로 풀어 오프셋을 구한다:
	//   hand = body + R(ba) * ((ox + addX) * sclX * flip, (oy + addY) * sclY)
	float dx = tx - bx, dy = ty - by;
	float c = cosf(-ba), s = sinf(-ba);
	float lx = dx * c - dy * s;
	float ly = dx * s + dy * c;
	float addX = lx / (sclX * flipX) - hj.ox;
	float addY = ly / sclY - hj.oy;

	out->off[HJ_HAND_F * 2 + 0] += (addX - out->off[HJ_HAND_F * 2 + 0]) * w;
	out->off[HJ_HAND_F * 2 + 1] += (addY - out->off[HJ_HAND_F * 2 + 1]) * w;

	// 국자가 카메라 반대쪽으로 넘어가면 손도 몸통 뒤로 보낸다. 치켜들어
	// 수직을 넘기면 cos(pitch) 가 음수가 되어 앞을 봐도 머리 뒤로 간다
	if (w > 0.5f)
	{
		float toCam = (cosf(dir) + sinf(dir)) * cosf(pitch);
		out->flags = (toCam < -0.10f) ? PF_HANDS_SWAP : PF_HANDS_NORMAL;
	}
}

Palette PlayerActor::MakePalette() const
{
	Col cloth(0.36f, 0.46f, 0.60f);
	Palette pal = Models::MakeHumanPalette(cloth, true);

	if (hurtFlash > 0.0f)
	{
		float k = hurtFlash / 0.35f;
		for (int i = 0; i < CS_COUNT; ++i)
			pal.c[i] = Mix(pal.c[i], Col(1.0f, 0.55f, 0.50f), k * 0.7f);
	}
	return pal;
}

void PlayerActor::OnRender(const RenderContext&, float sx, float sy)
{
	Palette pal = MakePalette();

	PlayerPose pose;
	GetPose(&pose);

	float bt = walkPhase * 0.13f;
	bool blink = (bt - floorf(bt)) > 0.955f;

	int flags = PF_PLAYER | pose.flags;

	// 카메라와의 거리는 월드 y 가 아니라 (x + y) 축이다. y 만 보면 A/D 처럼
	// 깊이가 전혀 변하지 않는 옆걸음에서도 등이 보인다.
	float depth = (faceX + faceY) * 0.5f;
	if (depth > -0.35f) flags |= PF_FACE_ONLY;
	flags |= blink ? PF_EYE_SHUT : PF_EYE_OPEN;

	R->DrawShadow(sx, sy, 30.0f, 14.0f, 0.34f, 0.22f);
	Models::Draw(R, MODEL_HUMAN, sx, sy - pose.bob,
		pose.ang, pose.off, pal, 1.0f, pose.squash, flipX, flags, 1.0f);
}

// ---- 무기 ---------------------------------------------------------------

// 스테인리스. 밝은 반사와 어두운 반사의 대비가 커야 금속으로 읽힌다
static const Col STEEL_HI (0.93f, 0.95f, 0.97f);
static const Col STEEL_MID(0.72f, 0.75f, 0.79f);
static const Col STEEL_LO (0.44f, 0.47f, 0.52f);

void WeaponActor::OnUpdate(float)
{
	const PlayerActor& p = *static_cast<const PlayerActor*>(GetParent());
	PlayerPose pose;
	p.GetPose(&pose);

	// 앞손이 몸통 뒤로 간 박자에는 플레이어보다 먼저 그려야 한다.
	// 바이어스는 다른 개체와 순서가 뒤바뀌지 않을 만큼 작게 둔다.
	bool handBehind = (pose.flags & PF_HANDS_SWAP) != 0;
	SetDepthBias(handBehind ? -0.001f : 0.001f);
}

// 국자 머리. (rx, ry) 는 입구(테두리) 중심.
// 위로 오목하다 — 아래 반쪽 그릇 위에 입구를 얹고, 입구 안쪽은 먼 벽이 밝고
// 가까운 벽이 어둡게 칠한다. 볼록한 바깥면과 명암 방향이 반대여야 오목하게
// 읽힌다.
static void DrawLadleHead(float rx, float ry, float alpha)
{
	const float w = LADLE_R * 2.0f;
	const float h = LADLE_R;   // 아이소에서 원은 2:1 타원

	R->DrawBowl(rx, ry + LADLE_DEPTH * 0.5f, w, LADLE_DEPTH,
		Fade(STEEL_MID, alpha), Fade(STEEL_LO, alpha), 0.35f);
	// 바깥면 반사광
	R->DrawEllipse(rx - LADLE_R * 0.45f, ry + LADLE_DEPTH * 0.45f, 5.0f, 3.0f,
		Fade(STEEL_HI, 0.75f * alpha));

	// 입구 테두리
	R->DrawEllipse(rx, ry, w + 1.5f, h + 1.0f, Fade(STEEL_HI, alpha));
	// 안쪽 — 먼 벽(위) 밝게, 가까운 벽(아래) 그늘
	R->DrawEllipse(rx, ry + 0.4f, w - 2.5f, h - 2.0f,
		Fade(STEEL_HI, alpha), Fade(STEEL_LO, alpha));
}

// 손 위치와 국자 방향에서 자루 양 끝(x0..x1)과 그릇 입구 중심(rx, ry).
// 입구 원 위에서 자루 방향 점이 곧 방향 벡터 * R 이므로, 그릇 중심은 자루
// 끝에서 (R - TUCK) 만큼 더 나간 뒤 목 꺾임만큼 자루 아래로 내린 곳이다.
static void LadleGeometry(float hx, float hy, float dir, float pitch,
	float* x0, float* y0, float* x1, float* y1, float* rx, float* ry)
{
	float px, py, nx, ny;
	LadleVec(dir, pitch, &px, &py);
	LadleDown(dir, pitch, &nx, &ny);
	*x0 = hx - px * LADLE_GRIP;
	*y0 = hy - py * LADLE_GRIP;
	*x1 = hx + px * (LADLE_HANDLE - LADLE_GRIP);
	*y1 = hy + py * (LADLE_HANDLE - LADLE_GRIP);
	// 자루 방향 벡터로 당기므로 화면 가로·세로 양쪽으로 함께 손잡이에 가까워진다
	*rx = *x1 + px * (LADLE_R - LADLE_HEAD_TUCK) + nx * LADLE_NECK_DROP;
	*ry = *y1 + py * (LADLE_R - LADLE_HEAD_TUCK) + ny * LADLE_NECK_DROP;
}

void WeaponActor::OnRender(const RenderContext&, float sx, float sy)
{
	const PlayerActor& p = *static_cast<const PlayerActor*>(GetParent());
	PlayerPose pose;
	p.GetPose(&pose);

	const float flip = p.flipX;

	float hx, hy;
	Models::JointPosition(MODEL_HUMAN, sx, sy - pose.bob, pose.ang, pose.off,
		1.0f, pose.squash, flip, HJ_HAND_F, &hx, &hy, NULL);

	float x0, y0, x1, y1, rx, ry;
	LadleGeometry(hx, hy, pose.weaponDir, pose.weaponPitch,
		&x0, &y0, &x1, &y1, &rx, &ry);

	// 궤적 — 내려오며 지나온 위쪽 각의 머리 잔상. 세로 부채꼴이 보인다
	if (pose.swinging)
	{
		float t = p.attackTimer / ATTACK_MOTION;
		float fade = 0.35f + 0.65f * (t - LADLE_T_RAISE) / (LADLE_T_STRIKE - LADLE_T_RAISE);
		for (int i = 1; i <= 4; ++i)
		{
			float gp = pose.weaponPitch + i * 0.30f;
			if (gp > LADLE_RAISE_PITCH) break;
			float gx0, gy0, gx1, gy1, gx, gy;
			LadleGeometry(hx, hy, pose.weaponDir, gp, &gx0, &gy0, &gx1, &gy1, &gx, &gy);
			R->DrawEllipse(gx, gy, LADLE_R * 2.2f, LADLE_R * 1.1f,
				Fade(Col(1.0f, 0.97f, 0.88f), (0.34f - i * 0.07f) * fade));
		}
	}

	// 머리가 자루보다 카메라에서 멀면 머리를 먼저 그린다. 화면 위아래가
	// 아니라 (x + y) 축 성분으로 본다 — 치켜든 국자는 화면 위에 있어도
	// 앞을 보고 있으면 머리 뒤로 넘어가 있다
	float toCam = (cosf(pose.weaponDir) + sinf(pose.weaponDir)) * cosf(pose.weaponPitch);
	bool headFirst = (toCam < 0.0f);
	if (headFirst) DrawLadleHead(rx, ry, 1.0f);

	float vx = x1 - x0, vy = y1 - y0;
	float len = sqrtf(vx * vx + vy * vy);
	if (len > 0.5f)
	{
		// 세로로 선 사각형을 자루 방향으로 돌린다. (0,-1) 을 a 만큼 돌리면
		// (sin a, -cos a) 이므로 a = atan2(vx, -vy)
		R->SetRotation(atan2f(vx, -vy), 0.0f, 0.0f);
		R->DrawRect((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, 4.0f, len,
			STEEL_HI, STEEL_MID, 0.5f, 0.30f);
		R->ClearRotation();
	}

	if (!headFirst) DrawLadleHead(rx, ry, 1.0f);

	// 손이 앞에 있을 때는 자루가 손을 덮으므로 앞손만 다시 그려 쥔 모양을
	// 만든다. 플레이어와 같은 포즈·플래그로 그리므로 음영까지 똑같다.
	if ((pose.flags & PF_HANDS_SWAP) == 0)
	{
		Models::DrawJoint(R, MODEL_HUMAN, sx, sy - pose.bob, pose.ang, pose.off,
			p.MakePalette(), 1.0f, pose.squash, flip, PF_PLAYER | pose.flags, 1.0f,
			HJ_HAND_F);
	}
}

void DropActor::OnRender(const RenderContext&, float sx, float sy)
{
	float up = sinf(bob) * 3.0f;
	const Col& c = g_Items[kind].color;

	R->DrawShadow(sx, sy, 18.0f, 9.0f, 0.26f, 0.24f);
	R->DrawEllipse(sx, sy - 30.0f - up, 34.0f, 24.0f, Fade(c, 0.22f));
	R->DrawEllipse(sx, sy - 12.0f - up, 14.0f, 14.0f, Shade(c, 1.20f), Shade(c, 0.78f), 0.28f);
	R->DrawEllipse(sx - 3.0f, sy - 15.0f - up, 5.0f, 4.0f, Fade(Col(1, 1, 1), 0.55f));
}

void FloatTextActor::OnRender(const RenderContext&, float sx, float sy)
{
	// 오버레이는 줌이 꺼진 뒤에 그리므로 좌표를 직접 옮긴다
	sy -= 70.0f;
	ZoomPoint(&sx, &sy);
	float a = life;
	if (a > 1.0f) a = 1.0f;
	R->DrawLabelCentered(sx, sy, text, Fade(color, a), FONT_SMALL);
}


// =============================================================================
// UI
// =============================================================================

static const Col UI_TEXT(0.97f, 0.94f, 0.88f);
static const Col UI_DIM (0.74f, 0.69f, 0.60f);
static const Col UI_GOLD(1.00f, 0.86f, 0.55f);

static void DrawPanel(float cx, float cy, float w, float h, float alpha)
{
	R->DrawRect(cx, cy, w, h,
		Fade(Col(0.16f, 0.13f, 0.12f), alpha),
		Fade(Col(0.09f, 0.07f, 0.07f), alpha), 0.14f);
}

static void DrawBar(float x, float y, float w, float h, float ratio,
	const Col& fill, const Col& back)
{
	if (ratio < 0.0f) ratio = 0.0f;
	if (ratio > 1.0f) ratio = 1.0f;
	R->DrawRect(x + w * 0.5f, y, w, h, back, 0.45f);
	if (ratio > 0.001f)
	{
		R->DrawRect(x + w * ratio * 0.5f, y, w * ratio, h - 2.0f,
			Shade(fill, 1.15f), Shade(fill, 0.82f), 0.45f);
	}
}

static void DrawUI()
{
	PROFILE_SCOPE("level1.ui");

	wchar_t buf[128];
	const float W = (float)SCR_W;
	const float H = (float)SCR_H;

	// ---- 좌상단 상태 --------------------------------------------------------
	DrawPanel(176.0f, 62.0f, 320.0f, 104.0f, 0.66f);

	swprintf_s(buf, 128, L"Lv %d", g_Player->level);
	R->DrawLabel(36.0f, 24.0f, buf, UI_GOLD, FONT_BODY);

	swprintf_s(buf, 128, L"%d / %d", g_Player->hp, MaxHp(*g_Player));
	R->DrawLabel(150.0f, 26.0f, buf, UI_DIM, FONT_SMALL);

	DrawBar(36.0f, 58.0f, 280.0f, 14.0f,
		(float)g_Player->hp / (float)MaxHp(*g_Player),
		Col(0.80f, 0.30f, 0.28f), Fade(Col(0.10f, 0.08f, 0.08f), 0.85f));

	DrawBar(36.0f, 80.0f, 280.0f, 9.0f,
		(float)g_Player->xp / (float)XpToNext(g_Player->level),
		Col(0.52f, 0.74f, 0.42f), Fade(Col(0.10f, 0.10f, 0.08f), 0.85f));

	swprintf_s(buf, 128, L"EXP %d / %d", g_Player->xp, XpToNext(g_Player->level));
	R->DrawLabel(36.0f, 92.0f, buf, UI_DIM, FONT_SMALL);

	// ---- 우상단 소지품 ------------------------------------------------------
	swprintf_s(buf, 128, L"열매 %d   등심 %d   미식핵 %d   처치 %d",
		g_Player->bag[IK_BERRY], g_Player->bag[IK_MEAT], g_Player->bag[IK_CORE], g_Kills);
	int tw = R->LabelWidth(buf, FONT_SMALL);
	DrawPanel(W - 30.0f - tw * 0.5f, 30.0f, tw + 34.0f, 34.0f, 0.58f);
	R->DrawLabel(W - 30.0f - tw, 21.0f, buf, UI_TEXT, FONT_SMALL);

	// ---- 스탯 포인트 알림 ----------------------------------------------------
	if (g_Player->statPoints > 0 && !g_StatPanel)
	{
		swprintf_s(buf, 128, L"스탯 포인트 %d   [Tab]", g_Player->statPoints);
		int pw = R->LabelWidth(buf, FONT_BODY);
		float pulse = 0.82f + sinf(g_Time * 4.0f) * 0.16f;
		DrawPanel(W * 0.5f, H - 66.0f, pw + 46.0f, 40.0f, 0.72f);
		R->DrawLabelCentered(W * 0.5f, H - 78.0f, buf, Fade(UI_GOLD, pulse), FONT_BODY);
	}

	// ---- 조작 안내 -----------------------------------------------------------
	if (g_Time < 14.0f)
	{
		float a = (g_Time > 11.0f) ? (14.0f - g_Time) / 3.0f : 1.0f;
		DrawPanel(150.0f, H - 120.0f, 250.0f, 120.0f, 0.58f * a);
		R->DrawLabel(48.0f, H - 168.0f, L"W A S D    이동",      Fade(UI_TEXT, a), FONT_SMALL);
		R->DrawLabel(48.0f, H - 142.0f, L"Space / E  공격",      Fade(UI_TEXT, a), FONT_SMALL);
		R->DrawLabel(48.0f, H - 116.0f, L"Tab        스탯",      Fade(UI_TEXT, a), FONT_SMALL);
		R->DrawLabel(48.0f, H -  90.0f, L"R          맵 재생성", Fade(UI_TEXT, a), FONT_SMALL);
	}

	// ---- 스탯 패널 -----------------------------------------------------------
	if (g_StatPanel)
	{
		DrawPanel(W * 0.5f, H * 0.5f, 460.0f, 300.0f, 0.90f);

		R->DrawLabelCentered(W * 0.5f, H * 0.5f - 130.0f, L"능력치", UI_GOLD, FONT_TITLE);

		swprintf_s(buf, 128, L"남은 포인트   %d", g_Player->statPoints);
		R->DrawLabelCentered(W * 0.5f, H * 0.5f - 88.0f, buf, UI_TEXT, FONT_BODY);

		float rowY = H * 0.5f - 46.0f;

		swprintf_s(buf, 128, L"[1]  힘    %d        공격력 %d", g_Player->str, AtkPower(*g_Player));
		R->DrawLabel(W * 0.5f - 190.0f, rowY, buf, UI_TEXT, FONT_BODY);

		swprintf_s(buf, 128, L"[2]  체력  %d        최대 HP %d", g_Player->vit, MaxHp(*g_Player));
		R->DrawLabel(W * 0.5f - 190.0f, rowY + 38.0f, buf, UI_TEXT, FONT_BODY);

		swprintf_s(buf, 128, L"[3]  민첩  %d        공속 %.2f초  속도 %.1f",
			g_Player->agi, AtkCooldown(*g_Player), MoveSpeed(*g_Player));
		R->DrawLabel(W * 0.5f - 190.0f, rowY + 76.0f, buf, UI_TEXT, FONT_BODY);

		R->DrawLabelCentered(W * 0.5f, H * 0.5f + 100.0f,
			L"숫자 키로 투자, Tab 으로 닫기", UI_DIM, FONT_SMALL);
	}
}

// =============================================================================
// Render
// =============================================================================

void Render()
{
	if (R == 0 || g_Player == 0) return;

	PROFILE_SCOPE("level1.render");

	R->BeginFrame(C_SKY);
	R->SetFrameTime(g_Time);

	// 대기 원근과 비침은 셰이더가 도형마다 푼다. 여기서는 파라미터만 넘긴다.
	// 플레이어는 항상 화면 중앙이다.
	R->SetHaze(C_HAZE, HAZE_STRENGTH, WORLD_ZOOM);
	R->SetSeeThrough(true, g_Player->GetDepth(), SCR_W * 0.5f, SCR_H * 0.5f,
		SEETHRU_X, SEETHRU_UP, 20.0f, SEETHRU_ALPHA);

	// 카메라는 플레이어를 화면 중앙에 둔다
	float camX, camY;
	IsoToScreen(g_Player->GetWorldX(), g_Player->GetWorldY(), &camX, &camY);

	const float marginX = SCR_W * 0.5f * (1.0f / WORLD_ZOOM - 1.0f);
	const float marginY = SCR_H * 0.5f * (1.0f / WORLD_ZOOM - 1.0f);

	RenderContext rc;
	rc.renderer    = R;
	rc.offX        = SCR_W * 0.5f - camX;
	rc.offY        = SCR_H * 0.5f - camY;
	rc.cullL       = -marginX;
	rc.cullT       = -marginY;
	rc.cullR       = SCR_W + marginX;
	rc.cullB       = SCR_H + marginY;
	rc.time        = g_Time;

	R->SetWorldScale(WORLD_ZOOM);
	g_Scene.Render(rc, LAYER_GROUND, LAYER_WORLD);

	// 오버레이와 UI 는 줌·사후처리의 영향을 받으면 안 된다
	R->ClearWorldScale();
	R->EndFrame();

	g_Scene.Render(rc, LAYER_OVERLAY, LAYER_OVERLAY);

	DrawUI();
}

// -----------------------------------------------------------------------------
// 모니터 (Tools/monitor.html) — 읽기 전용. 사냥터는 Sim 지역이 아니라서
// region 은 null 이다.
// -----------------------------------------------------------------------------

static const char* const SPECIES_IDS[SP_COUNT] = { "tomatoma", "boar" };

void WriteMonitorMap(JsonWriter& w, bool active)
{
	int alive[SP_COUNT] = { 0 };
	int aggro = 0;

	if (g_MonGroup)
	{
		const std::vector<Actor*>& mons = g_MonGroup->GetChildren();
		for (size_t i = 0; i < mons.size(); ++i)
		{
			if (mons[i]->IsDestroyed()) continue;
			const MonsterActor& m = *static_cast<const MonsterActor*>(mons[i]);
			if (m.species < 0 || m.species >= SP_COUNT) continue;
			alive[m.species]++;
			if (m.state == MS_CHASE || m.state == MS_WINDUP || m.state == MS_CHARGE) ++aggro;
		}
	}

	int aliveSum = 0, totalSum = 0;
	for (int s = 0; s < SP_COUNT; ++s)
	{
		aliveSum += alive[s];
		totalSum += g_MonSpawned[s];
	}

	w.BeginObject();
	w.Str("id", "level1");
	w.Str("name", "사냥터 (레벨 1)");
	w.Null("region");
	w.Bool("active", active);
	w.Num("time", g_Time, 1);
	w.Int("seed", (int)g_Seed);

	if (g_Player)
	{
		w.BeginObject("player");
		w.Num("x", g_Player->GetX());
		w.Num("y", g_Player->GetY());
		w.Int("hp", g_Player->hp);
		w.Int("maxHp", MaxHp(*g_Player));
		w.Int("level", g_Player->level);
		w.EndObject();
	}

	w.BeginObject("mobs");
	w.Int("alive", aliveSum);
	w.Int("total", totalSum);
	w.Int("killed", g_Kills);
	w.Int("aggro", aggro);                // 쫓아오거나 돌진 중
	w.BeginArray("species");
	for (int s = 0; s < SP_COUNT; ++s)
	{
		w.BeginObject();
		w.Str("id", SPECIES_IDS[s]);
		w.StrW("name", g_Species[s].name);
		w.Int("alive", alive[s]);
		w.Int("total", g_MonSpawned[s]);
		w.Bool("hostile", g_Species[s].contactDamage > 0);
		w.EndObject();
	}
	w.EndArray();
	w.EndObject();

	w.BeginArray("bosses");
	w.EndArray();
	w.BeginArray("agents");
	w.EndArray();

	w.EndObject();
}

} // namespace Level1
