#include "stdafx.h"

#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>

#include "Game.h"
#include "Renderer.h"

// =============================================================================
// 그랑 구스토 — 튜토리얼 레벨
//
// 이 파일은 UTF-8 BOM으로 저장되어 있다. MSVC가 한글 문자열 리터럴을 올바르게
// 읽으려면 BOM이 있어야 한다. 편집기에서 인코딩을 바꾸지 말 것.
// =============================================================================

namespace Game {

// -----------------------------------------------------------------------------
// 상수
// -----------------------------------------------------------------------------

static const int   MAP_W = 120;
static const int   MAP_H = 120;

static const float TILE_W = 72.0f;
static const float TILE_H = 36.0f;

static const float PLAYER_SPEED  = 4.4f;
static const float INTERACT_DIST = 1.7f;

static const float PI = 3.14159265f;

static Renderer* R = 0;
static int SCR_W = 1280;
static int SCR_H = 720;

// -----------------------------------------------------------------------------
// 팔레트 — 프로방스·지중해 기준
// -----------------------------------------------------------------------------

static const Col C_SKY      (0.86f, 0.83f, 0.74f);
static const Col C_GRASS    (0.53f, 0.61f, 0.34f);
static const Col C_FOREST   (0.38f, 0.47f, 0.27f);
static const Col C_PATH     (0.78f, 0.67f, 0.47f);
static const Col C_SQUARE   (0.76f, 0.71f, 0.61f);
static const Col C_SAND     (0.85f, 0.78f, 0.60f);
static const Col C_WATER    (0.29f, 0.53f, 0.60f);
static const Col C_FARM     (0.66f, 0.58f, 0.38f);

static const Col C_PLASTER  (0.91f, 0.86f, 0.75f);
static const Col C_ROOF     (0.73f, 0.36f, 0.25f);
static const Col C_STONE    (0.68f, 0.65f, 0.59f);
static const Col C_WOOD     (0.45f, 0.32f, 0.21f);

// -----------------------------------------------------------------------------
// 타일
// -----------------------------------------------------------------------------

enum TileType
{
	T_GRASS = 0, T_FOREST, T_PATH, T_SQUARE, T_SAND, T_WATER, T_FARM
};

static unsigned char g_Tile[MAP_H][MAP_W];
static bool          g_Blocked[MAP_H][MAP_W];

// -----------------------------------------------------------------------------
// 엔티티
// -----------------------------------------------------------------------------

enum EntType
{
	E_TREE = 0, E_PINE, E_BUSH, E_ROCK, E_HOUSE, E_WELL, E_CRATE, E_BARREL,
	E_FENCE, E_VINE, E_REED, E_POT, E_NPC, E_HERB, E_MONSTER, E_BEAST
};

struct Entity
{
	EntType type;
	float   x, y;
	bool    active;
	int     data;        // NPC 번호 또는 미식종 종류

	// 야생 미식종 전용
	float   homeX, homeY;
	float   tgtX, tgtY;
	float   wait;
	float   phase;
	float   faceX;
	bool    moving;
};

static std::vector<Entity> g_Ents;

// 야생 미식종 종류
enum Species { S_TOMATOMA = 0, S_POTATO, S_BEAN, S_MUSH, S_COUNT };

struct SpeciesInfo
{
	const wchar_t* name;
	int            grade;
	Col            body;
	Col            accent;
};

static const SpeciesInfo g_Species[S_COUNT] =
{
	{ L"토마토마", 1, Col(0.82f, 0.28f, 0.24f), Col(0.42f, 0.60f, 0.30f) },
	{ L"알감자",   1, Col(0.80f, 0.68f, 0.45f), Col(0.55f, 0.62f, 0.34f) },
	{ L"방울콩",   2, Col(0.52f, 0.72f, 0.38f), Col(0.36f, 0.52f, 0.28f) },
	{ L"버섯당",   2, Col(0.86f, 0.80f, 0.70f), Col(0.72f, 0.42f, 0.42f) },
};

// -----------------------------------------------------------------------------
// 마을 사람 12명
// -----------------------------------------------------------------------------

struct Npc
{
	const wchar_t* name;
	Col            cloth;
	const wchar_t* l1;
	const wchar_t* l2;
};

static const Npc g_Npcs[] =
{
	{ L"미라",   Col(0.76f, 0.34f, 0.33f), 0, 0 },   // 의뢰인, 대사는 따로 처리
	{ L"브루노", Col(0.38f, 0.43f, 0.60f), L"강한 놈일수록 맛이 좋아.", L"예나 지금이나 그건 안 변하지." },
	{ L"세라",   Col(0.72f, 0.61f, 0.30f), L"협회 심사관이 이런 촌구석까지 온다니.", L"살다 보니 별일이 다 있네." },
	{ L"토마스", Col(0.34f, 0.51f, 0.43f), L"작년에 상단이 호수를 싹 훑고 갔어.", L"새끼까지 남김없이 말이야." },
	{ L"이네스", Col(0.64f, 0.42f, 0.64f), L"우리 아버지가 6등급을 잡은 적이 있어.", L"돌아가실 때까지 그 얘기만 하셨지." },
	{ L"피에트", Col(0.50f, 0.45f, 0.35f), L"물가는 조심해라.", L"요 며칠 갈대밭에서 뭔가 큰 게 움직여." },
	{ L"로렌",   Col(0.44f, 0.56f, 0.52f), L"토마토마는 순해서 먼저 덤비지 않아.", L"첫 사냥 상대로는 딱이지." },
	{ L"안나",   Col(0.78f, 0.55f, 0.42f), L"부엌은 잘 돌아가니?", L"네 어머니 국물 맛이 아직도 생각난다." },
	{ L"지오",   Col(0.42f, 0.48f, 0.36f), L"요리사 등급이 오르면 북쪽 산도 들어갈 수 있대.", L"거긴 나오는 놈들 등급이 다르다더라." },
	{ L"엘사",   Col(0.68f, 0.66f, 0.72f), L"우물물이 예전만 못해.", L"여름이라 그런 거라고들 하는데, 글쎄." },
	{ L"파올로", Col(0.56f, 0.40f, 0.30f), L"숲 안쪽까지는 들어가지 마라.", L"공터까지만. 그 너머는 네 등급이 아니야." },
	{ L"니나",   Col(0.80f, 0.70f, 0.48f), L"나중에 네 가게 열면 첫 손님은 나야.", L"약속한 거다." },
};
static const int NPC_COUNT = 12;

// -----------------------------------------------------------------------------
// 퀘스트
// -----------------------------------------------------------------------------

enum QuestState { Q_TALK = 0, Q_HUNT_SMALL, Q_BEAST, Q_COOK, Q_DONE };

static const int TOMATO_GOAL = 3;

static QuestState g_Quest;
static int        g_TomatoCount;
static int        g_HerbCount;

// 대사 상자 — 미리 줄바꿈해 둔다. 런타임 워드랩은 한글 폭 측정이 필요해서
// 프로토타입 단계에서는 비용 대비 이득이 없다.
struct Dialog
{
	const wchar_t* who;
	const wchar_t* l1;
	const wchar_t* l2;
	const wchar_t* l3;
};

static Dialog g_Dlg;
static bool   g_DlgOpen;

static void OpenDialog(const wchar_t* who, const wchar_t* a,
	const wchar_t* b = 0, const wchar_t* c = 0)
{
	g_Dlg.who = who;
	g_Dlg.l1  = a;
	g_Dlg.l2  = b;
	g_Dlg.l3  = c;
	g_DlgOpen = true;
}

// -----------------------------------------------------------------------------
// 플레이어 / 입력 / 시간
// -----------------------------------------------------------------------------

static float g_PlayerX, g_PlayerY;
static float g_WalkPhase;
static bool  g_Walking;
static float g_FaceX, g_FaceY;

static float g_Time;
static float g_ClearAt;
static bool  g_Key[256];

// -----------------------------------------------------------------------------
// 유틸
// -----------------------------------------------------------------------------

static float Hash(int x, int y)
{
	float n = sinf(x * 12.9898f + y * 78.233f) * 43758.5453f;
	return n - floorf(n);
}

static float Hash2(int x, int y)
{
	float n = sinf(x * 39.3468f + y * 11.1357f) * 24634.6345f;
	return n - floorf(n);
}

static float Clampf(float v, float lo, float hi)
{
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

static void IsoToScreen(float wx, float wy, float* sx, float* sy)
{
	*sx = (wx - wy) * (TILE_W * 0.5f);
	*sy = (wx + wy) * (TILE_H * 0.5f);
}

static bool IsBlocked(float wx, float wy)
{
	int tx = (int)floorf(wx);
	int ty = (int)floorf(wy);
	if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return true;
	return g_Blocked[ty][tx];
}

static void BlockTile(int x, int y)
{
	if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return;
	g_Blocked[y][x] = true;
}

static void SetPath(int x, int y)
{
	if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return;
	g_Tile[y][x]    = T_PATH;
	g_Blocked[y][x] = false;
}

static void AddEnt(EntType t, float x, float y, int data)
{
	Entity e;
	e.type   = t;
	e.x      = x;
	e.y      = y;
	e.active = true;
	e.data   = data;
	e.homeX  = x;
	e.homeY  = y;
	e.tgtX   = x;
	e.tgtY   = y;
	e.wait   = Hash((int)(x * 13.0f), (int)(y * 7.0f)) * 2.5f;
	e.phase  = Hash2((int)(x * 5.0f), (int)(y * 3.0f)) * 6.28f;
	e.faceX  = 1.0f;
	e.moving = false;
	g_Ents.push_back(e);
}

// -----------------------------------------------------------------------------
// 월드 생성
//
//   숲 (14..58, 12..58)          사냥 공터 (42, 44) 반지름 6.5
//        |
//        | 숲길
//   마을 (58..71, 63..76)        집 12채, 주민 12명
//        |
//        | 호숫길                  포도밭 (74..100, 56..76)
//   호숫가 ---- 호수 (96, 96) 반지름 20
// -----------------------------------------------------------------------------

static const float LAKE_X = 96.0f, LAKE_Y = 96.0f, LAKE_R = 20.0f;
static const float HUNT_X = 42.0f, HUNT_Y = 44.0f, HUNT_R = 6.5f;

static void BuildTerrain()
{
	int x, y;

	for (y = 0; y < MAP_H; ++y)
		for (x = 0; x < MAP_W; ++x)
		{
			g_Tile[y][x] = T_GRASS;
			g_Blocked[y][x] = false;
		}

	// 숲
	for (y = 12; y < 58; ++y)
		for (x = 14; x < 58; ++x)
			g_Tile[y][x] = T_FOREST;

	// 포도밭
	for (y = 56; y < 76; ++y)
		for (x = 74; x < 100; ++x)
			g_Tile[y][x] = T_FARM;

	// 호수와 모래톱
	for (y = 0; y < MAP_H; ++y)
	{
		for (x = 0; x < MAP_W; ++x)
		{
			float dx = (float)x - LAKE_X;
			float dy = (float)y - LAKE_Y;
			float d = sqrtf(dx * dx + dy * dy);
			float wob = Hash(x * 5, y * 3) * 1.3f;

			if (d < LAKE_R + wob)
			{
				g_Tile[y][x] = T_WATER;
				g_Blocked[y][x] = true;
			}
			else if (d < LAKE_R + 3.0f + wob)
			{
				g_Tile[y][x] = T_SAND;
			}
		}
	}

	// 마을 광장
	for (y = 63; y < 77; ++y)
		for (x = 58; x < 72; ++x)
			g_Tile[y][x] = T_SQUARE;

	// 길 — 마을에서 서쪽 숲으로
	for (x = 45; x < 59; ++x) { SetPath(x, 68); SetPath(x, 69); }
	for (y = 46; y < 70; ++y) { SetPath(45, y); SetPath(46, y); }

	// 길 — 마을에서 남동쪽 호숫가로
	for (y = 76; y < 83; ++y) { SetPath(69, y); SetPath(70, y); }
	for (x = 70; x < 80; ++x) { SetPath(x, 81); SetPath(x, 82); }

	// 지도 경계
	for (x = 0; x < MAP_W; ++x) { BlockTile(x, 0); BlockTile(x, MAP_H - 1); }
	for (y = 0; y < MAP_H; ++y) { BlockTile(0, y); BlockTile(MAP_W - 1, y); }
}

static void BuildVillage()
{
	// 집 12채 — 광장을 둘러싸도록
	const float hx[] = { 57.0f, 57.0f, 57.0f, 72.5f, 72.5f, 72.5f,
	                     61.0f, 66.0f, 61.5f, 66.5f, 54.0f, 75.0f };
	const float hy[] = { 65.0f, 70.0f, 75.0f, 65.5f, 70.5f, 75.5f,
	                     61.5f, 61.5f, 78.5f, 78.5f, 61.0f, 79.0f };
	for (int i = 0; i < 12; ++i)
	{
		AddEnt(E_HOUSE, hx[i], hy[i], i);
		BlockTile((int)hx[i], (int)hy[i]);
	}

	AddEnt(E_WELL, 64.5f, 69.5f, 0);
	BlockTile(64, 69);

	// 어머니의 솥 — 집 뒤편
	AddEnt(E_POT, 66.8f, 78.0f, 0);

	AddEnt(E_CRATE,  59.4f, 66.4f, 0);
	AddEnt(E_CRATE,  59.4f, 67.1f, 0);
	AddEnt(E_BARREL, 70.6f, 67.5f, 0);
	AddEnt(E_BARREL, 70.6f, 68.2f, 0);
	AddEnt(E_CRATE,  62.5f, 74.6f, 0);
	AddEnt(E_ROCK,   59.0f, 73.5f, 0);
	AddEnt(E_ROCK,   71.2f, 74.0f, 0);

	// 주민 12명
	const float nx[] = { 64.5f, 60.5f, 68.5f, 69.5f, 61.5f, 67.0f,
	                     62.5f, 66.0f, 59.8f, 70.2f, 63.0f, 68.0f };
	const float ny[] = { 66.5f, 71.5f, 66.0f, 72.5f, 68.0f, 75.5f,
	                     64.5f, 70.0f, 75.0f, 64.5f, 73.0f, 69.0f };
	for (int i = 0; i < 12; ++i)
	{
		AddEnt(E_NPC, nx[i], ny[i], i);
	}

	// 포도밭 이랑
	for (int r2 = 0; r2 < 7; ++r2)
	{
		for (int c = 0; c < 12; ++c)
		{
			float vx = 76.0f + c * 1.8f;
			float vy = 58.0f + r2 * 2.4f;
			AddEnt(E_VINE, vx, vy, 0);
		}
	}

	// 울타리
	for (int i = 0; i < 14; ++i)
	{
		AddEnt(E_FENCE, 74.5f, 57.0f + i * 1.4f, 0);
	}
}

static void BuildForest()
{
	for (int y = 12; y < 58; ++y)
	{
		for (int x = 14; x < 58; ++x)
		{
			if (g_Tile[y][x] != T_FOREST) continue;

			float dx = (float)x - HUNT_X;
			float dy = (float)y - HUNT_Y;
			float dHunt = sqrtf(dx * dx + dy * dy);
			if (dHunt < HUNT_R) continue;      // 사냥 공터는 비워 둔다

			float h = Hash(x * 3 + 1, y * 7 + 2);

			if (h > 0.66f)
			{
				AddEnt(h > 0.88f ? E_PINE : E_TREE, x + 0.5f, y + 0.5f, 0);
				BlockTile(x, y);
			}
			else if (h < 0.09f)
			{
				AddEnt(E_BUSH, x + 0.5f, y + 0.5f, 0);
			}
			else if (h > 0.635f && h < 0.645f)
			{
				AddEnt(E_ROCK, x + 0.5f, y + 0.5f, 0);
				BlockTile(x, y);
			}
		}
	}

	// 공터 가장자리 관목
	for (int i = 0; i < 18; ++i)
	{
		float a = (float)i / 18.0f * 6.28f;
		AddEnt(E_BUSH, HUNT_X + cosf(a) * (HUNT_R - 0.6f),
			HUNT_Y + sinf(a) * (HUNT_R - 0.6f), 0);
	}

	// 햇살풀
	AddEnt(E_HERB, 40.2f, 42.4f, 0);
	AddEnt(E_HERB, 44.6f, 46.2f, 0);
	AddEnt(E_HERB, 39.4f, 46.8f, 0);
	AddEnt(E_HERB, 45.0f, 41.0f, 0);
}

static void BuildMonsters()
{
	// 사냥 공터 — 토마토마 위주. 첫 사냥터이므로 목표종이 눈에 잘 띄어야 한다.
	const int clearing = 13;
	for (int i = 0; i < clearing; ++i)
	{
		float a = (float)i / (float)clearing * 6.28f + 0.4f;
		float rr = 1.6f + Hash(i * 17, 5) * (HUNT_R - 2.6f);
		int sp = (i % 3 == 2) ? (int)S_BEAN : (int)S_TOMATOMA;
		AddEnt(E_MONSTER, HUNT_X + cosf(a) * rr, HUNT_Y + sinf(a) * rr, sp);
	}

	// 숲 곳곳에 흩어진 잡몹
	const float mx[] = { 30.0f, 33.5f, 26.0f, 51.0f, 54.0f, 48.5f,
	                     22.0f, 25.5f, 36.0f, 50.0f, 20.5f, 46.0f, 31.0f };
	const float my[] = { 30.0f, 51.0f, 22.0f, 26.0f, 40.0f, 54.0f,
	                     44.0f, 36.0f, 20.0f, 48.0f, 28.0f, 22.0f, 56.0f };
	for (int i = 0; i < 13; ++i)
	{
		int tx = (int)mx[i], ty = (int)my[i];
		if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) continue;
		if (g_Blocked[ty][tx]) continue;
		AddEnt(E_MONSTER, mx[i], my[i], i % S_COUNT);
	}

	// 호수의 큰 미식종 — 잡몹을 다 잡기 전에는 나타나지 않는다
	AddEnt(E_BEAST, 78.5f, 82.5f, 0);
	g_Ents.back().active = false;

	// 갈대
	for (int i = 0; i < 26; ++i)
	{
		float a = 3.4f + (float)i / 26.0f * 1.6f;
		float rr = LAKE_R + 1.2f + Hash(i * 7, 3) * 1.4f;
		AddEnt(E_REED, LAKE_X + cosf(a) * rr, LAKE_Y + sinf(a) * rr, 0);
	}
}

void Reset()
{
	g_Ents.clear();

	BuildTerrain();
	BuildVillage();
	BuildForest();
	BuildMonsters();

	g_Quest       = Q_TALK;
	g_TomatoCount = 0;
	g_HerbCount   = 0;
	g_DlgOpen     = false;
	g_PlayerX     = 64.0f;
	g_PlayerY     = 72.0f;
	g_WalkPhase   = 0.0f;
	g_Walking     = false;
	g_FaceX       = 0.0f;
	g_FaceY       = 1.0f;
	g_Time        = 0.0f;
	g_ClearAt     = -1.0f;

	for (int i = 0; i < 256; ++i) g_Key[i] = false;
}

void Init(Renderer* renderer, int screenW, int screenH)
{
	R = renderer;
	SCR_W = screenW;
	SCR_H = screenH;
	Reset();
}

void SetKey(unsigned char key, bool down)
{
	g_Key[key] = down;
}

// -----------------------------------------------------------------------------
// 야생 미식종 AI — 비선공. 자기 영역 안을 어슬렁거리다가, 플레이어가 너무
// 가까이 오면 슬쩍 물러난다. 먼저 덤비는 일은 없다.
// -----------------------------------------------------------------------------

static void UpdateMonsters(float dt)
{
	for (size_t i = 0; i < g_Ents.size(); ++i)
	{
		Entity& e = g_Ents[i];
		if (e.type != E_MONSTER || !e.active) continue;

		e.phase += dt * 3.4f;

		float pdx = g_PlayerX - e.x;
		float pdy = g_PlayerY - e.y;
		float pd = sqrtf(pdx * pdx + pdy * pdy);

		float mvx = 0.0f, mvy = 0.0f;
		float speed = 1.1f;

		if (pd < 2.2f && pd > 0.01f)
		{
			// 겁먹고 물러남
			mvx = -pdx / pd;
			mvy = -pdy / pd;
			speed = 1.9f;
			e.wait = 0.5f;
		}
		else if (e.wait > 0.0f)
		{
			e.wait -= dt;
		}
		else
		{
			float dx = e.tgtX - e.x;
			float dy = e.tgtY - e.y;
			float d = sqrtf(dx * dx + dy * dy);

			if (d < 0.15f)
			{
				// 새 목적지를 자기 영역 안에서 고른다
				float a = Hash((int)(e.x * 31.0f + g_Time * 7.0f), (int)(e.y * 17.0f)) * 6.28f;
				float rr = 0.8f + Hash2((int)(e.x * 13.0f), (int)(g_Time * 11.0f)) * 2.6f;
				e.tgtX = e.homeX + cosf(a) * rr;
				e.tgtY = e.homeY + sinf(a) * rr;
				e.wait = 0.7f + Hash((int)(e.y * 23.0f), (int)(g_Time * 5.0f)) * 2.4f;
			}
			else
			{
				mvx = dx / d;
				mvy = dy / d;
			}
		}

		e.moving = (mvx != 0.0f || mvy != 0.0f);
		if (e.moving)
		{
			if (mvx > 0.02f) e.faceX = 1.0f;
			else if (mvx < -0.02f) e.faceX = -1.0f;

			float nx = e.x + mvx * speed * dt;
			float ny = e.y + mvy * speed * dt;
			if (!IsBlocked(nx, e.y)) e.x = nx;
			if (!IsBlocked(e.x, ny)) e.y = ny;
		}
	}
}

void Update(float dt)
{
	g_Time += dt;

	float mx = 0.0f, my = 0.0f;

	// WASD는 화면 기준. 아이소메트릭이라 각 키가 월드 대각선으로 간다.
	if (g_Key['w'] || g_Key['W']) { mx -= 1.0f; my -= 1.0f; }
	if (g_Key['s'] || g_Key['S']) { mx += 1.0f; my += 1.0f; }
	if (g_Key['a'] || g_Key['A']) { mx -= 1.0f; my += 1.0f; }
	if (g_Key['d'] || g_Key['D']) { mx += 1.0f; my -= 1.0f; }

	float len = sqrtf(mx * mx + my * my);
	g_Walking = false;

	if (len > 0.0001f && !g_DlgOpen)
	{
		mx /= len;
		my /= len;
		g_FaceX = mx;
		g_FaceY = my;

		float step = PLAYER_SPEED * dt;
		float nx = g_PlayerX + mx * step;
		float ny = g_PlayerY + my * step;

		// 축을 나눠 처리해야 벽에 비스듬히 닿아도 미끄러진다
		if (!IsBlocked(nx, g_PlayerY)) g_PlayerX = nx;
		if (!IsBlocked(g_PlayerX, ny)) g_PlayerY = ny;

		g_Walking = true;
		g_WalkPhase += dt * 9.5f;
	}
	else
	{
		g_WalkPhase += dt * 1.6f;   // 숨쉬기
	}

	UpdateMonsters(dt);
}

// -----------------------------------------------------------------------------
// 상호작용
// -----------------------------------------------------------------------------

static int FindInteractable()
{
	int best = -1;
	float bestD = INTERACT_DIST;

	for (size_t i = 0; i < g_Ents.size(); ++i)
	{
		const Entity& e = g_Ents[i];
		if (!e.active) continue;

		bool usable = false;
		if (e.type == E_NPC)     usable = true;
		if (e.type == E_HERB)    usable = true;
		if (e.type == E_MONSTER) usable = true;
		if (e.type == E_BEAST)   usable = (g_Quest == Q_BEAST);
		if (e.type == E_POT)     usable = (g_Quest == Q_COOK);
		if (!usable) continue;

		float dx = e.x - g_PlayerX;
		float dy = e.y - g_PlayerY;
		float d = sqrtf(dx * dx + dy * dy);
		if (d < bestD) { bestD = d; best = (int)i; }
	}
	return best;
}

static const wchar_t* InteractLabel(int idx)
{
	if (idx < 0) return 0;
	switch (g_Ents[idx].type)
	{
	case E_NPC:     return L"[E]  대화";
	case E_HERB:    return L"[E]  채집";
	case E_MONSTER: return L"[E]  사냥";
	case E_BEAST:   return L"[E]  사냥";
	case E_POT:     return L"[E]  요리";
	default:        return 0;
	}
}

static void WakeBeast()
{
	for (size_t i = 0; i < g_Ents.size(); ++i)
		if (g_Ents[i].type == E_BEAST) g_Ents[i].active = true;
}

static void TalkToMira()
{
	switch (g_Quest)
	{
	case Q_TALK:
		OpenDialog(L"미라",
			L"협회 심사관이 해질 무렵에 온다더라.",
			L"서쪽 숲에 토마토마가 돌아다닐 거야. 세 마리만 잡아 와.",
			L"네 어머니 부엌이 어떤 곳인지, 이번엔 네가 보여줘야지.");
		g_Quest = Q_HUNT_SMALL;
		break;

	case Q_HUNT_SMALL:
		OpenDialog(L"미라",
			L"토마토마 세 마리. 서쪽 숲 공터에 많아.",
			L"순한 놈들이니까 겁먹지 말고.", 0);
		break;

	case Q_BEAST:
		OpenDialog(L"미라",
			L"피에트 말이 맞았나 보네.",
			L"깨끗하게 잡고, 잡은 뒤에 인사는 하고 와.", 0);
		break;

	case Q_COOK:
		OpenDialog(L"미라",
			L"솥은 아직 안 식었을 거야.",
			L"심사관은 내가 붙잡아 둘 테니 얼른 가.", 0);
		break;

	default:
		OpenDialog(L"미라",
			L"네 어머니라면 간이 세다고 한소리 했겠지.",
			L"그러고는 결국 다 먹었을 거고.", 0);
		break;
	}
}

void Interact()
{
	if (g_DlgOpen) { g_DlgOpen = false; return; }

	int idx = FindInteractable();
	if (idx < 0) return;

	Entity& e = g_Ents[idx];

	if (e.type == E_NPC)
	{
		if (e.data == 0) TalkToMira();
		else OpenDialog(g_Npcs[e.data].name, g_Npcs[e.data].l1, g_Npcs[e.data].l2, 0);
		return;
	}

	if (e.type == E_HERB)
	{
		e.active = false;
		g_HerbCount++;
		OpenDialog(0, L"햇살풀을 뜯었다. 손끝에 단내가 남는다.", 0, 0);
		return;
	}

	if (e.type == E_MONSTER)
	{
		int sp = e.data;
		e.active = false;

		if (sp == S_TOMATOMA && g_Quest == Q_HUNT_SMALL)
		{
			g_TomatoCount++;
			if (g_TomatoCount >= TOMATO_GOAL)
			{
				g_Quest = Q_BEAST;
				WakeBeast();
				OpenDialog(0,
					L"세 마리째. 손에 풀 냄새가 밴다.",
					L"그때 호수 쪽에서 뭔가 크게 움직였다.",
					L"피에트 말이 맞았다.");
			}
			else if (g_TomatoCount == 1)
			{
				OpenDialog(0,
					L"토마토마를 잡았다. 포획 등급 1, 미식 등급 별 하나.",
					L"생각보다 묵직하다.", 0);
			}
			else
			{
				OpenDialog(0,
					L"두 마리째. 손놀림이 조금 나아졌다.", 0, 0);
			}
		}
		else
		{
			static wchar_t buf[128];
			swprintf_s(buf, 128, L"%s을(를) 잡았다. 포획 등급 %d.",
				g_Species[sp].name, g_Species[sp].grade);
			OpenDialog(0, buf, L"의뢰와는 상관없지만, 나쁘지 않은 재료다.", 0);
		}
		return;
	}

	if (e.type == E_BEAST && g_Quest == Q_BEAST)
	{
		e.active = false;
		g_Quest = Q_COOK;
		OpenDialog(0,
			L"호수사슴. 포획 등급 2, 미식 등급 별 둘.",
			L"숨을 고르고, 받아내고, 고맙다고 말한다.",
			L"신선할 때 가져가야 한다.");
		return;
	}

	if (e.type == E_POT && g_Quest == Q_COOK)
	{
		g_Quest = Q_DONE;
		g_ClearAt = g_Time;
		OpenDialog(0,
			L"토마토마 셋, 호수사슴 하나, 그리고 어머니의 솥.",
			L"어머니의 요리는 아니다. 시작할 때부터 알고 있었다.",
			L"이건 내 것이고, 맛있다.");
		return;
	}
}

// =============================================================================
// 그리기
// =============================================================================

static void TileColors(int x, int y, unsigned char t, Col* top, Col* bot)
{
	Col base;
	switch (t)
	{
	case T_GRASS:  base = C_GRASS;  break;
	case T_FOREST: base = C_FOREST; break;
	case T_PATH:   base = C_PATH;   break;
	case T_SQUARE: base = C_SQUARE; break;
	case T_SAND:   base = C_SAND;   break;
	case T_WATER:  base = C_WATER;  break;
	case T_FARM:   base = C_FARM;   break;
	default:       base = C_GRASS;  break;
	}

	float n = Hash(x, y) * 0.09f - 0.045f;
	*top = Shade(base, 1.05f + n);
	*bot = Shade(base, 0.90f + n * 0.6f);
}

// 땅 위 잔디·자갈·꽃. 타일 패스에서 같이 그린다. 평면 디테일이라
// 깊이 정렬에 넣을 필요가 없다.
static void GroundDetail(int x, int y, unsigned char t, float sx, float sy)
{
	float h = Hash2(x, y);
	if (h < 0.80f) return;

	if (t == T_GRASS || t == T_FOREST)
	{
		Col tuft = Shade(t == T_GRASS ? C_GRASS : C_FOREST, 1.22f);
		float ox = (Hash(x * 7, y) - 0.5f) * 22.0f;
		R->DrawTriangle(sx + ox, sy - 3.0f, 5.0f, 9.0f, tuft);
		R->DrawTriangle(sx + ox + 5.0f, sy - 2.0f, 4.0f, 7.0f, Shade(tuft, 0.9f));

		if (h > 0.965f)
		{
			// 라벤더와 들꽃 — 프로방스 톤을 땅에서부터 깔아 준다
			Col f = (Hash(x, y * 3) > 0.5f)
				? Col(0.66f, 0.55f, 0.78f)
				: Col(0.94f, 0.86f, 0.52f);
			R->DrawEllipse(sx + ox - 3.0f, sy - 8.0f, 5.0f, 5.0f, f, 0.20f);
		}
	}
	else if (t == T_PATH || t == T_SQUARE)
	{
		float ox = (Hash(x * 3, y * 5) - 0.5f) * 26.0f;
		Col p = Shade(t == T_PATH ? C_PATH : C_SQUARE, 0.90f);
		R->DrawEllipse(sx + ox, sy, 9.0f, 5.0f, p);
	}
	else if (t == T_SAND && h > 0.93f)
	{
		float ox = (Hash(x * 9, y) - 0.5f) * 24.0f;
		R->DrawEllipse(sx + ox, sy, 7.0f, 4.0f, Shade(C_SAND, 0.92f));
	}
	else if (t == T_FARM)
	{
		Col row = Shade(C_FARM, 0.86f);
		R->DrawRect(sx, sy, 46.0f, 4.0f, row, 0.5f);
	}
}

// ---- 개체 ---------------------------------------------------------------

static void DrawTree(const Entity& e, float sx, float sy)
{
	float v = Hash((int)(e.x * 7.0f), (int)(e.y * 13.0f));
	Col leaf(0.33f + v * 0.10f, 0.46f + v * 0.09f, 0.24f + v * 0.06f);
	float sway = sinf(g_Time * 0.7f + e.phase) * 2.2f;

	R->DrawShadow(sx + 4.0f, sy + 2.0f, 52.0f, 24.0f, 0.30f, 0.26f);

	R->DrawRect(sx, sy - 17.0f, 12.0f, 36.0f,
		Shade(C_WOOD, 1.25f), Shade(C_WOOD, 0.80f), 0.30f);

	// 겹친 타원 세 개. 실루엣이 둥글어지면서 블록감이 사라진다.
	R->DrawEllipse(sx - 14.0f + sway, sy - 42.0f, 44.0f, 36.0f,
		Shade(leaf, 1.02f), Shade(leaf, 0.76f));
	R->DrawEllipse(sx + 14.0f + sway, sy - 46.0f, 42.0f, 34.0f,
		Shade(leaf, 0.96f), Shade(leaf, 0.72f));
	R->DrawEllipse(sx + sway * 1.4f, sy - 62.0f, 50.0f, 44.0f,
		Shade(leaf, 1.20f), Shade(leaf, 0.88f), 0.16f);
}

static void DrawPine(const Entity& e, float sx, float sy)
{
	float v = Hash((int)(e.x * 5.0f), (int)(e.y * 9.0f));
	Col leaf(0.23f + v * 0.06f, 0.38f + v * 0.07f, 0.26f + v * 0.05f);
	float sway = sinf(g_Time * 0.6f + e.phase) * 1.6f;

	R->DrawShadow(sx + 3.0f, sy + 2.0f, 44.0f, 20.0f, 0.30f, 0.26f);
	R->DrawRect(sx, sy - 12.0f, 10.0f, 26.0f,
		Shade(C_WOOD, 1.10f), Shade(C_WOOD, 0.75f), 0.35f);

	R->DrawTriangle(sx + sway * 0.4f, sy - 32.0f, 44.0f, 34.0f,
		Shade(leaf, 1.05f), Shade(leaf, 0.72f));
	R->DrawTriangle(sx + sway * 0.7f, sy - 52.0f, 36.0f, 30.0f,
		Shade(leaf, 1.14f), Shade(leaf, 0.80f));
	R->DrawTriangle(sx + sway, sy - 70.0f, 26.0f, 26.0f,
		Shade(leaf, 1.26f), Shade(leaf, 0.92f), 0.14f);
}

static void DrawBush(const Entity& e, float sx, float sy)
{
	float v = Hash((int)(e.x * 11.0f), (int)(e.y * 5.0f));
	Col c(0.34f + v * 0.10f, 0.47f + v * 0.10f, 0.26f + v * 0.06f);

	R->DrawShadow(sx + 2.0f, sy + 1.0f, 30.0f, 14.0f, 0.26f, 0.28f);
	R->DrawEllipse(sx - 6.0f, sy - 9.0f, 22.0f, 18.0f, Shade(c, 1.02f), Shade(c, 0.78f));
	R->DrawEllipse(sx + 6.0f, sy - 11.0f, 24.0f, 20.0f,
		Shade(c, 1.16f), Shade(c, 0.86f), 0.12f);
}

static void DrawRock(float sx, float sy)
{
	R->DrawShadow(sx + 2.0f, sy + 1.0f, 30.0f, 14.0f, 0.28f, 0.26f);
	R->DrawEllipse(sx, sy - 8.0f, 30.0f, 20.0f,
		Shade(C_STONE, 1.10f), Shade(C_STONE, 0.78f), 0.10f);
	R->DrawEllipse(sx - 5.0f, sy - 14.0f, 16.0f, 11.0f,
		Shade(C_STONE, 1.22f), Shade(C_STONE, 0.98f));
}

static void DrawHouse(const Entity& e, float sx, float sy)
{
	float v = Hash((int)e.x, (int)e.y);
	Col wall = Shade(C_PLASTER, 0.95f + v * 0.10f);
	Col roof = Shade(C_ROOF, 0.92f + v * 0.16f);

	R->DrawShadow(sx + 6.0f, sy + 4.0f, 104.0f, 40.0f, 0.34f, 0.24f);

	// 벽 — 위에서 아래로 떨어지는 그라디언트가 회벽 질감을 만든다
	R->DrawRect(sx, sy - 34.0f, 74.0f, 68.0f,
		Shade(wall, 1.06f), Shade(wall, 0.80f), 0.05f, 0.10f);

	// 회벽 얼룩 — 낮은 알파의 판 두 장이면 단색 벽처럼 보이지 않는다
	R->DrawEllipse(sx - 20.0f, sy - 22.0f, 34.0f, 26.0f, Fade(Shade(wall, 0.90f), 0.45f));
	R->DrawEllipse(sx + 22.0f, sy - 46.0f, 26.0f, 22.0f, Fade(Shade(wall, 1.08f), 0.35f));

	// 기단 석재
	R->DrawRect(sx, sy - 4.0f, 78.0f, 13.0f,
		Shade(C_STONE, 1.05f), Shade(C_STONE, 0.72f), 0.08f);

	// 지붕
	R->DrawTriangle(sx, sy - 84.0f, 98.0f, 42.0f,
		Shade(roof, 1.12f), Shade(roof, 0.74f), 0.18f);

	// 기와 골 — 얇은 어두운 띠 세 줄
	for (int i = 0; i < 3; ++i)
	{
		float t = 0.30f + i * 0.20f;
		float ww = 98.0f * t;
		float yy = sy - 84.0f + 21.0f - 42.0f * (1.0f - t);
		R->DrawRect(sx, yy, ww, 2.5f, Fade(Shade(roof, 0.66f), 0.55f), 0.5f);
	}

	// 처마
	R->DrawRect(sx, sy - 64.0f, 100.0f, 7.0f,
		Shade(roof, 0.86f), Shade(roof, 0.60f), 0.35f);

	// 문
	R->DrawRect(sx, sy - 16.0f, 18.0f, 30.0f,
		Shade(C_WOOD, 1.10f), Shade(C_WOOD, 0.72f), 0.16f);
	R->DrawEllipse(sx + 5.0f, sy - 16.0f, 4.0f, 4.0f, Col(0.85f, 0.78f, 0.50f));

	// 창 — 안쪽 등불빛. 사후처리 블룸이 이 지점을 집어 준다.
	for (int s = -1; s <= 1; s += 2)
	{
		float wx = sx + 22.0f * s;
		R->DrawRect(wx, sy - 44.0f, 15.0f, 15.0f,
			Col(0.30f, 0.34f, 0.36f), Col(0.20f, 0.24f, 0.28f), 0.10f);
		R->DrawRect(wx, sy - 44.0f, 11.0f, 11.0f,
			Col(0.98f, 0.80f, 0.46f), Col(0.86f, 0.62f, 0.32f), 0.10f);
		R->DrawRect(wx, sy - 53.0f, 19.0f, 4.0f, Shade(roof, 0.90f), 0.4f);
	}
}

static void DrawWell(float sx, float sy)
{
	R->DrawShadow(sx + 3.0f, sy + 2.0f, 52.0f, 24.0f, 0.30f, 0.24f);
	R->DrawRect(sx, sy - 12.0f, 44.0f, 26.0f,
		Shade(C_STONE, 1.08f), Shade(C_STONE, 0.74f), 0.14f, 0.08f);
	R->DrawEllipse(sx, sy - 23.0f, 38.0f, 14.0f,
		Col(0.22f, 0.30f, 0.34f), Col(0.12f, 0.18f, 0.22f));
	R->DrawEllipse(sx - 6.0f, sy - 24.0f, 12.0f, 5.0f, Fade(Col(0.8f, 0.9f, 1.0f), 0.30f));

	R->DrawRect(sx - 18.0f, sy - 44.0f, 6.0f, 40.0f,
		Shade(C_WOOD, 1.1f), Shade(C_WOOD, 0.8f), 0.4f);
	R->DrawRect(sx + 18.0f, sy - 44.0f, 6.0f, 40.0f,
		Shade(C_WOOD, 1.1f), Shade(C_WOOD, 0.8f), 0.4f);
	R->DrawTriangle(sx, sy - 72.0f, 58.0f, 26.0f,
		Shade(C_ROOF, 1.08f), Shade(C_ROOF, 0.76f), 0.14f);
}

static void DrawCrate(float sx, float sy)
{
	R->DrawShadow(sx + 2.0f, sy + 1.0f, 28.0f, 13.0f, 0.26f, 0.26f);
	R->DrawRect(sx, sy - 12.0f, 26.0f, 24.0f,
		Shade(C_WOOD, 1.30f), Shade(C_WOOD, 0.92f), 0.10f, 0.08f);
	R->DrawRect(sx, sy - 12.0f, 26.0f, 3.0f, Fade(Shade(C_WOOD, 0.70f), 0.7f), 0.5f);
}

static void DrawBarrel(float sx, float sy)
{
	R->DrawShadow(sx + 2.0f, sy + 1.0f, 26.0f, 12.0f, 0.26f, 0.26f);
	R->DrawRect(sx, sy - 14.0f, 24.0f, 28.0f,
		Shade(C_WOOD, 1.22f), Shade(C_WOOD, 0.84f), 0.28f, 0.10f);
	R->DrawRect(sx, sy - 20.0f, 26.0f, 3.0f, Fade(Col(0.42f, 0.40f, 0.38f), 0.8f), 0.5f);
	R->DrawRect(sx, sy - 8.0f, 26.0f, 3.0f, Fade(Col(0.42f, 0.40f, 0.38f), 0.8f), 0.5f);
	R->DrawEllipse(sx, sy - 27.0f, 22.0f, 8.0f, Shade(C_WOOD, 1.35f));
}

static void DrawFence(float sx, float sy)
{
	R->DrawShadow(sx, sy, 20.0f, 9.0f, 0.20f, 0.28f);
	R->DrawRect(sx, sy - 14.0f, 5.0f, 28.0f,
		Shade(C_WOOD, 1.25f), Shade(C_WOOD, 0.90f), 0.4f);
	R->DrawRect(sx, sy - 20.0f, 28.0f, 4.0f, Shade(C_WOOD, 1.10f), 0.5f);
	R->DrawRect(sx, sy - 10.0f, 28.0f, 4.0f, Shade(C_WOOD, 1.00f), 0.5f);
}

static void DrawVine(const Entity& e, float sx, float sy)
{
	float v = Hash((int)(e.x * 3.0f), (int)(e.y * 7.0f));
	Col leaf(0.40f + v * 0.08f, 0.50f + v * 0.08f, 0.28f);

	R->DrawShadow(sx + 2.0f, sy + 1.0f, 26.0f, 12.0f, 0.24f, 0.26f);
	R->DrawRect(sx, sy - 9.0f, 4.0f, 20.0f, Shade(C_WOOD, 1.0f), 0.5f);
	R->DrawEllipse(sx, sy - 22.0f, 30.0f, 22.0f,
		Shade(leaf, 1.10f), Shade(leaf, 0.82f), 0.10f);
	if (v > 0.5f)
		R->DrawEllipse(sx + 6.0f, sy - 16.0f, 9.0f, 11.0f,
			Col(0.44f, 0.28f, 0.46f), Col(0.30f, 0.18f, 0.34f), 0.18f);
}

static void DrawReed(const Entity& e, float sx, float sy)
{
	float sway = sinf(g_Time * 1.4f + e.phase) * 3.0f;
	Col c(0.56f, 0.58f, 0.36f);
	for (int i = -1; i <= 1; ++i)
	{
		R->DrawRect(sx + i * 5.0f + sway * (0.5f + i * 0.3f), sy - 16.0f,
			3.0f, 32.0f, Shade(c, 1.1f), Shade(c, 0.8f), 0.5f);
	}
	R->DrawEllipse(sx + sway, sy - 34.0f, 7.0f, 13.0f,
		Col(0.52f, 0.40f, 0.28f), Col(0.40f, 0.30f, 0.20f));
}

static void DrawPot(float sx, float sy)
{
	R->DrawShadow(sx + 3.0f, sy + 2.0f, 48.0f, 22.0f, 0.30f, 0.24f);

	// 화덕
	R->DrawRect(sx, sy - 7.0f, 46.0f, 16.0f,
		Shade(C_STONE, 1.06f), Shade(C_STONE, 0.72f), 0.20f);

	// 불꽃 — 요리가 끝나면 더 세게 흔들린다
	float amp = (g_Quest == Q_DONE) ? 1.0f : 0.55f;
	float f1 = 14.0f + sinf(g_Time * 9.0f) * 4.0f * amp;
	float f2 = 11.0f + sinf(g_Time * 7.3f + 1.7f) * 4.0f * amp;
	R->DrawTriangle(sx - 8.0f, sy - 16.0f, 15.0f, f1,
		Col(0.98f, 0.72f, 0.30f), Col(0.90f, 0.38f, 0.14f));
	R->DrawTriangle(sx + 8.0f, sy - 15.0f, 13.0f, f2,
		Col(1.00f, 0.86f, 0.44f), Col(0.92f, 0.48f, 0.18f));

	// 솥
	R->DrawEllipse(sx, sy - 30.0f, 44.0f, 34.0f,
		Col(0.28f, 0.26f, 0.28f), Col(0.14f, 0.13f, 0.15f), 0.10f);
	R->DrawEllipse(sx, sy - 44.0f, 46.0f, 12.0f,
		Col(0.36f, 0.34f, 0.36f), Col(0.22f, 0.21f, 0.23f));

	if (g_Quest == Q_DONE)
	{
		// 김 — 기획서에 적은 대로, 이 게임의 하이라이트는 먹는 순간이다
		for (int i = 0; i < 5; ++i)
		{
			float t = g_Time * 0.75f + i * 0.62f;
			float up = t - floorf(t);
			float a = (1.0f - up) * 0.34f;
			float wob = sinf(t * 3.2f + i) * 9.0f;
			R->DrawEllipse(sx + wob, sy - 52.0f - up * 62.0f,
				16.0f + up * 22.0f, 14.0f + up * 20.0f,
				Fade(Col(1.0f, 0.97f, 0.90f), a));
		}
		R->DrawEllipse(sx, sy - 46.0f, 30.0f, 10.0f,
			Fade(Col(1.0f, 0.88f, 0.58f), 0.55f));
	}
}

static void DrawHerb(float sx, float sy)
{
	float p = 0.24f + sinf(g_Time * 2.6f) * 0.10f;
	R->DrawEllipse(sx, sy - 10.0f, 54.0f, 34.0f,
		Fade(Col(1.0f, 0.92f, 0.50f), p * 0.45f));

	R->DrawShadow(sx + 1.0f, sy, 20.0f, 9.0f, 0.22f, 0.28f);
	R->DrawRect(sx, sy - 9.0f, 3.0f, 18.0f, Col(0.52f, 0.58f, 0.32f), 0.5f);
	R->DrawTriangle(sx - 8.0f, sy - 15.0f, 13.0f, 10.0f, Col(0.58f, 0.66f, 0.32f));
	R->DrawTriangle(sx + 8.0f, sy - 15.0f, 13.0f, 10.0f, Col(0.58f, 0.66f, 0.32f));
	R->DrawEllipse(sx, sy - 24.0f, 17.0f, 16.0f,
		Col(0.98f, 0.86f, 0.38f), Col(0.86f, 0.66f, 0.24f), 0.24f);
}

// 야생 미식종. 둥근 실루엣 + 큰 눈 + 통통 뛰는 모션.
static void DrawMonster(const Entity& e, float sx, float sy)
{
	const SpeciesInfo& si = g_Species[e.data];

	float hop = e.moving ? fabsf(sinf(e.phase)) * 7.0f : fabsf(sinf(e.phase * 0.4f)) * 1.8f;
	float squash = 1.0f + (hop / 7.0f) * 0.10f;

	R->DrawShadow(sx + 2.0f, sy + 1.0f, 34.0f - hop * 0.6f, 15.0f - hop * 0.25f,
		0.30f - hop * 0.012f, 0.26f);

	float by = sy - 17.0f - hop;

	if (e.data == S_MUSH)
	{
		// 버섯당 — 기둥 + 갓
		R->DrawRect(sx, by + 4.0f, 18.0f, 22.0f,
			Shade(si.body, 1.05f), Shade(si.body, 0.82f), 0.30f);
		R->DrawEllipse(sx, by - 10.0f, 42.0f, 28.0f,
			Shade(si.accent, 1.12f), Shade(si.accent, 0.78f), 0.22f);
		R->DrawEllipse(sx - 9.0f, by - 14.0f, 11.0f, 8.0f, Fade(Col(1, 1, 1), 0.45f));
		R->DrawEllipse(sx + 7.0f, by - 8.0f, 8.0f, 6.0f, Fade(Col(1, 1, 1), 0.30f));
	}
	else
	{
		// 토마토는 세로보다 가로가 넓고 살짝 눌린 형태다
		bool isTom = (e.data == S_TOMATOMA);
		float bw = 34.0f * (2.0f - squash) * (isTom ? 1.16f : 1.0f);
		float bh = 32.0f * squash * (isTom ? 0.90f : 1.0f);

		R->DrawEllipse(sx, by, bw, bh,
			Shade(si.body, 1.12f), Shade(si.body, 0.76f), 0.20f);

		if (isTom)
		{
			// 옆으로 갈라진 골. 이게 있어야 그냥 빨간 공이 아니라 열매로 보인다.
			R->DrawEllipse(sx - bw * 0.29f, by + 1.0f, bw * 0.30f, bh * 0.86f,
				Fade(Shade(si.body, 0.86f), 0.50f));
			R->DrawEllipse(sx + bw * 0.29f, by + 1.0f, bw * 0.30f, bh * 0.86f,
				Fade(Shade(si.body, 0.86f), 0.50f));
		}

		// 반사광
		R->DrawEllipse(sx - 8.0f, by - 8.0f, 13.0f, 10.0f, Fade(Col(1, 1, 1), 0.32f));

		if (isTom)
		{
			// 꼭지.
			//
			// 이전에는 삼각형 세 개를 공 위에 얹어서 "빨간 공 + 작은 산"으로
			// 보였다. 실제 토마토의 꼭지는 위로 솟은 게 아니라 열매 어깨를
			// 감싸며 바깥으로 눕는 별 모양이다.
			//
			// seatY = 몸통에 꼭지가 앉는 자리, capY = 잎과 꼭다리가 그리는 높이.
			// 잎과 줄기를 seatY보다 몇 픽셀 위에 찍어야 열매에 파묻히지 않고
			// 어깨 위에 얹힌 것처럼 보인다.
			float seatY = by - bh * 0.40f;
			float capY  = seatY - 6.0f;

			Col leaf = si.accent;

			// 꼭지가 앉는 자리의 함몰 — 이것만 몸통에 남는다
			R->DrawEllipse(sx, seatY + 1.0f, bw * 0.44f, 7.0f,
				Fade(Shade(si.body, 0.70f), 0.50f));

			// 갈래 다섯. 바깥으로 갈수록 아래로 처지고 작아진다.
			const float spread[5] = { -1.0f, -0.55f, 0.0f, 0.55f, 1.0f };
			for (int k = 0; k < 5; ++k)
			{
				float f = spread[k];
				float lx = sx + f * bw * 0.36f;
				float ly = capY + 2.0f + fabsf(f) * 3.4f;
				float lw = 13.0f - fabsf(f) * 3.0f;
				float lh = 7.4f - fabsf(f) * 1.4f;
				R->DrawEllipse(lx, ly, lw, lh,
					Shade(leaf, 1.12f - fabsf(f) * 0.12f),
					Shade(leaf, 0.76f));
			}

			// 어깨를 덮는 받침
			R->DrawEllipse(sx, capY, bw * 0.40f, 7.0f,
				Shade(leaf, 1.20f), Shade(leaf, 0.88f), 0.12f);

			// 꼭다리 — 짧고 굵게, 끝에 옹이
			R->DrawRect(sx, capY - 6.0f, 4.6f, 12.0f,
				Shade(leaf, 0.94f), Shade(leaf, 0.64f), 0.45f);
			R->DrawEllipse(sx, capY - 12.0f, 6.4f, 4.4f,
				Shade(leaf, 1.06f), Shade(leaf, 0.80f));
		}
		else if (e.data == S_POTATO)
		{
			R->DrawEllipse(sx - 7.0f, by + 3.0f, 7.0f, 5.0f, Fade(Shade(si.body, 0.72f), 0.8f));
			R->DrawEllipse(sx + 8.0f, by - 3.0f, 6.0f, 4.0f, Fade(Shade(si.body, 0.72f), 0.8f));
			R->DrawTriangle(sx + 2.0f, by - 20.0f, 9.0f, 11.0f, si.accent);
		}
		else
		{
			R->DrawEllipse(sx, by - 19.0f, 9.0f, 8.0f, si.accent);
		}
	}

	// 눈
	float ex = e.faceX * 2.0f;
	R->DrawEllipse(sx - 6.0f + ex, by - 1.0f, 6.5f, 7.5f, Col(0.14f, 0.11f, 0.10f));
	R->DrawEllipse(sx + 7.0f + ex, by - 1.0f, 6.5f, 7.5f, Col(0.14f, 0.11f, 0.10f));
	R->DrawEllipse(sx - 7.0f + ex, by - 3.0f, 2.4f, 2.4f, Col(1, 1, 1, 0.9f));
	R->DrawEllipse(sx + 6.0f + ex, by - 3.0f, 2.4f, 2.4f, Col(1, 1, 1, 0.9f));

	// 발
	R->DrawEllipse(sx - 9.0f, sy - 3.0f, 11.0f, 7.0f, Shade(si.body, 0.70f));
	R->DrawEllipse(sx + 9.0f, sy - 3.0f, 11.0f, 7.0f, Shade(si.body, 0.70f));
}

static void DrawBeast(float sx, float sy)
{
	float breathe = sinf(g_Time * 1.6f) * 2.6f;
	float glow = 0.13f + sinf(g_Time * 2.2f) * 0.05f;

	Col body(0.52f, 0.42f, 0.64f);
	Col fur(0.66f, 0.56f, 0.78f);

	R->DrawEllipse(sx, sy - 30.0f, 150.0f, 110.0f, Fade(Col(0.86f, 0.76f, 1.0f), glow));
	R->DrawShadow(sx + 4.0f, sy + 2.0f, 82.0f, 32.0f, 0.34f, 0.24f);

	// 다리
	for (int i = 0; i < 4; ++i)
	{
		float lx = sx - 26.0f + i * 17.0f;
		R->DrawRect(lx, sy - 14.0f, 10.0f, 28.0f,
			Shade(body, 0.92f), Shade(body, 0.66f), 0.40f);
	}

	// 몸통
	R->DrawEllipse(sx, sy - 38.0f - breathe, 84.0f, 46.0f,
		Shade(body, 1.16f), Shade(body, 0.78f), 0.18f);
	R->DrawEllipse(sx - 6.0f, sy - 48.0f - breathe, 44.0f, 20.0f, Fade(fur, 0.35f));

	// 목과 머리
	R->DrawRect(sx + 30.0f, sy - 54.0f - breathe, 20.0f, 30.0f,
		Shade(body, 1.05f), Shade(body, 0.80f), 0.35f);
	R->DrawEllipse(sx + 36.0f, sy - 72.0f - breathe, 38.0f, 30.0f,
		Shade(body, 1.20f), Shade(body, 0.86f), 0.20f);
	R->DrawEllipse(sx + 48.0f, sy - 68.0f - breathe, 20.0f, 16.0f,
		Shade(fur, 1.05f), Shade(fur, 0.85f));

	// 뿔
	R->DrawTriangle(sx + 27.0f, sy - 96.0f - breathe, 9.0f, 22.0f, Shade(fur, 1.2f));
	R->DrawTriangle(sx + 41.0f, sy - 99.0f - breathe, 9.0f, 26.0f, Shade(fur, 1.25f));
	R->DrawTriangle(sx + 34.0f, sy - 104.0f - breathe, 7.0f, 18.0f, Shade(fur, 1.3f));

	// 눈
	R->DrawEllipse(sx + 44.0f, sy - 73.0f - breathe, 7.0f, 8.0f, Col(1.0f, 0.94f, 0.62f));
	R->DrawEllipse(sx + 44.0f, sy - 73.0f - breathe, 3.0f, 6.0f, Col(0.15f, 0.10f, 0.12f));
}

// 사람 — 머리·몸통·팔·다리를 나눠 그리고 걷기 사이클을 돌린다.
static void DrawPerson(float sx, float sy, const Col& cloth,
	float phase, bool walking, float faceX, float faceY, bool isPlayer)
{
	float legL = walking ? sinf(phase) * 6.5f : 0.0f;
	float legR = walking ? sinf(phase + PI) * 6.5f : 0.0f;
	float armL = walking ? sinf(phase + PI) * 5.5f : sinf(phase * 0.6f) * 0.8f;
	float armR = walking ? sinf(phase) * 5.5f : -sinf(phase * 0.6f) * 0.8f;
	float bob  = walking ? fabsf(sinf(phase)) * 2.6f : sinf(phase * 0.6f) * 0.7f;

	Col skin(0.91f, 0.76f, 0.62f);
	Col hair(0.26f, 0.19f, 0.15f);
	Col trous(0.31f, 0.28f, 0.30f);

	R->DrawShadow(sx + 1.0f, sy + 1.0f, 26.0f, 12.0f, 0.32f, 0.26f);

	// 뒤쪽 팔
	R->DrawRect(sx - 11.0f + armL * 0.7f, sy - 26.0f - bob, 7.0f, 20.0f,
		Shade(cloth, 0.78f), Shade(cloth, 0.62f), 0.45f);

	// 다리
	R->DrawRect(sx - 5.0f + legL, sy - 8.0f - bob * 0.3f, 8.0f, 18.0f,
		Shade(trous, 1.15f), Shade(trous, 0.85f), 0.42f);
	R->DrawRect(sx + 5.0f + legR, sy - 8.0f - bob * 0.3f, 8.0f, 18.0f,
		Shade(trous, 1.05f), Shade(trous, 0.78f), 0.42f);

	// 신발
	R->DrawEllipse(sx - 5.0f + legL, sy - 1.0f, 11.0f, 6.0f, Shade(C_WOOD, 0.85f));
	R->DrawEllipse(sx + 5.0f + legR, sy - 1.0f, 11.0f, 6.0f, Shade(C_WOOD, 0.85f));

	// 몸통
	R->DrawRect(sx, sy - 27.0f - bob, 22.0f, 28.0f,
		Shade(cloth, 1.16f), Shade(cloth, 0.80f), 0.30f, 0.10f);

	if (isPlayer)
	{
		// 앞치마 — 마커 없이도 요리사임이 읽힌다
		R->DrawRect(sx, sy - 22.0f - bob, 16.0f, 22.0f,
			Col(0.96f, 0.94f, 0.89f), Col(0.82f, 0.79f, 0.73f), 0.22f);
		R->DrawRect(sx, sy - 33.0f - bob, 18.0f, 3.0f, Col(0.90f, 0.88f, 0.83f), 0.5f);
	}

	// 앞쪽 팔
	R->DrawRect(sx + 11.0f + armR * 0.7f, sy - 26.0f - bob, 7.0f, 20.0f,
		Shade(cloth, 1.05f), Shade(cloth, 0.84f), 0.45f);
	R->DrawEllipse(sx + 11.0f + armR * 0.9f, sy - 15.0f - bob, 7.0f, 7.0f, skin);

	// 머리
	R->DrawEllipse(sx, sy - 48.0f - bob, 21.0f, 23.0f,
		Shade(skin, 1.06f), Shade(skin, 0.88f), 0.14f);

	// 머리카락 — 바라보는 방향에 따라 살짝 밀린다
	float hx = faceX * 1.6f;
	R->DrawEllipse(sx + hx, sy - 54.0f - bob, 23.0f, 15.0f,
		Shade(hair, 1.20f), Shade(hair, 0.90f), 0.10f);

	// 화면 위쪽을 보고 있으면 뒤통수라 눈을 그리지 않는다
	if (faceY > -0.35f)
	{
		float ex = faceX * 2.6f;
		R->DrawEllipse(sx - 4.5f + ex, sy - 47.0f - bob, 3.2f, 4.0f, Col(0.16f, 0.12f, 0.11f));
		R->DrawEllipse(sx + 4.5f + ex, sy - 47.0f - bob, 3.2f, 4.0f, Col(0.16f, 0.12f, 0.11f));
	}
}

static void DrawEntity(const Entity& e, float sx, float sy)
{
	switch (e.type)
	{
	case E_TREE:    DrawTree(e, sx, sy); break;
	case E_PINE:    DrawPine(e, sx, sy); break;
	case E_BUSH:    DrawBush(e, sx, sy); break;
	case E_ROCK:    DrawRock(sx, sy); break;
	case E_HOUSE:   DrawHouse(e, sx, sy); break;
	case E_WELL:    DrawWell(sx, sy); break;
	case E_CRATE:   DrawCrate(sx, sy); break;
	case E_BARREL:  DrawBarrel(sx, sy); break;
	case E_FENCE:   DrawFence(sx, sy); break;
	case E_VINE:    DrawVine(e, sx, sy); break;
	case E_REED:    DrawReed(e, sx, sy); break;
	case E_POT:     DrawPot(sx, sy); break;
	case E_HERB:    DrawHerb(sx, sy); break;
	case E_MONSTER: DrawMonster(e, sx, sy); break;
	case E_BEAST:   DrawBeast(sx, sy); break;
	case E_NPC:
		DrawPerson(sx, sy, g_Npcs[e.data].cloth,
			g_Time * 1.1f + e.phase, false, e.faceX, 1.0f, false);
		break;
	default: break;
	}
}

// ---- 깊이 정렬 ----------------------------------------------------------

struct DrawItem
{
	float depth;
	int   index;    // -1 이면 플레이어
};

static bool DepthLess(const DrawItem& a, const DrawItem& b)
{
	return a.depth < b.depth;
}

// =============================================================================
// UI
// =============================================================================

static const Col UI_PANEL(0.13f, 0.11f, 0.10f, 0.72f);
static const Col UI_TEXT (0.97f, 0.94f, 0.88f);
static const Col UI_DIM  (0.74f, 0.69f, 0.60f);
static const Col UI_GOLD (1.00f, 0.86f, 0.55f);

static void ObjectiveText(wchar_t* out, int cap)
{
	switch (g_Quest)
	{
	case Q_TALK:
		swprintf_s(out, cap, L"마을 광장에서 미라를 찾아가자");
		break;
	case Q_HUNT_SMALL:
		swprintf_s(out, cap, L"서쪽 숲 공터에서 토마토마를 잡자    %d / %d",
			g_TomatoCount, TOMATO_GOAL);
		break;
	case Q_BEAST:
		swprintf_s(out, cap, L"호숫가에 뭔가 있다. 남동쪽으로");
		break;
	case Q_COOK:
		swprintf_s(out, cap, L"부엌으로 돌아가 요리하자");
		break;
	default:
		swprintf_s(out, cap, L"첫 요리가 완성됐다");
		break;
	}
}

static void DrawPanel(float cx, float cy, float w, float h, float alpha)
{
	R->DrawRect(cx, cy, w, h,
		Fade(Col(0.16f, 0.13f, 0.12f), alpha),
		Fade(Col(0.09f, 0.07f, 0.07f), alpha), 0.14f);
}

static void DrawUI()
{
	wchar_t buf[192];
	const float W = (float)SCR_W;
	const float H = (float)SCR_H;

	// ---- 목표 표시줄 -------------------------------------------------------
	ObjectiveText(buf, 192);
	int tw = R->LabelWidth(buf, FONT_BODY);
	DrawPanel(34.0f + tw * 0.5f, 40.0f, tw + 44.0f, 42.0f, 0.66f);
	R->DrawRect(24.0f, 40.0f, 4.0f, 26.0f, UI_GOLD, 0.5f);
	R->DrawLabel(36.0f, 30.0f, buf, UI_TEXT, FONT_BODY);

	// ---- 시간 -------------------------------------------------------------
	swprintf_s(buf, 192, L"%02d:%02d", (int)(g_Time / 60.0f), (int)fmodf(g_Time, 60.0f));
	int twt = R->LabelWidth(buf, FONT_SMALL);
	DrawPanel(W - 34.0f - twt * 0.5f, 40.0f, twt + 32.0f, 34.0f, 0.55f);
	R->DrawLabel(W - 34.0f - twt, 31.0f, buf, UI_DIM, FONT_SMALL);

	// ---- 조작 안내 --------------------------------------------------------
	if (g_Time < 15.0f && g_Quest == Q_TALK)
	{
		float a = (g_Time > 12.0f) ? (15.0f - g_Time) / 3.0f : 1.0f;
		DrawPanel(140.0f, H - 118.0f, 216.0f, 96.0f, 0.60f * a);
		R->DrawLabel(52.0f, H - 156.0f, L"W A S D    이동", Fade(UI_TEXT, a), FONT_SMALL);
		R->DrawLabel(52.0f, H - 128.0f, L"E              상호작용", Fade(UI_TEXT, a), FONT_SMALL);
		R->DrawLabel(52.0f, H - 100.0f, L"R              다시 시작", Fade(UI_TEXT, a), FONT_SMALL);
	}

	// ---- 상호작용 안내 ----------------------------------------------------
	if (!g_DlgOpen)
	{
		const wchar_t* label = InteractLabel(FindInteractable());
		if (label != 0)
		{
			int lw = R->LabelWidth(label, FONT_BODY);
			float pulse = 0.86f + sinf(g_Time * 4.0f) * 0.10f;
			DrawPanel(W * 0.5f, H - 190.0f, lw + 44.0f, 40.0f, 0.72f);
			R->DrawLabelCentered(W * 0.5f, H - 202.0f, label,
				Fade(UI_GOLD, pulse), FONT_BODY);
		}
	}

	// ---- 대사 상자 --------------------------------------------------------
	if (g_DlgOpen)
	{
		const float boxH = 132.0f;
		const float boxY = H - boxH * 0.5f - 26.0f;

		DrawPanel(W * 0.5f, boxY, W - 120.0f, boxH, 0.88f);
		R->DrawRect(W * 0.5f, boxY - boxH * 0.5f + 3.0f, W - 120.0f, 5.0f,
			UI_GOLD, Shade(UI_GOLD, 0.6f), 0.5f);

		float ty = H - 132.0f;

		if (g_Dlg.who != 0)
		{
			R->DrawLabel(84.0f, ty - 32.0f, g_Dlg.who, UI_GOLD, FONT_BODY);
		}

		if (g_Dlg.l1) { R->DrawLabel(84.0f, ty, g_Dlg.l1, UI_TEXT, FONT_BODY); ty += 30.0f; }
		if (g_Dlg.l2) { R->DrawLabel(84.0f, ty, g_Dlg.l2, UI_TEXT, FONT_BODY); ty += 30.0f; }
		if (g_Dlg.l3) { R->DrawLabel(84.0f, ty, g_Dlg.l3, UI_TEXT, FONT_BODY); }

		R->DrawLabel(W - 200.0f, H - 46.0f, L"[E]  계속", UI_DIM, FONT_SMALL);
	}

	// ---- 클리어 ------------------------------------------------------------
	if (g_Quest == Q_DONE && !g_DlgOpen && g_ClearAt >= 0.0f)
	{
		DrawPanel(W * 0.5f, H * 0.5f - 50.0f, 460.0f, 168.0f, 0.86f);
		R->DrawLabelCentered(W * 0.5f, H * 0.5f - 116.0f, L"튜토리얼 완료",
			UI_GOLD, FONT_TITLE);

		swprintf_s(buf, 192, L"소요 시간   %02d:%02d",
			(int)(g_ClearAt / 60.0f), (int)fmodf(g_ClearAt, 60.0f));
		R->DrawLabelCentered(W * 0.5f, H * 0.5f - 66.0f, buf, UI_TEXT, FONT_BODY);

		swprintf_s(buf, 192, L"잡은 미식종 %d마리 · 채집 %d개",
			g_TomatoCount + 1, g_HerbCount);
		R->DrawLabelCentered(W * 0.5f, H * 0.5f - 36.0f, buf, UI_DIM, FONT_SMALL);

		R->DrawLabelCentered(W * 0.5f, H * 0.5f + 2.0f, L"[R]  다시 하기",
			UI_DIM, FONT_SMALL);
	}
}

// =============================================================================
// Render
// =============================================================================

void Render()
{
	if (R == 0) return;

	R->BeginFrame(C_SKY);

	float camX, camY;
	IsoToScreen(g_PlayerX, g_PlayerY, &camX, &camY);
	const float offX = SCR_W * 0.5f - camX;
	const float offY = SCR_H * 0.5f - camY;

	// ---- 지면 --------------------------------------------------------------
	// 타일 14,400개 중 화면에 걸리는 것만 그린다. 청크 컬링의 축소판이다.
	for (int y = 0; y < MAP_H; ++y)
	{
		for (int x = 0; x < MAP_W; ++x)
		{
			float sx, sy;
			IsoToScreen((float)x + 0.5f, (float)y + 0.5f, &sx, &sy);
			sx += offX;
			sy += offY;

			if (sx < -TILE_W || sx > SCR_W + TILE_W) continue;
			if (sy < -TILE_H || sy > SCR_H + TILE_H) continue;

			unsigned char t = g_Tile[y][x];
			Col top, bot;
			TileColors(x, y, t, &top, &bot);

			if (t == T_WATER)
			{
				R->DrawWater(sx, sy, TILE_W, TILE_H, top, bot, g_Time, 1.0f);
			}
			else
			{
				R->DrawDiamond(sx, sy, TILE_W, TILE_H, top, bot);
				GroundDetail(x, y, t, sx, sy);
			}
		}
	}

	// ---- 깊이 정렬된 개체 ---------------------------------------------------
	static std::vector<DrawItem> order;
	order.clear();

	for (size_t i = 0; i < g_Ents.size(); ++i)
	{
		const Entity& e = g_Ents[i];
		if (!e.active) continue;

		float sx, sy;
		IsoToScreen(e.x, e.y, &sx, &sy);
		sx += offX;
		sy += offY;
		if (sx < -180.0f || sx > SCR_W + 180.0f) continue;
		if (sy < -200.0f || sy > SCR_H + 200.0f) continue;

		DrawItem d;
		d.depth = e.x + e.y;
		d.index = (int)i;
		order.push_back(d);
	}

	DrawItem me;
	me.depth = g_PlayerX + g_PlayerY;
	me.index = -1;
	order.push_back(me);

	std::sort(order.begin(), order.end(), DepthLess);

	for (size_t i = 0; i < order.size(); ++i)
	{
		if (order[i].index < 0)
		{
			float sx, sy;
			IsoToScreen(g_PlayerX, g_PlayerY, &sx, &sy);
			DrawPerson(sx + offX, sy + offY, Col(0.34f, 0.41f, 0.52f),
				g_WalkPhase, g_Walking, g_FaceX, g_FaceY, true);
		}
		else
		{
			const Entity& e = g_Ents[order[i].index];
			float sx, sy;
			IsoToScreen(e.x, e.y, &sx, &sy);
			DrawEntity(e, sx + offX, sy + offY);
		}
	}

	// 월드에만 사후처리를 건다. UI는 그 뒤에 얹어야 비네팅에 먹히지 않는다.
	R->EndFrame();

	DrawUI();
}

} // namespace Game
