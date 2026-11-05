#include "stdafx.h"

#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <cstdlib>
#include <string>

#include "Game.h"
#include "Renderer.h"
#include "SpriteAtlas.h"
#include "Model.h"
#include "Platform.h"
#include "Profiler.h"
#include "Sim/SimWorld.h"

// =============================================================================
// 그랑 구스토 — 튜토리얼 레벨 (고향 마을)
//
// 마을 테스트 확장판: 맵 240x240, 주민 32명(기존 12 + 신규 20). 주민의 성향·
// 일과·인사·행동은 Data/Sim/*.ggd 에서 읽고, 매일 새벽 Sim 이 고른 행동을
// 여기서 몸짓(behavior)으로 옮긴다. STORY_SIM.md 4.2 의 "일과 층"이다.
//
// 이 파일은 UTF-8 BOM으로 저장되어 있다. MSVC가 한글 문자열 리터럴을 올바르게
// 읽으려면 BOM이 있어야 한다. 편집기에서 인코딩을 바꾸지 말 것.
// =============================================================================

namespace Game {

// -----------------------------------------------------------------------------
// 상수
// -----------------------------------------------------------------------------

static const int   MAP_W = 240;
static const int   MAP_H = 240;

// 예전 120x120 배치를 새 맵 가운데로 옮기는 오프셋. 마을·서쪽 숲·호수의
// 상대 배치는 그대로 두고, 늘어난 바깥쪽에 산적 숲·교역로·들판을 붙였다.
static const float OFS = 60.0f;

static const float TILE_W = 72.0f;
static const float TILE_H = 36.0f;

static const float PLAYER_SPEED  = 4.4f;
static const float INTERACT_DIST = 1.7f;

static const float PI = 3.14159265f;

// ---- 마을 테스트 설정 ----
// 명세의 하루는 1200초다. 일과 이동을 앉은자리에서 보려고 테스트 동안만 줄인다.
static const float TEST_DAY_SECONDS = 240.0f;
static const char* const SIM_DATA_DIR = "Data/Sim";
static const unsigned long long SIM_SEED = 20260929ull;

// NPC 대화창 셋째 줄에 오늘 행동·관계 단계·친화도를 보여 준다
static const bool SHOW_NPC_DEBUG_LINE = true;

// 블렌더에서 구운 스프라이트(Data/Sprites)로 그린다. false 면 예전 SDF 도형.
// 아틀라스를 못 읽으면 자동으로 SDF 로 돌아간다 (콘솔에 이유가 찍힌다).
static const bool USE_BLENDER_SPRITES = true;
static const char* const SPRITE_LIST = "Data/Sprites/sprites.txt";

static Renderer* R = 0;
static int SCR_W = 1280;
static int SCR_H = 720;

// -----------------------------------------------------------------------------
// 팔레트 — 프로방스·지중해 기준
// -----------------------------------------------------------------------------

// 마을 쪽은 프로방스 기준 팔레트라 원래 따뜻하지만, 풀·길·광장까지 전부
// 황토 계열이면 화면이 누렇게 뜬다. 녹색과 흙색의 색상 간격을 벌려 둔다.
static const Col C_SKY      (0.78f, 0.80f, 0.81f);
static const Col C_GRASS    (0.45f, 0.58f, 0.37f);
static const Col C_FOREST   (0.32f, 0.45f, 0.31f);
static const Col C_PATH     (0.70f, 0.63f, 0.51f);
static const Col C_SQUARE   (0.74f, 0.71f, 0.65f);
static const Col C_SAND     (0.83f, 0.77f, 0.64f);
static const Col C_WATER    (0.28f, 0.51f, 0.60f);
static const Col C_FARM     (0.63f, 0.55f, 0.40f);

static const Col C_PLASTER  (0.91f, 0.86f, 0.75f);
static const Col C_ROOF     (0.73f, 0.36f, 0.25f);
static const Col C_STONE    (0.68f, 0.65f, 0.59f);
static const Col C_WOOD     (0.45f, 0.32f, 0.21f);
static const Col C_CANVAS   (0.86f, 0.80f, 0.66f);

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
	E_FENCE, E_VINE, E_REED, E_POT, E_NPC, E_HERB, E_MONSTER, E_BEAST,
	E_TENT, E_WAGON, E_STALL, E_CAMPFIRE
};

// E_HOUSE 의 data. 0..99 는 주택 번호, 그 위는 특수 건물
enum HouseKind
{
	HK_TAVERN = 100, HK_INN, HK_GUILD, HK_MILL, HK_MANOR
};

struct Entity
{
	EntType type;
	float   x, y;
	bool    active;
	int     data;        // NPC 는 에이전트 번호, 미식종은 종류, 집은 번호/종류

	// 야생 미식종 전용
	float   homeX, homeY;
	float   tgtX, tgtY;
	float   wait;
	float   phase;
	float   faceX;
	float   dirX, dirY;  // 마지막 이동 방향 (스프라이트 8방향용)
	bool    moving;
	float   respawn;     // 잡힌 뒤 다시 나타날 때까지 (초). 0 이면 대기 없음
	int     claim;       // 이 녀석을 노리는 사냥꾼 에이전트, -1 없음
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

// 잡몹은 잡혀도 다시 나온다. 주민 사냥꾼과 플레이어가 같은 먹이를 두고
// 경쟁하므로, 재출현이 없으면 반나절 만에 들판이 빈다.
static const float MOB_RESPAWN_MIN = 55.0f;
static const float MOB_RESPAWN_VAR = 35.0f;

// -----------------------------------------------------------------------------
// 퀘스트
// -----------------------------------------------------------------------------

enum QuestState { Q_TALK = 0, Q_HUNT_SMALL, Q_BEAST, Q_COOK, Q_DONE };

static const int TOMATO_GOAL = 3;

static QuestState g_Quest;
static int        g_TomatoCount;
static int        g_HerbCount;
static int        g_CatchCount;

// 대사 상자 — 미리 줄바꿈해 둔다. 런타임 워드랩은 한글 폭 측정이 필요해서
// 프로토타입 단계에서는 비용 대비 이득이 없다.
struct Dialog
{
	std::wstring who;
	std::wstring l1;
	std::wstring l2;
	std::wstring l3;
};

static Dialog g_Dlg;
static bool   g_DlgOpen;

static void OpenDialog(const wchar_t* who, const wchar_t* a,
	const wchar_t* b = 0, const wchar_t* c = 0)
{
	g_Dlg.who = who ? who : L"";
	g_Dlg.l1  = a ? a : L"";
	g_Dlg.l2  = b ? b : L"";
	g_Dlg.l3  = c ? c : L"";
	g_DlgOpen = true;
}

// -----------------------------------------------------------------------------
// 플레이어 / 입력 / 시간
// -----------------------------------------------------------------------------

static float g_PlayerX, g_PlayerY;
static float g_WalkPhase;
static bool  g_Walking;
static float g_FaceX, g_FaceY;
static float g_FlipX = 1.0f;   // 마지막으로 향한 화면 좌우

static float g_Time;
static float g_ClearAt;
static bool  g_Key[256];

// 포획 직후 감사 의례를 받을 수 있는 남은 시간
static float g_GratitudeT;
static wchar_t g_PlayerBubble[96];
static float   g_PlayerBubbleT;

static bool  g_ShowNpcPanel;   // F5

// 렌더에서 계산한 카메라 오프셋. UI 패스가 월드 위치에 말풍선을 붙일 때 쓴다
static float g_CamOffX, g_CamOffY;

// -----------------------------------------------------------------------------
// 스프라이트 (블렌더 에셋, Art/ 원본 → Data/Sprites 아틀라스)
// -----------------------------------------------------------------------------

static SpriteAtlas g_Spr;
static bool        g_SprOk;
static int g_AnRinoIdle = -1, g_AnRinoWalk = -1, g_AnRinoGrat = -1;
static int g_AnTartuIdle = -1, g_AnTartuWalk = -1, g_AnTartuSniff = -1;
static int g_AnTomIdle = -1, g_AnTomHop = -1, g_AnTomCaught = -1;
static int g_SpHouse = -1, g_SpTree = -1, g_SpHearth = -1, g_SpCauldron = -1;
static int g_SpDishIcon = -1, g_SpDishClose = -1;

// 감사 의례 애니메이션 경과 시간. 음수면 재생하지 않는다
static float g_GratAnimT = -1.0f;

// 타르투 — 리노 뒤를 따라다니는 송로 강아지 (명세 2.5). 아직 시뮬레이션과는
// tartu_size 로만 이어진다. 가만히 있으면 땅에 코를 박고 킁킁거린다.
static float g_TartuX, g_TartuY;
static float g_TartuDirX = 1.0f, g_TartuDirY = 1.0f;
static float g_TartuT;          // 현재 동작의 경과 시간
static float g_TartuIdleT;      // 멈춰 선 지 얼마나 됐나
static bool  g_TartuMoving;

// 스프라이트 배율. 리노는 주민(SDF 사람, 약 62px)과 키를 맞추고, 토마토마는
// 예전 그림만큼 눈에 띄게 키운다 (블렌더 원본은 실제 비례로 만들었다)
static const float RINO_SPRITE_SCALE     = 0.9f;
static const float TOMATOMA_SPRITE_SCALE = 1.3f;

static const float TARTU_FOLLOW_DIST = 1.15f;
static const float TARTU_SNIFF_AFTER = 2.5f;    // 이만큼 서 있으면 킁킁
static const float TARTU_SNIFF_LEN   = 3.0f;

// 포획 연출: 토마토마가 빙글 돌며 작아지고 빛이 되어 사라진다 (유혈 없음)
struct CatchFx
{
	float x, y, t;
	float dirX, dirY;
};
static std::vector<CatchFx> g_CatchFx;

// 스프라이트 한 장. (sx, sy) = 발(피벗)의 스크린 좌표. flip 이면 피벗 기준 좌우 반전
static void DrawSpr(const SpriteFrame& f, float sx, float sy, float scale, bool flip,
	const Col& tint)
{
	float w = f.w * scale;
	float h = f.h * scale;
	float left = flip ? sx - (f.ox + f.w) * scale : sx + f.ox * scale;
	float top  = sy + f.oy * scale;
	float uv[4] = { f.u0, f.v0, f.u1, f.v1 };
	if (flip)
	{
		uv[0] = f.u1;
		uv[2] = f.u0;
	}
	R->DrawSprite(left + w * 0.5f, top + h * 0.5f, w, h, uv, tint);
}

static void LoadSprites()
{
	g_SprOk = false;
	if (!USE_BLENDER_SPRITES || R == 0) return;

	if (!g_Spr.Load(SPRITE_LIST))
	{
		printf("[Sprites] %s -> SDF fallback\n", g_Spr.Error().c_str());
		return;
	}
	int aw = g_Spr.Width(), ah = g_Spr.Height();
	if (!R->SetSpriteAtlas(g_Spr.Pixels(), aw, ah))
	{
		printf("[Sprites] atlas upload failed -> SDF fallback\n");
		return;
	}
	g_Spr.FreePixels();

	g_AnRinoIdle   = g_Spr.FindAnim("rino.idle");
	g_AnRinoWalk   = g_Spr.FindAnim("rino.walk");
	g_AnRinoGrat   = g_Spr.FindAnim("rino.gratitude");
	g_AnTartuIdle  = g_Spr.FindAnim("tartu.idle");
	g_AnTartuWalk  = g_Spr.FindAnim("tartu.walk");
	g_AnTartuSniff = g_Spr.FindAnim("tartu.sniff");
	g_AnTomIdle    = g_Spr.FindAnim("tomatoma.idle");
	g_AnTomHop     = g_Spr.FindAnim("tomatoma.hop");
	g_AnTomCaught  = g_Spr.FindAnim("tomatoma.caught");
	g_SpHouse      = g_Spr.FindSprite("house");
	g_SpTree       = g_Spr.FindSprite("tree");
	g_SpHearth     = g_Spr.FindSprite("pot_hearth");
	g_SpCauldron   = g_Spr.FindSprite("pot_cauldron");
	g_SpDishIcon   = g_Spr.FindSprite("dish_icon");
	g_SpDishClose  = g_Spr.FindSprite("dish_closeup");

	// 리노 애니메이션 셋만 있으면 켠다. 나머지는 하나씩 없어도 그 개체만 SDF 로 그린다
	g_SprOk = (g_AnRinoIdle >= 0 && g_AnRinoWalk >= 0 && g_AnRinoGrat >= 0);
	printf("[Sprites] atlas %dx%d, rino %s, tartu %s, tomatoma %s, props %d/6\n", aw, ah,
		g_SprOk ? "ok" : "MISSING",
		(g_AnTartuIdle >= 0 && g_AnTartuWalk >= 0 && g_AnTartuSniff >= 0) ? "ok" : "missing",
		(g_AnTomIdle >= 0 && g_AnTomHop >= 0 && g_AnTomCaught >= 0) ? "ok" : "missing",
		(g_SpHouse >= 0) + (g_SpTree >= 0) + (g_SpHearth >= 0) + (g_SpCauldron >= 0) +
		(g_SpDishIcon >= 0) + (g_SpDishClose >= 0));
}

static bool TartuReady()
{
	return g_SprOk && g_AnTartuIdle >= 0 && g_AnTartuWalk >= 0 && g_AnTartuSniff >= 0;
}

static bool TomatomaReady()
{
	return g_SprOk && g_AnTomIdle >= 0 && g_AnTomHop >= 0 && g_AnTomCaught >= 0;
}

// -----------------------------------------------------------------------------
// 시뮬레이션
// -----------------------------------------------------------------------------

// 명세(16.2)상 g_Sim 은 SimpleGame.cpp 가 가져야 씬 전환 후에도 남는다.
// 지금은 고향 맵만 NPC 를 쓰므로 여기 둔다. 항구 맵이 생기면 옮긴다.
static Sim::World g_Sim;
static bool       g_SimOk;
static wchar_t    g_SimError[256];   // 로드 실패 사유. 화면에 띄워 조용히 비지 않게 한다
static int        g_HomeRegion = -1;

// 알림 (우측 상단)
static int     g_NoticeSeen;
static wchar_t g_NoticeText[160];
static float   g_NoticeT;

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

// 화면 쪽 난수. Sim 의 결정론과 섞이지 않게 따로 둔다
static unsigned int g_Rng = 12345u;

static float RandF()
{
	g_Rng = g_Rng * 1664525u + 1013904223u;
	return (float)(g_Rng >> 8) / 16777216.0f;
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

static void SetTile(int x, int y, unsigned char t)
{
	if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return;
	g_Tile[y][x] = t;
}

static void SetPath(int x, int y)
{
	if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return;
	g_Tile[y][x]    = T_PATH;
	g_Blocked[y][x] = false;
}

// 폭 2 짜리 직선 길. 가로면 y, y+1 두 줄, 세로면 x, x+1 두 줄
static void PathH(int x0, int x1, int y)
{
	for (int x = x0; x <= x1; ++x) { SetPath(x, y); SetPath(x, y + 1); }
}

static void PathV(int x, int y0, int y1)
{
	for (int y = y0; y <= y1; ++y) { SetPath(x, y); SetPath(x + 1, y); }
}

static void FillRect(int x0, int y0, int x1, int y1, unsigned char t)
{
	for (int y = y0; y <= y1; ++y)
		for (int x = x0; x <= x1; ++x)
			SetTile(x, y, t);
}

static void FillCircle(float cx, float cy, float r, unsigned char t)
{
	for (int y = (int)(cy - r) - 1; y <= (int)(cy + r) + 1; ++y)
	{
		for (int x = (int)(cx - r) - 1; x <= (int)(cx + r) + 1; ++x)
		{
			float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
			if (dx * dx + dy * dy <= r * r) SetTile(x, y, t);
		}
	}
}

static int AddEnt(EntType t, float x, float y, int data)
{
	Entity e;
	e.type    = t;
	e.x       = x;
	e.y       = y;
	e.active  = true;
	e.data    = data;
	e.homeX   = x;
	e.homeY   = y;
	e.tgtX    = x;
	e.tgtY    = y;
	e.wait    = Hash((int)(x * 13.0f), (int)(y * 7.0f)) * 2.5f;
	e.phase   = Hash2((int)(x * 5.0f), (int)(y * 3.0f)) * 6.28f;
	e.faceX   = 1.0f;
	e.dirX    = 1.0f;
	e.dirY    = 1.0f;
	e.moving  = false;
	e.respawn = 0.0f;
	e.claim   = -1;
	g_Ents.push_back(e);
	return (int)g_Ents.size() - 1;
}

// UTF-8 (데이터 파일) -> 화면 문자열
static void Widen(const std::string& s, wchar_t* out, int cap)
{
	Platform::Utf8ToWide(s.c_str(), out, cap);
}

// -----------------------------------------------------------------------------
// 맵 위치 — npcs.ggd 의 schedule 이 가리키는 id
//
// 위치는 맵이 정한다. 같은 NPC 데이터를 다른 맵(항구)에 올리면 그 맵이 자기
// 위치 표를 가진다. indoor 인 위치에 밤에 도착하면 집에 들어가 보이지 않는다.
// -----------------------------------------------------------------------------

struct Loc
{
	std::string id;
	float x, y, r;
	bool  indoor;
};

static std::vector<Loc> g_Locs;

static void AddLoc(const char* id, float x, float y, float r, bool indoor)
{
	Loc l;
	l.id = id;
	l.x = x;
	l.y = y;
	l.r = r;
	l.indoor = indoor;
	g_Locs.push_back(l);
}

static int FindLoc(const std::string& id)
{
	for (size_t i = 0; i < g_Locs.size(); ++i)
		if (g_Locs[i].id == id) return (int)i;
	return -1;
}

// -----------------------------------------------------------------------------
// 월드 생성 (240 x 240, 괄호는 타일 좌표)
//
//   산적 숲       (8..76, 8..74)       야영지 (40, 40). 북쪽 길로 시장과 이어진다
//   서쪽 숲       (74..117, 72..117)   사냥 공터 (102, 104) — 기존 숲을 +60 옮긴 것
//   시장·여관     (119..137, 108..122) 선술집, 여관, 협회 출장소, 노점
//   마을 광장     (118..131, 123..136) 기존 집 12채 + 방앗간
//   교역로        y 113, 시장 -> 동쪽 끝. 상단 야영지 (205, 107), 저택 갈림길 (182)
//   악당 저택     (182, 59)
//   포도밭        (139..165, 116..134)
//   호수          (156, 156) 반지름 20, 호숫가 (140, 140)
//   남서 들판     (30..95, 165..225)   남동 들판 (182..232, 182..232) — 잡몹 밭
// -----------------------------------------------------------------------------

static const float LAKE_X = 96.0f + OFS, LAKE_Y = 96.0f + OFS, LAKE_R = 20.0f;
static const float HUNT_X = 42.0f + OFS, HUNT_Y = 44.0f + OFS, HUNT_R = 6.5f;
static const float CAMP_X = 40.0f,       CAMP_Y = 40.0f,       CAMP_R = 8.0f;

static void BuildTerrain()
{
	int x, y;

	for (y = 0; y < MAP_H; ++y)
		for (x = 0; x < MAP_W; ++x)
		{
			g_Tile[y][x] = T_GRASS;
			g_Blocked[y][x] = false;
		}

	// 숲 — 서쪽(기존)과 북서쪽 산적 숲
	FillRect(74, 72, 117, 117, T_FOREST);
	FillRect(8, 8, 76, 74, T_FOREST);
	FillCircle(CAMP_X, CAMP_Y, CAMP_R, T_GRASS);

	// 포도밭
	FillRect(139, 116, 165, 134, T_FARM);

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

	// 마을 광장, 시장 광장, 저택 앞마당, 상단 야영지
	FillRect(118, 123, 131, 136, T_SQUARE);
	FillRect(119, 108, 137, 122, T_SQUARE);
	FillRect(176, 56, 188, 66, T_SQUARE);
	FillRect(196, 100, 214, 112, T_SAND);

	// 길 — 마을에서 서쪽 숲 (기존)
	PathH(105, 118, 128);
	PathV(105, 106, 129);

	// 길 — 마을에서 호숫가 (기존)
	PathV(129, 136, 142);
	PathH(130, 139, 141);

	// 교역로 — 시장에서 동쪽 맵 끝까지
	PathH(137, MAP_W - 2, 113);

	// 북쪽 길 — 시장에서 올라가 산적 숲을 가로질러 야영지까지
	PathV(125, 28, 108);
	PathH(44, 126, 28);
	PathV(44, 28, 38);

	// 저택으로 갈라지는 길
	PathV(182, 64, 113);

	// 남서 들판으로 — 방앗간 앞을 지난다
	PathH(99, 118, 137);
	PathV(99, 137, 196);
	PathH(50, 100, 195);

	// 지도 경계
	for (x = 0; x < MAP_W; ++x) { BlockTile(x, 0); BlockTile(x, MAP_H - 1); }
	for (y = 0; y < MAP_H; ++y) { BlockTile(0, y); BlockTile(MAP_W - 1, y); }
}

static void AddHouse(float x, float y, int data, const char* locId)
{
	AddEnt(E_HOUSE, x, y, data);
	BlockTile((int)x, (int)y);
	// 문은 카메라 쪽(+x +y) 대각선 앞
	if (locId) AddLoc(locId, x + 1.2f, y + 1.2f, 0.6f, true);
}

static void BuildVillage()
{
	// 집 12채 — 광장을 둘러싸도록 (기존 배치)
	const float hx[] = { 57.0f, 57.0f, 57.0f, 72.5f, 72.5f, 72.5f,
	                     61.0f, 66.0f, 61.5f, 66.5f, 54.0f, 75.0f };
	const float hy[] = { 65.0f, 70.0f, 75.0f, 65.5f, 70.5f, 75.5f,
	                     61.5f, 61.5f, 78.5f, 78.5f, 61.0f, 79.0f };
	char id[16];
	for (int i = 0; i < 12; ++i)
	{
		snprintf(id, sizeof(id), "house_%d", i + 1);
		AddHouse(hx[i] + OFS, hy[i] + OFS, i, id);
	}
	// 확장판 주민의 집
	AddHouse(114.0f, 124.0f, 12, "house_13");
	AddHouse(114.0f, 133.0f, 13, "house_14");

	// 방앗간과 방앗간 집 (미라)
	AddHouse(109.0f, 131.0f, HK_MILL, 0);
	AddLoc("mill", 110.4f, 132.6f, 1.2f, false);
	AddHouse(105.0f, 133.0f, 14, "mill_house");

	AddEnt(E_WELL, 64.5f + OFS, 69.5f + OFS, 0);
	BlockTile(64 + (int)OFS, 69 + (int)OFS);

	// 어머니의 솥 — 집 뒤편
	AddEnt(E_POT, 66.8f + OFS, 78.0f + OFS, 0);

	AddEnt(E_CRATE,  59.4f + OFS, 66.4f + OFS, 0);
	AddEnt(E_CRATE,  59.4f + OFS, 67.1f + OFS, 0);
	AddEnt(E_BARREL, 70.6f + OFS, 67.5f + OFS, 0);
	AddEnt(E_BARREL, 70.6f + OFS, 68.2f + OFS, 0);
	AddEnt(E_CRATE,  62.5f + OFS, 74.6f + OFS, 0);
	AddEnt(E_ROCK,   59.0f + OFS, 73.5f + OFS, 0);
	AddEnt(E_ROCK,   71.2f + OFS, 74.0f + OFS, 0);

	// 시장 — 선술집, 여관, 협회 출장소, 노점 다섯
	AddHouse(122.0f, 109.0f, HK_TAVERN, "tavern");
	AddHouse(129.0f, 109.0f, HK_INN, "inn");
	AddHouse(135.0f, 109.0f, HK_GUILD, "guild_post");
	for (int i = 0; i < 5; ++i)
	{
		float sx = 121.5f + i * 3.0f;
		AddEnt(E_STALL, sx, 116.5f, i);
		BlockTile((int)sx, 116);
	}
	AddEnt(E_BARREL, 120.4f, 119.6f, 0);
	AddEnt(E_CRATE,  136.2f, 118.4f, 0);
	AddEnt(E_CRATE,  136.2f, 119.1f, 0);

	// 동쪽 문 — 교역로 입구
	AddEnt(E_FENCE, 138.6f, 111.4f, 0);
	AddEnt(E_FENCE, 138.6f, 116.2f, 0);

	// 포도밭 이랑 (기존 배치를 넓힌 밭에 맞춰 반복)
	for (int r2 = 0; r2 < 7; ++r2)
	{
		for (int c = 0; c < 13; ++c)
		{
			float vx = 141.0f + c * 1.8f;
			float vy = 117.5f + r2 * 2.4f;
			AddEnt(E_VINE, vx, vy, 0);
		}
	}
	for (int i = 0; i < 12; ++i) AddEnt(E_FENCE, 138.5f, 117.0f + i * 1.4f, 0);

	// 악당 저택
	AddHouse(182.0f, 59.0f, HK_MANOR, "manor");
	AddEnt(E_BARREL, 178.6f, 62.2f, 0);
	AddEnt(E_CRATE,  186.0f, 63.0f, 0);

	// 산적 야영지 — 천막 넷과 모닥불
	const float tx[] = { 36.0f, 44.5f, 36.5f, 45.0f };
	const float ty[] = { 36.5f, 36.0f, 44.5f, 44.0f };
	for (int i = 0; i < 4; ++i)
	{
		AddEnt(E_TENT, tx[i], ty[i], 0);
		BlockTile((int)tx[i], (int)ty[i]);
	}
	AddEnt(E_CAMPFIRE, CAMP_X, CAMP_Y, 0);
	BlockTile((int)CAMP_X, (int)CAMP_Y);
	AddEnt(E_BARREL, 42.6f, 38.2f, 0);

	// 상단 야영지 — 짐마차 셋, 천막 둘
	const float wx[] = { 200.0f, 206.0f, 211.0f };
	const float wy[] = { 103.5f, 102.5f, 105.5f };
	for (int i = 0; i < 3; ++i)
	{
		AddEnt(E_WAGON, wx[i], wy[i], i);
		BlockTile((int)wx[i], (int)wy[i]);
	}
	AddEnt(E_TENT, 202.0f, 109.5f, 1);
	AddEnt(E_TENT, 209.5f, 110.0f, 1);
	BlockTile(202, 109);
	BlockTile(209, 110);
	AddEnt(E_CRATE, 204.0f, 104.6f, 0);
	AddEnt(E_BARREL, 207.2f, 105.0f, 0);

	// ---- 위치 표 (집·가게 문은 AddHouse 가 넣었다) ----
	AddLoc("square",        125.0f, 130.0f, 3.0f, false);
	AddLoc("well",          125.9f, 131.3f, 0.8f, false);
	AddLoc("market",        127.5f, 119.6f, 3.0f, false);
	AddLoc("gate",          139.6f, 113.8f, 1.4f, false);
	AddLoc("road_east",     168.0f, 113.9f, 5.0f, false);
	AddLoc("road_north",    125.9f,  68.0f, 6.0f, false);
	AddLoc("caravan",       205.0f, 107.0f, 3.0f, false);
	AddLoc("camp",          CAMP_X, CAMP_Y, 3.5f, false);
	AddLoc("bandit_woods",   56.0f,  50.0f, 7.0f, false);
	AddLoc("hunt_clearing", HUNT_X, HUNT_Y, 4.0f, false);
	AddLoc("forest_edge",   110.0f, 121.0f, 2.5f, false);
	AddLoc("lakeside",      140.5f, 140.5f, 2.0f, false);
	AddLoc("vineyard",      152.0f, 125.0f, 5.0f, false);
	AddLoc("meadow_sw",      62.0f, 196.0f, 10.0f, false);
	AddLoc("field_se",      205.0f, 205.0f, 10.0f, false);
}

static void BuildForest()
{
	for (int y = 1; y < MAP_H - 1; ++y)
	{
		for (int x = 1; x < MAP_W - 1; ++x)
		{
			if (g_Tile[y][x] != T_FOREST) continue;

			float dx = (float)x - HUNT_X;
			float dy = (float)y - HUNT_Y;
			if (dx * dx + dy * dy < HUNT_R * HUNT_R) continue;   // 사냥 공터는 비워 둔다

			// 산적 숲은 침엽수가 많고 어둡다
			bool dark = (x < 80 && y < 80);
			float h = Hash(x * 3 + 1, y * 7 + 2);

			if (h > 0.66f)
			{
				AddEnt(h > (dark ? 0.76f : 0.88f) ? E_PINE : E_TREE, x + 0.5f, y + 0.5f, 0);
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

	// 들판 — 드문드문 나무와 덤불. 빈 풀밭만 있으면 세트장처럼 보인다
	for (int y = 2; y < MAP_H - 2; ++y)
	{
		for (int x = 2; x < MAP_W - 2; ++x)
		{
			if (g_Tile[y][x] != T_GRASS) continue;
			bool village = (x > 100 && x < 145 && y > 100 && y < 145);
			if (village) continue;

			float h = Hash2(x * 7 + 3, y * 5 + 1);
			if (h > 0.986f)
			{
				AddEnt(E_TREE, x + 0.5f, y + 0.5f, 0);
				BlockTile(x, y);
			}
			else if (h < 0.018f)
			{
				AddEnt(E_BUSH, x + 0.5f, y + 0.5f, 0);
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
	AddEnt(E_HERB, 40.2f + OFS, 42.4f + OFS, 0);
	AddEnt(E_HERB, 44.6f + OFS, 46.2f + OFS, 0);
	AddEnt(E_HERB, 39.4f + OFS, 46.8f + OFS, 0);
	AddEnt(E_HERB, 45.0f + OFS, 41.0f + OFS, 0);
	const float hx[] = { 52.0f, 70.0f, 88.0f, 40.0f, 60.0f, 200.0f, 215.0f, 190.0f };
	const float hy[] = { 50.0f, 60.0f, 205.0f, 180.0f, 212.0f, 195.0f, 220.0f, 214.0f };
	for (int i = 0; i < 8; ++i)
	{
		if (!IsBlocked(hx[i], hy[i])) AddEnt(E_HERB, hx[i], hy[i], 0);
	}
}

// 영역 안 빈 타일에 잡몹을 흩어 놓는다. 해시로 골라 실행마다 같은 배치다
static void ScatterMobs(int x0, int y0, int x1, int y1, int count, int seed,
	const int* speciesMix, int mixCount)
{
	int placed = 0;
	for (int i = 0; i < count * 6 && placed < count; ++i)
	{
		float fx = x0 + Hash(i * 13 + seed, seed * 7 + 1) * (x1 - x0);
		float fy = y0 + Hash2(i * 7 + seed * 3, seed + 5) * (y1 - y0);
		int tx = (int)fx, ty = (int)fy;
		if (tx < 1 || ty < 1 || tx >= MAP_W - 1 || ty >= MAP_H - 1) continue;
		if (g_Blocked[ty][tx] || g_Tile[ty][tx] == T_PATH) continue;
		AddEnt(E_MONSTER, fx, fy, speciesMix[placed % mixCount]);
		++placed;
	}
}

static void BuildMonsters()
{
	// 사냥 공터 — 토마토마 위주. 첫 사냥터이므로 목표종이 눈에 잘 띄어야 한다.
	const int clearing = 18;
	for (int i = 0; i < clearing; ++i)
	{
		float a = (float)i / (float)clearing * 6.28f + 0.4f;
		float rr = 1.6f + Hash(i * 17, 5) * (HUNT_R - 2.6f);
		int sp = (i % 3 == 2) ? (int)S_BEAN : (int)S_TOMATOMA;
		AddEnt(E_MONSTER, HUNT_X + cosf(a) * rr, HUNT_Y + sinf(a) * rr, sp);
	}

	const int all[]    = { S_TOMATOMA, S_POTATO, S_BEAN, S_MUSH };
	const int field[]  = { S_TOMATOMA, S_POTATO, S_TOMATOMA, S_BEAN, S_POTATO };
	const int woods[]  = { S_MUSH, S_BEAN, S_MUSH, S_POTATO };

	ScatterMobs(76, 74, 116, 116, 26, 11, all, 4);      // 서쪽 숲
	ScatterMobs(10, 10, 74, 72, 20, 23, woods, 4);      // 산적 숲
	ScatterMobs(30, 165, 95, 225, 44, 37, field, 5);    // 남서 들판
	ScatterMobs(182, 182, 232, 232, 34, 41, field, 5);  // 남동 들판
	ScatterMobs(167, 118, 176, 136, 6, 53, all, 4);     // 포도밭 가장자리

	// 호수의 큰 미식종 — 잡몹을 다 잡기 전에는 나타나지 않는다
	AddEnt(E_BEAST, 78.5f + OFS, 82.5f + OFS, 0);
	g_Ents.back().active = false;

	// 갈대
	for (int i = 0; i < 26; ++i)
	{
		float a = 3.4f + (float)i / 26.0f * 1.6f;
		float rr = LAKE_R + 1.2f + Hash(i * 7, 3) * 1.4f;
		AddEnt(E_REED, LAKE_X + cosf(a) * rr, LAKE_Y + sinf(a) * rr, 0);
	}
}

// =============================================================================
// 길찾기 — 타일 A* (8방향, 모서리 끼기 금지, 옥타일 휴리스틱)
//
// 주민이 구간마다 한 번씩, 사냥·약탈 대상이 움직이면 몇 초에 한 번 부른다.
// 한 프레임에 여러 명이 동시에 요청해도 PATH_BUDGET 만큼만 풀고 나머지는
// 다음 프레임으로 미룬다 (새벽에 32명이 한꺼번에 출근하는 순간의 히치 방지).
// BFS 는 야영지 -> 교역로(150 타일) 같은 먼 길에서 맵 절반을 뒤져서 A* 로 바꿨다.
// =============================================================================

static const int PATH_BUDGET    = 3;
static const int PATH_MAX_NODES = 12000;

struct PathNode
{
	int f;
	int cell;
};

static bool PathNodeLess(const PathNode& a, const PathNode& b)
{
	return a.f > b.f;   // std::push_heap 은 최대 힙이라 뒤집는다
}

static int                   g_PathStamp;
static int                   g_PathSeen[MAP_H * MAP_W];   // 이번 탐색에서 열었는가
static int                   g_PathG[MAP_H * MAP_W];
static int                   g_PathPrev[MAP_H * MAP_W];
static std::vector<PathNode> g_PathHeap;
static int                   g_PathBudgetLeft;

static bool FreeCell(int x, int y)
{
	return x >= 0 && y >= 0 && x < MAP_W && y < MAP_H && !g_Blocked[y][x];
}

// 막힌 칸을 목적지로 받으면 가장 가까운 빈 칸으로 바꾼다
static bool NearestFree(float x, float y, int* ox, int* oy)
{
	int cx = (int)floorf(x), cy = (int)floorf(y);
	for (int r = 0; r <= 6; ++r)
	{
		for (int dy = -r; dy <= r; ++dy)
		{
			for (int dx = -r; dx <= r; ++dx)
			{
				if (abs(dx) != r && abs(dy) != r) continue;
				if (FreeCell(cx + dx, cy + dy))
				{
					*ox = cx + dx;
					*oy = cy + dy;
					return true;
				}
			}
		}
	}
	return false;
}

static int Octile(int ax, int ay, int bx, int by)
{
	int dx = abs(ax - bx), dy = abs(ay - by);
	return (dx < dy) ? 14 * dx + 10 * (dy - dx) : 14 * dy + 10 * (dx - dy);
}

static bool FindPath(float sx, float sy, float tx, float ty, std::vector<int>& out)
{
	PROFILE_SCOPE("tutorial.path");
	out.clear();

	int ax, ay, bx, by;
	if (!NearestFree(sx, sy, &ax, &ay)) return false;
	if (!NearestFree(tx, ty, &bx, &by)) return false;

	int start = ay * MAP_W + ax;
	int goal = by * MAP_W + bx;
	if (start == goal) return true;

	++g_PathStamp;
	g_PathHeap.clear();
	g_PathSeen[start] = g_PathStamp;
	g_PathG[start] = 0;
	g_PathPrev[start] = -1;
	PathNode n0;
	n0.f = Octile(ax, ay, bx, by);
	n0.cell = start;
	g_PathHeap.push_back(n0);

	static const int DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
	static const int DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
	static const int COST[8] = { 10, 10, 10, 10, 14, 14, 14, 14 };

	int expanded = 0;
	bool found = false;
	while (!g_PathHeap.empty() && expanded < PATH_MAX_NODES)
	{
		std::pop_heap(g_PathHeap.begin(), g_PathHeap.end(), PathNodeLess);
		PathNode cur = g_PathHeap.back();
		g_PathHeap.pop_back();

		int cx = cur.cell % MAP_W, cy = cur.cell / MAP_W;
		int g = g_PathG[cur.cell];
		// 더 싼 길로 이미 다시 넣은 칸의 낡은 항목
		if (cur.f > g + Octile(cx, cy, bx, by)) continue;
		if (cur.cell == goal) { found = true; break; }
		++expanded;

		for (int d = 0; d < 8; ++d)
		{
			int nx = cx + DX[d], ny = cy + DY[d];
			if (!FreeCell(nx, ny)) continue;
			// 대각선은 양옆 두 칸이 모두 비어야 한다 (나무 모서리 끼기 방지)
			if (d >= 4 && (!FreeCell(cx + DX[d], cy) || !FreeCell(cx, cy + DY[d]))) continue;
			int ni = ny * MAP_W + nx;
			int ng = g + COST[d];
			if (g_PathSeen[ni] == g_PathStamp && g_PathG[ni] <= ng) continue;
			g_PathSeen[ni] = g_PathStamp;
			g_PathG[ni] = ng;
			g_PathPrev[ni] = cur.cell;
			PathNode pn;
			pn.f = ng + Octile(nx, ny, bx, by);
			pn.cell = ni;
			g_PathHeap.push_back(pn);
			std::push_heap(g_PathHeap.begin(), g_PathHeap.end(), PathNodeLess);
		}
	}
	PROFILE_COUNT("tutorial.path_nodes", expanded);
	if (!found) return false;

	for (int c = goal; c != start && c >= 0; c = g_PathPrev[c]) out.push_back(c);
	std::reverse(out.begin(), out.end());
	return true;
}

// 두 점 사이가 뚫려 있는가 (경로 당기기용)
static bool LineClear(float x0, float y0, float x1, float y1)
{
	float dx = x1 - x0, dy = y1 - y0;
	float len = sqrtf(dx * dx + dy * dy);
	int steps = (int)(len / 0.25f) + 1;
	for (int i = 1; i <= steps; ++i)
	{
		float t = (float)i / (float)steps;
		if (IsBlocked(x0 + dx * t, y0 + dy * t)) return false;
	}
	return true;
}

// =============================================================================
// 주민 에이전트 — Sim NPC 의 화면 쪽 몸
//
// Sim 이 하루 한 번 고른 행동(actions.ggd 의 behavior)과 일과표(schedule)를
// 매 프레임 움직임으로 바꾼다. 상태는 읽기만 하고, 플레이어 행동은
// OnPlayerEvent 로 보고만 한다 (STORY_SIM.md 0.1).
// =============================================================================

enum Behavior
{
	BH_STAY = 0,   // 자리 지키기
	BH_WANDER,     // 위치 근처 어슬렁
	BH_HUNT,       // 근처 잡몹을 쫓아 잡는다 — 플레이어와 먹이 경쟁
	BH_PATROL,     // 넓게 돈다
	BH_ROB,        // 장사·여행 중인 주민을 쫓아가 턴다
	BH_TRADE,      // 노점에 서서 호객
	BH_TRAVEL,     // 길을 따라 멀리 오간다
	BH_STALK,      // 사이가 나쁘면 플레이어를 따라붙는다
	BH_SCHEME,     // 구석에서 수군거린다
	BH_GOSSIP,     // 사람 많은 데서 떠든다
	BH_COUNT
};

static const char* const BEHAVIOR_IDS[BH_COUNT] =
{
	"stay", "wander", "hunt", "patrol", "rob", "trade", "travel", "stalk", "scheme", "gossip"
};

enum Look
{
	LK_VILLAGER = 0, LK_BANDIT, LK_MERCHANT, LK_TRAVELER, LK_HUNTER, LK_PILGRIM,
	LK_BARD, LK_NOBLE, LK_VILLAIN, LK_OFFICIAL, LK_GUARD, LK_CLOAK, LK_COUNT
};

static const char* const LOOK_IDS[LK_COUNT] =
{
	"villager", "bandit", "merchant", "traveler", "hunter", "pilgrim",
	"bard", "noble", "villain", "official", "guard", "cloak"
};

static const float NPC_WALK_SPEED   = 2.3f;
static const float HUNT_SEARCH      = 16.0f;   // 위치에서 이 반경 안의 잡몹만 노린다
static const float CATCH_DIST       = 0.9f;
static const float STALK_RANGE      = 9.0f;
static const float STALK_KEEP       = 2.2f;
static const float BARK_RANGE       = 6.0f;
static const float HOSTILE_RANGE    = 2.6f;

struct Agent
{
	int     npc;          // Sim NPC 인덱스
	int     ent;          // g_Ents 의 E_NPC
	wchar_t name[16];
	wchar_t title[32];
	wchar_t bark[BH_COUNT][96];
	Col     cloth;
	int     look;
	int     loc[4];       // 구간별 위치 (없으면 -1)

	float   x, y;
	float   tx, ty;
	std::vector<int> path;
	size_t  pathPos;
	bool    needPath;
	float   stuckT;

	float   faceX, faceY, flipX;
	float   phase;
	bool    walking;
	float   speed;

	int     seg;
	int     behavior;
	float   thinkT;
	int     target;       // 사냥: 잡몹 엔티티, 약탈: 에이전트
	float   repathT;
	float   fleeT;
	bool    indoor;       // 밤에 집 안
	bool    present;

	float   barkT;
	float   hostileT;
	wchar_t bubble[96];
	float   bubbleT;
};

static std::vector<Agent> g_Agents;
static std::vector<int>   g_AgentOfNpc;   // Sim NPC -> 에이전트, -1
static int                g_NpcCatches;   // 주민 사냥꾼이 잡은 잡몹 수 (F7)
static int                g_Robberies;

static void Say(Agent& a, const wchar_t* text, float secs)
{
	if (!text || !text[0]) return;
	wcsncpy_s(a.bubble, 96, text, _TRUNCATE);
	// 말풍선은 한 줄이다
	for (int i = 0; a.bubble[i]; ++i) if (a.bubble[i] == L'\n') a.bubble[i] = L' ';
	a.bubbleT = secs;
}

static void PlayerSay(const wchar_t* text, float secs)
{
	wcsncpy_s(g_PlayerBubble, 96, text, _TRUNCATE);
	g_PlayerBubbleT = secs;
}

static void GoTo(Agent& a, float x, float y)
{
	a.tx = x;
	a.ty = y;
	a.path.clear();
	a.pathPos = 0;
	a.needPath = true;
	a.stuckT = 0.0f;
}

static bool Arrived(const Agent& a)
{
	float dx = a.tx - a.x, dy = a.ty - a.y;
	return !a.needPath && dx * dx + dy * dy < 0.25f * 0.25f;
}

// 위치 반경 안의 빈 지점 하나
static void PickPointNear(float cx, float cy, float r, float* ox, float* oy)
{
	for (int i = 0; i < 8; ++i)
	{
		float ang = RandF() * 6.2832f;
		float rr = sqrtf(RandF()) * r;
		float px = cx + cosf(ang) * rr;
		float py = cy + sinf(ang) * rr;
		if (!IsBlocked(px, py))
		{
			*ox = px;
			*oy = py;
			return;
		}
	}
	int fx, fy;
	if (NearestFree(cx, cy, &fx, &fy)) { *ox = fx + 0.5f; *oy = fy + 0.5f; }
	else { *ox = cx; *oy = cy; }
}

static int CurrentLoc(const Agent& a)
{
	int seg = g_Sim.GetSegment();
	return a.loc[seg];
}

static void LocCenter(const Agent& a, float* x, float* y, float* r)
{
	int li = CurrentLoc(a);
	if (li < 0) li = FindLoc("square");
	const Loc& l = g_Locs[li];
	*x = l.x;
	*y = l.y;
	*r = l.r;
}

static int BehaviorOf(const Sim::ActionDef* act)
{
	if (!act) return BH_STAY;
	for (int i = 0; i < BH_COUNT; ++i)
		if (act->behavior == BEHAVIOR_IDS[i]) return i;
	return BH_STAY;
}

static void SpawnAgents()
{
	g_Agents.clear();
	g_AgentOfNpc.assign(g_SimOk ? g_Sim.GetNpcCount() : 0, -1);
	if (!g_SimOk) return;

	int square = FindLoc("square");

	for (int i = 0; i < g_Sim.GetNpcCount(); ++i)
	{
		const Sim::NpcDef& def = g_Sim.GetNpcDef(i);
		if (def.homeRegion != g_HomeRegion) continue;   // 이 맵 주민만 몸을 가진다

		Agent a;
		a.npc = i;
		Widen(def.name, a.name, 16);
		Widen(def.title, a.title, 32);
		for (int b = 0; b < BH_COUNT; ++b)
		{
			a.bark[b][0] = 0;
			for (size_t k = 0; k < def.barks.size(); ++k)
				if (def.barks[k].first == BEHAVIOR_IDS[b]) Widen(def.barks[k].second, a.bark[b], 96);
		}
		a.cloth = def.hasCloth ? Col(def.cloth[0], def.cloth[1], def.cloth[2])
			: Col(0.55f, 0.50f, 0.45f);
		a.look = LK_VILLAGER;
		for (int k = 0; k < LK_COUNT; ++k) if (def.look == LOOK_IDS[k]) a.look = k;

		for (int s = 0; s < 4; ++s)
		{
			a.loc[s] = -1;
			if ((int)def.schedule.size() == 4)
			{
				a.loc[s] = FindLoc(def.schedule[s]);
				if (a.loc[s] < 0)
					printf("[Game] %s: unknown map location '%s'\n", def.id.c_str(),
						def.schedule[s].c_str());
			}
			if (a.loc[s] < 0) a.loc[s] = square;
		}

		float cx, cy, cr;
		LocCenter(a, &cx, &cy, &cr);
		PickPointNear(cx, cy, cr, &a.x, &a.y);
		a.tx = a.x;
		a.ty = a.y;
		a.pathPos = 0;
		a.needPath = false;
		a.stuckT = 0.0f;
		a.faceX = 0.0f;
		a.faceY = 1.0f;
		a.flipX = (RandF() < 0.5f) ? -1.0f : 1.0f;
		a.phase = RandF() * 20.0f;
		a.walking = false;
		a.speed = NPC_WALK_SPEED;
		a.seg = -1;
		a.behavior = -1;
		a.thinkT = RandF() * 2.0f;
		a.target = -1;
		a.repathT = 0.0f;
		a.fleeT = 0.0f;
		a.indoor = false;
		a.present = true;
		a.barkT = 4.0f + RandF() * 8.0f;
		a.hostileT = 0.0f;
		a.bubble[0] = 0;
		a.bubbleT = 0.0f;

		a.ent = AddEnt(E_NPC, a.x, a.y, (int)g_Agents.size());
		g_AgentOfNpc[i] = (int)g_Agents.size();
		g_Agents.push_back(a);
	}
	printf("[Game] village agents: %d\n", (int)g_Agents.size());
}

// 목격자 판정용 (Sim 은 Actor 를 모른다)
static bool LocateNpc(int npcIdx, float* wx, float* wy, void*)
{
	if (npcIdx < 0 || npcIdx >= (int)g_AgentOfNpc.size()) return false;
	int ai = g_AgentOfNpc[npcIdx];
	if (ai < 0) return false;
	const Agent& a = g_Agents[ai];
	if (!a.present || a.indoor) return false;
	*wx = a.x;
	*wy = a.y;
	return true;
}

// ---- 움직임 ---------------------------------------------------------------

static void StepToward(Agent& a, float px, float py, float speed, float dt)
{
	float dx = px - a.x, dy = py - a.y;
	float d = sqrtf(dx * dx + dy * dy);
	if (d < 0.02f) return;
	float step = speed * dt;
	if (step > d) step = d;
	float mx = dx / d, my = dy / d;

	float nx = a.x + mx * step;
	float ny = a.y + my * step;
	float ox = a.x, oy = a.y;
	if (!IsBlocked(nx, a.y)) a.x = nx;
	if (!IsBlocked(a.x, ny)) a.y = ny;

	a.faceX = mx;
	a.faceY = my;
	float screenH = mx - my;
	if (screenH < -0.05f)     a.flipX = -1.0f;
	else if (screenH > 0.05f) a.flipX =  1.0f;

	float moved = fabsf(a.x - ox) + fabsf(a.y - oy);
	if (moved > 0.0001f) a.walking = true;
	if (moved < step * 0.2f) a.stuckT += dt;
	else a.stuckT = 0.0f;
}

static void FollowPath(Agent& a, float dt)
{
	if (a.needPath)
	{
		if (g_PathBudgetLeft <= 0) return;   // 다음 프레임에
		--g_PathBudgetLeft;
		a.needPath = false;
		if (!FindPath(a.x, a.y, a.tx, a.ty, a.path))
		{
			// 못 가는 곳이면 제자리에서 포기한다
			a.tx = a.x;
			a.ty = a.y;
			a.path.clear();
		}
		a.pathPos = 0;
	}

	// 경로를 당긴다 — 한눈에 보이는 다음 칸까지 건너뛴다
	int pulls = 0;
	while (a.pathPos + 1 < a.path.size() && pulls < 4)
	{
		int n = a.path[a.pathPos + 1];
		if (!LineClear(a.x, a.y, n % MAP_W + 0.5f, n / MAP_W + 0.5f)) break;
		++a.pathPos;
		++pulls;
	}

	float px, py;
	if (a.pathPos < a.path.size())
	{
		int c = a.path[a.pathPos];
		px = c % MAP_W + 0.5f;
		py = c / MAP_W + 0.5f;
		// 마지막 칸은 칸 중앙 대신 실제 목표점으로
		if (a.pathPos + 1 == a.path.size()) { px = a.tx; py = a.ty; }
	}
	else
	{
		px = a.tx;
		py = a.ty;
	}

	StepToward(a, px, py, a.speed, dt);

	float dx = px - a.x, dy = py - a.y;
	if (dx * dx + dy * dy < 0.12f * 0.12f && a.pathPos < a.path.size()) ++a.pathPos;

	// 끼었으면 그 목표는 포기한다. 다음 판단에서 새 지점을 고른다
	if (a.stuckT > 1.2f)
	{
		a.stuckT = 0.0f;
		a.path.clear();
		a.pathPos = 0;
		a.tx = a.x;
		a.ty = a.y;
	}
}

// ---- 행동 ---------------------------------------------------------------

static int FindPreyMob(const Agent& a, float cx, float cy)
{
	int best = -1;
	float bestD = 1e9f;
	for (size_t i = 0; i < g_Ents.size(); ++i)
	{
		const Entity& e = g_Ents[i];
		if (e.type != E_MONSTER || !e.active) continue;
		if (e.claim >= 0 && e.claim != (int)(&a - &g_Agents[0])) continue;
		float hx = e.x - cx, hy = e.y - cy;
		if (hx * hx + hy * hy > HUNT_SEARCH * HUNT_SEARCH) continue;
		float dx = e.x - a.x, dy = e.y - a.y;
		float d = dx * dx + dy * dy;
		if (d < bestD) { bestD = d; best = (int)i; }
	}
	return best;
}

static void ReleaseClaim(Agent& a)
{
	if (a.behavior == BH_HUNT && a.target >= 0 && a.target < (int)g_Ents.size())
	{
		if (g_Ents[a.target].claim == (int)(&a - &g_Agents[0])) g_Ents[a.target].claim = -1;
	}
	a.target = -1;
}

static void CatchMob(int entIdx)
{
	Entity& m = g_Ents[entIdx];
	if (m.data == S_TOMATOMA && TomatomaReady())
	{
		CatchFx fx;
		fx.x = m.x;
		fx.y = m.y;
		fx.t = 0.0f;
		fx.dirX = m.dirX;
		fx.dirY = m.dirY;
		g_CatchFx.push_back(fx);
	}
	m.active = false;
	m.claim = -1;
	m.respawn = MOB_RESPAWN_MIN + RandF() * MOB_RESPAWN_VAR;
}

static int FindRobVictim(const Agent& a)
{
	int best = -1;
	float bestD = 25.0f * 25.0f;
	for (size_t i = 0; i < g_Agents.size(); ++i)
	{
		const Agent& v = g_Agents[i];
		if (&v == &a || !v.present || v.indoor || v.fleeT > 0.0f) continue;
		if (v.behavior != BH_TRADE && v.behavior != BH_TRAVEL) continue;
		float dx = v.x - a.x, dy = v.y - a.y;
		float d = dx * dx + dy * dy;
		if (d < bestD) { bestD = d; best = (int)i; }
	}
	return best;
}

// 걸어가는 중이 아니고 쉬는 시간이 끝났으면 위치 근처의 새 지점을 고른다.
// radius 가 0 보다 크면 위치 반경 대신 그 값을 쓴다
static void ThinkWander(Agent& a, float radiusMul, float minWait, float maxWait,
	float radius = 0.0f)
{
	if (a.needPath || !Arrived(a)) return;
	if (a.thinkT > 0.0f) return;
	float cx, cy, cr;
	LocCenter(a, &cx, &cy, &cr);
	float px, py;
	PickPointNear(cx, cy, radius > 0.0f ? radius : cr * radiusMul, &px, &py);
	GoTo(a, px, py);
	a.thinkT = minWait + RandF() * (maxWait - minWait);
}

static void UpdateAgent(Agent& a, float dt)
{
	const Sim::NpcState& st = g_Sim.GetNpcState(a.npc);
	Entity& ent = g_Ents[a.ent];

	a.present = st.alive && !st.captured && st.region == g_HomeRegion;
	if (!a.present)
	{
		ent.active = false;
		return;
	}

	a.walking = false;
	if (a.thinkT > 0.0f)   a.thinkT -= dt;
	if (a.barkT > 0.0f)    a.barkT -= dt;
	if (a.bubbleT > 0.0f)  a.bubbleT -= dt;
	if (a.hostileT > 0.0f) a.hostileT -= dt;
	if (a.repathT > 0.0f)  a.repathT -= dt;

	// ---- 구간·행동 전환 ----
	// 낮과 저녁에만 오늘의 행동을 한다. 새벽과 밤은 일과표 위치로 오가며 쉰다.
	int seg = g_Sim.GetSegment();
	int bh = BH_STAY;
	if (seg == Sim::SEG_DAY || seg == Sim::SEG_EVENING)
		bh = BehaviorOf(g_Sim.GetCurrentAction(a.npc));

	if (seg != a.seg || bh != a.behavior)
	{
		ReleaseClaim(a);
		a.seg = seg;
		a.behavior = bh;
		a.indoor = false;
		a.thinkT = 0.0f;
		float cx, cy, cr;
		LocCenter(a, &cx, &cy, &cr);
		float px, py;
		PickPointNear(cx, cy, cr, &px, &py);
		GoTo(a, px, py);
	}

	const Loc& here = g_Locs[a.loc[seg]];

	// 밤에 집(실내 위치)에 닿으면 들어가서 안 보인다
	if (a.indoor)
	{
		ent.active = false;
		return;
	}

	float pdx = g_PlayerX - a.x, pdy = g_PlayerY - a.y;
	float pd = sqrtf(pdx * pdx + pdy * pdy);
	Sim::Band band = g_Sim.GetBand(a.npc);

	a.speed = NPC_WALK_SPEED;

	if (a.fleeT > 0.0f)
	{
		// 털린 직후 — 자기 위치로 달아난다
		a.fleeT -= dt;
		a.speed = 3.6f;
	}
	else
	{
		switch (a.behavior)
		{
		case BH_STAY:
			if (Arrived(a) && here.indoor && seg == Sim::SEG_NIGHT)
			{
				a.indoor = true;
				ent.active = false;
				return;
			}
			if (!here.indoor) ThinkWander(a, 0.5f, 4.0f, 9.0f);
			break;

		case BH_WANDER:
		case BH_GOSSIP:
		case BH_SCHEME:
			ThinkWander(a, a.behavior == BH_WANDER ? 1.8f : 0.8f, 2.5f, 7.0f);
			break;

		case BH_TRADE:
			ThinkWander(a, 0.4f, 5.0f, 10.0f);
			break;

		case BH_PATROL:
			a.speed = 2.6f;
			ThinkWander(a, 3.0f, 0.5f, 2.0f);
			break;

		case BH_TRAVEL:
			a.speed = 2.5f;
			ThinkWander(a, 1.0f, 1.0f, 4.0f, 12.0f);   // 위치 크기와 상관없이 멀리 오간다
			break;

		case BH_HUNT:
		{
			a.speed = 3.1f;
			bool valid = a.target >= 0 && g_Ents[a.target].active
				&& g_Ents[a.target].type == E_MONSTER;
			if (!valid)
			{
				a.target = -1;
				if (a.thinkT <= 0.0f)
				{
					int m = FindPreyMob(a, here.x, here.y);
					if (m >= 0)
					{
						a.target = m;
						g_Ents[m].claim = (int)(&a - &g_Agents[0]);
						GoTo(a, g_Ents[m].x, g_Ents[m].y);
						a.repathT = 1.5f;
					}
					else
					{
						ThinkWander(a, 2.0f, 2.0f, 4.0f);
					}
				}
			}
			else
			{
				Entity& m = g_Ents[a.target];
				float mdx = m.x - a.x, mdy = m.y - a.y;
				float md = sqrtf(mdx * mdx + mdy * mdy);
				if (md < CATCH_DIST)
				{
					CatchMob(a.target);
					++g_NpcCatches;
					a.target = -1;
					a.thinkT = 3.0f + RandF() * 4.0f;   // 잡고 나면 한숨 돌린다
					if (pd < BARK_RANGE * 1.5f) Say(a, L"잡았다!", 2.0f);
					GoTo(a, a.x, a.y);
				}
				else if (a.repathT <= 0.0f || (md < 3.0f && Arrived(a)))
				{
					GoTo(a, m.x, m.y);
					a.repathT = (md < 3.0f) ? 0.4f : 1.5f;
				}
			}
			break;
		}

		case BH_ROB:
		{
			a.speed = 3.3f;
			bool valid = a.target >= 0 && g_Agents[a.target].present
				&& !g_Agents[a.target].indoor && g_Agents[a.target].fleeT <= 0.0f;
			if (!valid)
			{
				a.target = -1;
				if (a.thinkT <= 0.0f)
				{
					a.target = FindRobVictim(a);
					if (a.target < 0)
					{
						// 먹잇감이 없으면 길목에서 서성인다
						ThinkWander(a, 1.5f, 2.0f, 5.0f);
					}
					else a.repathT = 0.0f;
				}
			}
			if (a.target >= 0)
			{
				Agent& v = g_Agents[a.target];
				float vdx = v.x - a.x, vdy = v.y - a.y;
				float vd = sqrtf(vdx * vdx + vdy * vdy);
				if (vd < 1.0f)
				{
					Say(a, a.bark[BH_ROB][0] ? a.bark[BH_ROB] : L"가진 거 내놔!", 3.0f);
					Say(v, L"도둑이야! 누가 좀 도와줘요!", 3.0f);
					v.fleeT = 5.0f;
					float fx, fy, fr;
					LocCenter(v, &fx, &fy, &fr);
					GoTo(v, fx, fy);
					a.target = -1;
					a.thinkT = 18.0f + RandF() * 10.0f;   // 한탕 뒤에는 한동안 잠잠하다
					++g_Robberies;
					printf("[Game] %s robbed %s\n", g_Sim.GetNpcDef(a.npc).id.c_str(),
						g_Sim.GetNpcDef(v.npc).id.c_str());
				}
				else if (a.repathT <= 0.0f)
				{
					GoTo(a, v.x, v.y);
					a.repathT = 1.2f;
				}
			}
			break;
		}

		case BH_STALK:
			if (pd < STALK_RANGE && band <= Sim::BAND_WARY)
			{
				a.speed = 3.0f;
				if (a.repathT <= 0.0f)
				{
					if (pd > STALK_KEEP)
					{
						GoTo(a, g_PlayerX - pdx / pd * STALK_KEEP, g_PlayerY - pdy / pd * STALK_KEEP);
					}
					else GoTo(a, a.x, a.y);
					a.repathT = 0.6f;
				}
				if (a.barkT <= 0.0f && a.bark[BH_STALK][0])
				{
					Say(a, a.bark[BH_STALK], 3.5f);
					a.barkT = 9.0f + RandF() * 5.0f;
				}
			}
			else ThinkWander(a, 1.5f, 2.0f, 6.0f);
			break;
		}
	}

	FollowPath(a, dt);

	// 서 있을 때는 플레이어가 가까우면 쳐다본다
	if (!a.walking && pd < 4.0f && pd > 0.01f)
	{
		a.faceX = pdx / pd;
		a.faceY = pdy / pd;
		float screenH = a.faceX - a.faceY;
		if (screenH < -0.05f)     a.flipX = -1.0f;
		else if (screenH > 0.05f) a.flipX =  1.0f;
	}

	// ---- 적대 조우 (명세 7.2 OnHostileEncounter — 전투 확정 전 스텁) ----
	if (band == Sim::BAND_HOSTILE && pd < HOSTILE_RANGE && a.hostileT <= 0.0f)
	{
		wchar_t line[96];
		Widen(g_Sim.GetGreeting(a.npc), line, 96);
		Say(a, line, 3.5f);
		a.hostileT = 10.0f;
		printf("[Game] hostile encounter: %s\n", g_Sim.GetNpcDef(a.npc).id.c_str());
	}
	// ---- 혼잣말 — 오늘 행동에 맞는 한마디 ----
	else if (pd < BARK_RANGE && a.barkT <= 0.0f && a.bubbleT <= 0.0f && a.behavior >= 0
		&& a.bark[a.behavior][0] && a.behavior != BH_STALK)
	{
		Say(a, a.bark[a.behavior], 3.5f);
		a.barkT = 10.0f + RandF() * 8.0f;
	}

	// 걸음 위상 — 플레이어와 같은 박자 (걸을 때 9.5, 서 있을 때 숨쉬기 1.6)
	a.phase += dt * (a.walking ? 9.5f * a.speed / PLAYER_SPEED * 1.4f : 1.6f);
	ent.x = a.x;
	ent.y = a.y;
	ent.faceX = a.flipX;
	ent.active = true;
}

static void UpdateAgents(float dt)
{
	PROFILE_SCOPE("tutorial.npc");
	g_PathBudgetLeft = PATH_BUDGET;

	int present = 0;
	for (size_t i = 0; i < g_Agents.size(); ++i)
	{
		UpdateAgent(g_Agents[i], dt);
		if (g_Agents[i].present && !g_Agents[i].indoor) ++present;
	}
	PROFILE_GAUGE("tutorial.npcs_present", present);
}

// -----------------------------------------------------------------------------
// 리셋
// -----------------------------------------------------------------------------

static void BakeTerrain();   // 그리기 쪽 (곡선 지형)

void Reset()
{
	g_Ents.clear();
	g_Locs.clear();

	BuildTerrain();
	BakeTerrain();
	BuildVillage();
	BuildForest();
	BuildMonsters();

	// Sim 도 처음부터. 같은 시드라 같은 하루가 다시 시작된다
	g_SimError[0] = 0;
	g_SimOk = g_Sim.Load(SIM_DATA_DIR, SIM_SEED);
	if (g_SimOk)
	{
		g_Sim.SetDayLength(TEST_DAY_SECONDS);
		g_HomeRegion = g_Sim.GetData().FindRegion("home");
		g_Sim.SetPlayerRegion(g_HomeRegion);
		g_Sim.SetNpcLocator(LocateNpc, 0);
		g_Sim.Update(0.0f);   // 0일차 새벽 틱 — 주민이 오늘 할 일을 정한다
	}
	else
	{
		printf("[Game] Sim data failed to load. The village runs without residents.\n");
		wchar_t why[200];
		Widen(g_Sim.GetData().GetError(), why, 200);
		swprintf_s(g_SimError, 256, L"주민 데이터 로드 실패 — %ls", why);
	}
	SpawnAgents();
	g_NpcCatches  = 0;
	g_Robberies   = 0;

	g_Quest       = Q_TALK;
	g_TomatoCount = 0;
	g_HerbCount   = 0;
	g_CatchCount  = 0;
	g_DlgOpen     = false;
	g_PlayerX     = 64.0f + OFS;
	g_PlayerY     = 72.0f + OFS;
	g_WalkPhase   = 0.0f;
	g_Walking     = false;
	g_FaceX       = 0.0f;
	g_FaceY       = 1.0f;
	g_FlipX       = 1.0f;
	g_Time        = 0.0f;
	g_ClearAt     = -1.0f;
	g_GratitudeT  = 0.0f;
	g_PlayerBubbleT = 0.0f;
	g_GratAnimT   = -1.0f;
	g_TartuX      = g_PlayerX - TARTU_FOLLOW_DIST;
	g_TartuY      = g_PlayerY - 0.4f;
	g_TartuDirX   = 1.0f;
	g_TartuDirY   = 1.0f;
	g_TartuT      = 0.0f;
	g_TartuIdleT  = 0.0f;
	g_TartuMoving = false;
	g_CatchFx.clear();
	g_NoticeSeen  = 0;
	g_NoticeT     = 0.0f;

	for (int i = 0; i < 256; ++i) g_Key[i] = false;

	int mobs = 0;
	for (size_t i = 0; i < g_Ents.size(); ++i) if (g_Ents[i].type == E_MONSTER) ++mobs;
	printf("[Game] village: %dx%d tiles, %d entities, %d monsters, %d locations\n",
		MAP_W, MAP_H, (int)g_Ents.size(), mobs, (int)g_Locs.size());
}

void Init(Renderer* renderer, int screenW, int screenH)
{
	R = renderer;
	SCR_W = screenW;
	SCR_H = screenH;
	LoadSprites();
	Reset();
}

void SetKey(unsigned char key, bool down)
{
	g_Key[key] = down;
}

// F5 NPC 현황, F6 다음 날, F7 상태 덤프, F8 다음 구간.
// GLUT 특수 키 번호(GLUT_KEY_F5 = 5 ...)를 그대로 받는다.
void SpecialKey(int key)
{
	if (key == 5) g_ShowNpcPanel = !g_ShowNpcPanel;
	if (!g_SimOk) return;
	if (key == 6) g_Sim.SkipToNextDawn();
	if (key == 7)
	{
		g_Sim.DumpState();
		printf("[Game] ==== agents (segment %d) — npc catches %d, robberies %d ====\n",
			g_Sim.GetSegment(), g_NpcCatches, g_Robberies);
		for (size_t i = 0; i < g_Agents.size(); ++i)
		{
			const Agent& a = g_Agents[i];
			printf("  %-10s at (%6.1f,%6.1f) loc %-13s behavior %-7s target %4d%s%s\n",
				g_Sim.GetNpcDef(a.npc).id.c_str(), a.x, a.y,
				g_Locs[a.loc[g_Sim.GetSegment()]].id.c_str(),
				a.behavior >= 0 ? BEHAVIOR_IDS[a.behavior] : "-", a.target,
				a.indoor ? " indoor" : "", a.present ? "" : " absent");
		}
	}
	if (key == 8) g_Sim.DebugAdvanceSegment();
}

// -----------------------------------------------------------------------------
// 야생 미식종 AI — 비선공. 자기 영역 안을 어슬렁거리다가, 플레이어가 너무
// 가까이 오면 슬쩍 물러난다. 먼저 덤비는 일은 없다.
// -----------------------------------------------------------------------------

static void UpdateMonsters(float dt)
{
	int alive = 0;
	for (size_t i = 0; i < g_Ents.size(); ++i)
	{
		Entity& e = g_Ents[i];
		if (e.type != E_MONSTER) continue;

		if (!e.active)
		{
			// 재출현 — 플레이어 눈앞에서 솟아나지 않게 멀 때만
			if (e.respawn <= 0.0f) continue;
			e.respawn -= dt;
			if (e.respawn <= 0.0f)
			{
				float dx = g_PlayerX - e.homeX, dy = g_PlayerY - e.homeY;
				if (dx * dx + dy * dy < 8.0f * 8.0f) { e.respawn = 5.0f; continue; }
				e.active = true;
				e.x = e.tgtX = e.homeX;
				e.y = e.tgtY = e.homeY;
				e.claim = -1;
			}
			continue;
		}
		++alive;

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
			e.dirX = mvx;
			e.dirY = mvy;
			if (mvx > 0.02f) e.faceX = 1.0f;
			else if (mvx < -0.02f) e.faceX = -1.0f;

			float nx = e.x + mvx * speed * dt;
			float ny = e.y + mvy * speed * dt;
			if (!IsBlocked(nx, e.y)) e.x = nx;
			if (!IsBlocked(e.x, ny)) e.y = ny;
		}
	}
	PROFILE_GAUGE("tutorial.monsters_alive", alive);
}

static void PollNotices(float dt)
{
	if (g_NoticeT > 0.0f) g_NoticeT -= dt;
	if (!g_SimOk) return;

	int serial = g_Sim.GetNoticeSerial();
	if (serial == g_NoticeSeen) return;

	// 한 틱에 여럿 나오면 마지막 것만 띄운다 (나머지는 콘솔에 남아 있다)
	const std::vector<Sim::Notice>& list = g_Sim.GetNotices();
	if (!list.empty())
	{
		Widen(list.back().text, g_NoticeText, 160);
		g_NoticeT = 5.0f;
	}
	g_NoticeSeen = serial;
}

// 타르투: 리노 등 뒤 한 걸음 자리를 쫓는다. 멀어지면 빨리, 가까우면 선다.
static void UpdateTartu(float dt)
{
	float bx = g_PlayerX - g_FaceX * TARTU_FOLLOW_DIST;
	float by = g_PlayerY - g_FaceY * TARTU_FOLLOW_DIST;
	float dx = bx - g_TartuX;
	float dy = by - g_TartuY;
	float d = sqrtf(dx * dx + dy * dy);

	// 순간이동·재시작으로 너무 벌어지면 그냥 옆에 데려다 놓는다
	if (d > 10.0f)
	{
		g_TartuX = bx;
		g_TartuY = by;
		d = 0.0f;
	}

	bool wasMoving = g_TartuMoving;
	g_TartuMoving = d > (g_TartuMoving ? 0.15f : 0.55f);
	if (g_TartuMoving)
	{
		float sp = PLAYER_SPEED * (d > 2.5f ? 1.25f : 0.95f);
		float step = sp * dt;
		if (step > d) step = d;
		float mx = dx / d, my = dy / d;
		float nx = g_TartuX + mx * step;
		float ny = g_TartuY + my * step;
		if (!IsBlocked(nx, g_TartuY)) g_TartuX = nx;
		if (!IsBlocked(g_TartuX, ny)) g_TartuY = ny;
		g_TartuDirX = mx;
		g_TartuDirY = my;
		g_TartuIdleT = 0.0f;
	}
	else
	{
		g_TartuIdleT += dt;
	}
	if (wasMoving != g_TartuMoving) g_TartuT = 0.0f;
	g_TartuT += dt;
}

static void UpdateSpriteFx(float dt)
{
	if (!g_SprOk) return;

	if (g_GratAnimT >= 0.0f)
	{
		g_GratAnimT += dt;
		// 걷기 시작하면 의례를 끊는다. 끝나면 마지막 포즈에 조금 머문다
		if (g_Walking || g_GratAnimT > g_Spr.AnimDuration(g_AnRinoGrat) + 0.35f)
			g_GratAnimT = -1.0f;
	}

	if (TomatomaReady())
	{
		float len = g_Spr.AnimDuration(g_AnTomCaught) + 0.25f;
		for (size_t i = 0; i < g_CatchFx.size(); )
		{
			g_CatchFx[i].t += dt;
			if (g_CatchFx[i].t > len)
			{
				g_CatchFx[i] = g_CatchFx.back();
				g_CatchFx.pop_back();
			}
			else
			{
				++i;
			}
		}
	}
	else
	{
		g_CatchFx.clear();
	}

	if (TartuReady()) UpdateTartu(dt);
}

void Update(float dt)
{
	g_Time += dt;

	// 대화창이 열려 있으면 시계가 멈춘다 (명세 4.4)
	if (g_SimOk) g_Sim.Update(g_DlgOpen ? 0.0f : dt);

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

		// 화면 좌우는 월드 축이 아니라 (x - y) 다. 이 값이 0인 정면/후면
		// 이동에서는 직전 방향을 유지해야 제자리에서 뒤집히지 않는다.
		float screenH = mx - my;
		if (screenH < -0.05f)     g_FlipX = -1.0f;
		else if (screenH > 0.05f) g_FlipX =  1.0f;

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

	if (g_GratitudeT > 0.0f) g_GratitudeT -= dt;
	if (g_PlayerBubbleT > 0.0f) g_PlayerBubbleT -= dt;
	UpdateSpriteFx(dt);

	UpdateMonsters(dt);
	if (!g_DlgOpen) UpdateAgents(dt);
	PollNotices(dt);
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

static const wchar_t* BandName(Sim::Band b)
{
	switch (b)
	{
	case Sim::BAND_ALLY:     return L"동맹";
	case Sim::BAND_FRIENDLY: return L"우호";
	case Sim::BAND_NEUTRAL:  return L"중립";
	case Sim::BAND_WARY:     return L"경계";
	default:                 return L"적대";
	}
}

static Col BandColor(Sim::Band b)
{
	switch (b)
	{
	case Sim::BAND_ALLY:     return Col(0.98f, 0.62f, 0.70f);
	case Sim::BAND_FRIENDLY: return Col(0.60f, 0.88f, 0.52f);
	case Sim::BAND_NEUTRAL:  return Col(0.82f, 0.82f, 0.78f);
	case Sim::BAND_WARY:     return Col(1.00f, 0.80f, 0.32f);
	default:                 return Col(0.96f, 0.34f, 0.28f);
	}
}

static void TalkToAgent(Agent& a)
{
	// 인사 — 관계 단계별 (없는 단계는 중립으로 대체)
	static wchar_t greet[256];
	Widen(g_Sim.GetGreeting(a.npc), greet, 256);

	static wchar_t l1[128], l2[128], l3[160];
	l1[0] = l2[0] = l3[0] = 0;
	wchar_t* nl = wcschr(greet, L'\n');
	if (nl)
	{
		*nl = 0;
		wcsncpy_s(l2, 128, nl + 1, _TRUNCATE);
	}
	wcsncpy_s(l1, 128, greet, _TRUNCATE);
	if (!l1[0]) wcsncpy_s(l1, 128, L"......", _TRUNCATE);

	Sim::Band band = g_Sim.GetBand(a.npc);

	// 장사꾼은 단계에 따라 거래 조건이 달라진다 (7.2)
	const wchar_t* trade = 0;
	if (a.behavior == BH_TRADE)
	{
		if (band >= Sim::BAND_FRIENDLY)     trade = L"거래 10% 할인";
		else if (band == Sim::BAND_NEUTRAL) trade = L"거래 가능";
		else                                trade = L"거래 거부";
	}

	if (SHOW_NPC_DEBUG_LINE)
	{
		const Sim::ActionDef* act = g_Sim.GetCurrentAction(a.npc);
		wchar_t actName[32];
		actName[0] = 0;
		if (act) Widen(act->name, actName, 32);
		swprintf_s(l3, 160, L"[%ls]  오늘: %ls   ·   %ls (%d)%ls%ls",
			a.title, actName[0] ? actName : L"-", BandName(band),
			g_Sim.GetEffAffinity(a.npc), trade ? L"   ·   " : L"", trade ? trade : L"");
	}
	else if (trade)
	{
		wcsncpy_s(l3, 160, trade, _TRUNCATE);
	}

	OpenDialog(a.name, l1, l2[0] ? l2 : 0, l3[0] ? l3 : 0);
}

// 포획 공통 — 목격자 반응과 먹이 경쟁
static void ReportCatch(Entity& e, const char* eventId)
{
	if (!g_SimOk) return;
	g_Sim.OnPlayerEvent(eventId, e.x, e.y);

	// 주민 사냥꾼이 노리던 먹이를 가로챘다
	if (e.claim >= 0 && e.claim < (int)g_Agents.size())
	{
		Agent& h = g_Agents[e.claim];
		g_Sim.OnPlayerEvent("stole_prey", e.x, e.y, h.npc);
		Say(h, L"...그거, 내가 먼저 노렸는데.", 3.0f);
		h.target = -1;
		e.claim = -1;
	}
	++g_CatchCount;
	g_GratitudeT = 4.0f;
}

void Gratitude()
{
	if (g_DlgOpen || g_GratitudeT <= 0.0f) return;
	g_GratitudeT = 0.0f;
	g_GratAnimT = 0.0f;
	PlayerSay(L"네 힘, 한 톨도 남기지 않고 받겠습니다!", 3.0f);
	if (g_SimOk) g_Sim.OnPlayerEvent("gratitude", g_PlayerX, g_PlayerY);
}

void Interact()
{
	if (g_DlgOpen) { g_DlgOpen = false; return; }

	int idx = FindInteractable();
	if (idx < 0) return;

	Entity& e = g_Ents[idx];

	if (e.type == E_NPC)
	{
		Agent& a = g_Agents[e.data];
		const Sim::NpcDef& def = g_Sim.GetNpcDef(a.npc);
		if (def.id == "mira") TalkToMira();   // 튜토리얼 스크립트 (명세 1장 예외)
		else TalkToAgent(a);
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
		ReportCatch(e, "catch_small");
		CatchMob(idx);

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
			swprintf_s(buf, 128, L"%ls을(를) 잡았다. 포획 등급 %d.",
				g_Species[sp].name, g_Species[sp].grade);
			OpenDialog(0, buf, L"의뢰와는 상관없지만, 나쁘지 않은 재료다.", 0);
		}
		return;
	}

	if (e.type == E_BEAST && g_Quest == Q_BEAST)
	{
		e.active = false;
		g_Quest = Q_COOK;
		ReportCatch(e, "beast_catch");
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

// ---- 곡선 지형 --------------------------------------------------------------
//
// 타일마다 마름모를 그리면 격자가 그대로 드러난다. 레벨 1 과 같은 TerrainLayer
// 로 바꿨다: 셰이더가 픽셀마다 주변 네 타일을 섞고 경계를 노이즈로 휘게 해서
// 풀·길·광장·모래·물의 경계가 곡선이 되고 타일 선이 사라진다. draw call 1.
//
// 경계가 얼마나 휘는지(타일 단위)와 얼마나 또렷한지. 휨을 0.5 가까이 올리면
// 폭 2 짜리 길이 끊어져 보이기 시작한다.
static const float TERRAIN_WARP  = 0.38f;
static const float TERRAIN_SHARP = 7.0f;
static const int   TERRAIN_TYPES = 7;   // T_GRASS .. T_FARM

static TerrainLayer g_Terrain;

// 아래 세 함수는 Shaders/Ground.fs 의 Hash / Noise / 가중치 계산과 똑같아야
// 한다 (Level1.cpp 와도 같다). 화면에 보이는 곡선 경계 안쪽에만 풀·자갈을
// 심는 데 쓴다.
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

static void BakeTerrain()
{
	g_Terrain.SetTiles(&g_Tile[0][0], MAP_W, MAP_H);
	g_Terrain.SetTypeCount(TERRAIN_TYPES);
	g_Terrain.SetTypeColor(T_GRASS,  C_GRASS);
	g_Terrain.SetTypeColor(T_FOREST, C_FOREST);
	g_Terrain.SetTypeColor(T_PATH,   C_PATH);
	g_Terrain.SetTypeColor(T_SQUARE, C_SQUARE);
	g_Terrain.SetTypeColor(T_SAND,   C_SAND);
	g_Terrain.SetTypeColor(T_WATER,  C_WATER);
	g_Terrain.SetTypeColor(T_FARM,   C_FARM);
	g_Terrain.SetWaterType(T_WATER);
	g_Terrain.SetShape(TERRAIN_WARP, TERRAIN_SHARP);
}

// 땅 위 잔디·자갈·꽃. 평면 디테일이라 깊이 정렬에 넣을 필요가 없다.
//
// 예전에는 타일 중앙 높이에 가로로만 흔들어 찍어서 줄이 맞아 보였다. 이제
// 자리를 타일 안 아무 데나(월드 좌표로) 흩고, 곡선 경계로 봤을 때 실제로 그
// 지형인 곳에만 심는다. 안 그러면 휘어 들어간 물가나 길 위에 풀이 뜬다.
static void GroundDetail(int x, int y, float offX, float offY)
{
	float h = Hash2(x, y);
	if (h < 0.72f) return;

	float wx = x + 0.12f + Hash(x * 7, y * 3 + 1) * 0.76f;
	float wy = y + 0.12f + Hash(x * 5 + 2, y * 11) * 0.76f;
	int t = TerrainTypeAt(wx, wy);

	float sx, sy;
	IsoToScreen(wx, wy, &sx, &sy);
	sx += offX;
	sy += offY;

	if (t == T_GRASS || t == T_FOREST)
	{
		Col tuft = Shade(t == T_GRASS ? C_GRASS : C_FOREST, 1.16f + Hash(x, y * 7) * 0.12f);
		float th = 7.0f + Hash(x * 3, y) * 4.0f;
		R->DrawTriangle(sx, sy - th * 0.4f, 5.0f, th, tuft);
		R->DrawTriangle(sx + 5.0f, sy - th * 0.3f, 4.0f, th * 0.75f, Shade(tuft, 0.9f));

		if (h > 0.965f)
		{
			// 라벤더와 들꽃 — 프로방스 톤을 땅에서부터 깔아 준다
			Col f = (Hash(x, y * 3) > 0.5f)
				? Col(0.66f, 0.55f, 0.78f)
				: Col(0.94f, 0.86f, 0.52f);
			R->DrawEllipse(sx - 3.0f, sy - 8.0f, 5.0f, 5.0f, f, 0.20f);
		}
	}
	else if (t == T_PATH || t == T_SQUARE)
	{
		Col p = Shade(t == T_PATH ? C_PATH : C_SQUARE, 0.88f);
		R->DrawEllipse(sx, sy, 8.0f + Hash(y, x) * 4.0f, 5.0f, p);
	}
	else if (t == T_SAND && h > 0.90f)
	{
		R->DrawEllipse(sx, sy, 7.0f, 4.0f, Shade(C_SAND, 0.92f));
	}
	else if (t == T_FARM)
	{
		// 밭 흙덩이. 타일마다 같은 가로줄을 그으면 격자가 다시 보인다
		R->DrawEllipse(sx, sy, 12.0f + Hash(x * 9, y) * 8.0f, 4.0f, Shade(C_FARM, 0.84f));
	}
}

// ---- 개체 ---------------------------------------------------------------

static void DrawTree(const Entity& e, float sx, float sy)
{
	float v = Hash((int)(e.x * 7.0f), (int)(e.y * 13.0f));

	if (g_SprOk && g_SpTree >= 0)
	{
		// 한 장을 크기·색·좌우만 바꿔 숲 전체에 쓴다. 바람은 발을 축으로 GPU 가 돌린다
		float k = 0.92f + v * 0.20f;
		Col tint(0.90f + v * 0.16f, 0.93f + v * 0.10f, 0.90f + v * 0.06f);
		R->DrawShadow(sx + 5.0f, sy + 2.0f, 74.0f * k, 30.0f * k, 0.30f, 0.26f);
		R->SetSway(sx, sy, e.phase, 0.016f, 0.7f);
		DrawSpr(g_Spr.Frame(g_SpTree), sx, sy, k, v > 0.5f, tint);
		R->ClearSway();
		return;
	}
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

	if (g_SprOk && g_SpHouse >= 0)
	{
		// 주택은 좌우 반전과 명도로 변화를 준다. 영주 저택은 보랏빛으로 눌러
		// 마을 집들과 한눈에 달라 보이게 한다 (예전 검은 지붕 덧칠의 대신)
		Col tint = Shade(Col(1.0f, 1.0f, 1.0f), 0.94f + v * 0.08f);
		if (e.data == HK_MANOR) tint = Col(0.78f, 0.72f, 0.84f);
		bool flip = (e.data < HK_TAVERN) && (((int)(v * 97.0f)) & 1);
		R->DrawShadow(sx + 6.0f, sy + 6.0f, 128.0f, 50.0f, 0.34f, 0.24f);
		DrawSpr(g_Spr.Frame(g_SpHouse), sx, sy, 1.0f, flip, tint);
		return;
	}
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
	if (g_SprOk && g_SpHearth >= 0 && g_SpCauldron >= 0)
	{
		// 화덕 → 불꽃(셰이더) → 솥 순서. 솥이 돌 위로 떠 있어서 그 틈으로 불이 보인다
		float amp = (g_Quest == Q_DONE) ? 1.0f : 0.6f;
		R->DrawShadow(sx + 3.0f, sy + 3.0f, 58.0f, 26.0f, 0.30f, 0.24f);
		DrawSpr(g_Spr.Frame(g_SpHearth), sx, sy, 1.0f, false, Col(1.0f, 1.0f, 1.0f));
		R->DrawEllipse(sx, sy - 6.0f, 54.0f, 26.0f,
			Fade(Col(1.0f, 0.66f, 0.30f), 0.16f + sinf(g_Time * 6.1f) * 0.04f * amp));
		R->DrawFlame(sx, sy - 13.0f, 26.0f, 30.0f, g_Time, 0.75f + 0.25f * amp, 1.0f);
		DrawSpr(g_Spr.Frame(g_SpCauldron), sx, sy, 1.0f, false, Col(1.0f, 1.0f, 1.0f));

		if (g_Quest == Q_DONE)
		{
			for (int i = 0; i < 5; ++i)
			{
				float t = g_Time * 0.75f + i * 0.62f;
				float up = t - floorf(t);
				float a = (1.0f - up) * 0.34f;
				float wob = sinf(t * 3.2f + i) * 9.0f;
				R->DrawEllipse(sx + wob, sy - 36.0f - up * 62.0f,
					16.0f + up * 22.0f, 14.0f + up * 20.0f,
					Fade(Col(1.0f, 0.97f, 0.90f), a));
			}
			// 완성된 한 그릇이 솥 옆에 놓인다
			if (g_SpDishIcon >= 0)
				DrawSpr(g_Spr.Frame(g_SpDishIcon), sx + 40.0f, sy + 12.0f, 1.0f, false,
					Col(1.0f, 1.0f, 1.0f));
		}
		return;
	}

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
// 토마토마 한 장. t 는 동작 경과(초)
static void DrawTomatomaSprite(int anim, float t, float dirX, float dirY, float sx, float sy,
	float shadowK)
{
	bool flip = false;
	int dir = SpriteAtlas::SpriteDirFromWorld(dirX, dirY, &flip);
	if (shadowK > 0.0f)
		R->DrawShadow(sx + 2.0f, sy + 1.0f, 36.0f * shadowK, 15.0f * shadowK, 0.28f * shadowK, 0.26f);
	DrawSpr(g_Spr.AnimFrameAt(anim, dir, t), sx, sy, TOMATOMA_SPRITE_SCALE, flip,
		Col(1.0f, 1.0f, 1.0f));
}

static void DrawMonster(const Entity& e, float sx, float sy)
{
	const SpeciesInfo& si = g_Species[e.data];

	if (e.data == S_TOMATOMA && TomatomaReady())
	{
		// phase 는 초당 3.4 씩 오른다 -> 초로 되돌려 애니메이션 시간에 쓴다
		float t = e.phase / 3.4f;
		DrawTomatomaSprite(e.moving ? g_AnTomHop : g_AnTomIdle, t, e.dirX, e.dirY, sx, sy, 1.0f);
		return;
	}

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

// 사람.
//
// 팔다리가 없는 단순 형태다. 움직임은 관절 회전이 아니라 몸통 기울기,
// 스쿼시, 손발의 지연 오프셋으로 만든다.
// 반환값은 수직 바운스(px). 머리에 얹는 소품이 같은 높이를 따라가야 한다
static float DrawPerson(float sx, float sy, const Col& cloth,
	float phase, bool walking, float faceX, float faceY, float flipX, bool isPlayer)
{
	Palette pal = Models::MakeHumanPalette(cloth, isPlayer);

	float ang[HJ_COUNT];
	float off[HJ_COUNT * 2];
	float squash = 1.0f;
	int   poseFlags = 0;
	float bob = Models::HumanPose(ang, off, &squash, &poseFlags,
		phase, walking, -1.0f);

	// 깜빡임 — 개체마다 위상이 달라 동시에 감지 않는다
	float bt = phase * 0.13f;
	bool blink = (bt - floorf(bt)) > 0.955f;

	int flags = (isPlayer ? PF_PLAYER : PF_VILLAGER) | poseFlags;

	// 카메라와의 거리는 월드 y 가 아니라 (x + y) 축이다. y 만 보면 A/D 처럼
	// 깊이가 전혀 변하지 않는 옆걸음에서도 등이 보인다.
	float depth = (faceX + faceY) * 0.5f;
	if (depth > -0.35f) flags |= PF_FACE_ONLY;
	flags |= blink ? PF_EYE_SHUT : PF_EYE_OPEN;

	float flip = (flipX < -0.05f) ? -1.0f : 1.0f;

	R->DrawShadow(sx, sy, 30.0f, 14.0f, 0.34f, 0.22f);
	Models::Draw(R, MODEL_HUMAN, sx, sy - bob,
		ang, off, pal, 1.0f, squash, flip, flags, 1.0f);
	return bob;
}

// ---- 확장판 소품 ------------------------------------------------------------

// 아래를 가리키는 삼각형 (말풍선 꼬리, 목표 표시)
static void DrawDownTriangle(float cx, float cy, float w, float h, const Col& c)
{
	R->SetRotation(PI, 0.0f, 0.0f);
	R->DrawTriangle(cx, cy, w, h, c);
	R->ClearRotation();
}

static void DrawTent(const Entity& e, float sx, float sy)
{
	Col canvas = (e.data == 1) ? Shade(C_CANVAS, 1.02f) : Col(0.58f, 0.30f, 0.24f);
	R->DrawShadow(sx + 4.0f, sy + 2.0f, 76.0f, 30.0f, 0.30f, 0.26f);
	R->DrawTriangle(sx, sy - 26.0f, 72.0f, 54.0f,
		Shade(canvas, 1.10f), Shade(canvas, 0.72f), 0.10f);
	// 입구 — 안이 어두워야 천막으로 읽힌다
	R->DrawTriangle(sx + 4.0f, sy - 14.0f, 22.0f, 30.0f,
		Col(0.16f, 0.13f, 0.12f), Col(0.10f, 0.08f, 0.08f));
	R->DrawRect(sx, sy - 54.0f, 3.0f, 10.0f, Shade(C_WOOD, 1.1f), 0.5f);
}

static void DrawWagon(const Entity& e, float sx, float sy)
{
	float v = Hash((int)e.x, (int)e.y);
	R->DrawShadow(sx + 4.0f, sy + 3.0f, 84.0f, 30.0f, 0.32f, 0.24f);
	// 바퀴 뒤쪽
	R->DrawEllipse(sx - 26.0f, sy - 12.0f, 22.0f, 22.0f, Shade(C_WOOD, 0.80f), Shade(C_WOOD, 0.60f));
	// 짐칸
	R->DrawRect(sx, sy - 24.0f, 74.0f, 22.0f,
		Shade(C_WOOD, 1.28f), Shade(C_WOOD, 0.90f), 0.08f, 0.08f);
	// 덮개 — 상단마다 색이 조금씩 다르다
	Col cover = Mix(C_CANVAS, Col(0.62f, 0.50f, 0.34f), v * 0.6f);
	R->DrawEllipse(sx, sy - 44.0f, 70.0f, 40.0f, Shade(cover, 1.12f), Shade(cover, 0.80f), 0.10f);
	R->DrawRect(sx, sy - 34.0f, 72.0f, 3.0f, Fade(Shade(cover, 0.66f), 0.8f), 0.5f);
	// 바퀴 앞쪽
	R->DrawEllipse(sx + 24.0f, sy - 10.0f, 24.0f, 24.0f, Shade(C_WOOD, 1.05f), Shade(C_WOOD, 0.72f));
	R->DrawEllipse(sx + 24.0f, sy - 10.0f, 6.0f, 6.0f, Col(0.30f, 0.28f, 0.26f));
}

static void DrawStall(const Entity& e, float sx, float sy)
{
	static const Col awning[5] =
	{
		Col(0.80f, 0.36f, 0.30f), Col(0.36f, 0.52f, 0.66f), Col(0.84f, 0.70f, 0.30f),
		Col(0.46f, 0.62f, 0.40f), Col(0.64f, 0.44f, 0.66f)
	};
	static const Col goods[5] =
	{
		Col(0.90f, 0.40f, 0.26f), Col(0.95f, 0.82f, 0.36f), Col(0.60f, 0.72f, 0.36f),
		Col(0.72f, 0.46f, 0.30f), Col(0.86f, 0.84f, 0.78f)
	};
	int k = e.data % 5;

	R->DrawShadow(sx + 3.0f, sy + 2.0f, 70.0f, 26.0f, 0.30f, 0.26f);
	R->DrawRect(sx - 28.0f, sy - 30.0f, 4.0f, 56.0f, Shade(C_WOOD, 1.1f), 0.5f);
	R->DrawRect(sx + 28.0f, sy - 30.0f, 4.0f, 56.0f, Shade(C_WOOD, 1.1f), 0.5f);
	// 판매대와 물건
	R->DrawRect(sx, sy - 12.0f, 64.0f, 20.0f,
		Shade(C_WOOD, 1.30f), Shade(C_WOOD, 0.92f), 0.10f);
	for (int i = 0; i < 4; ++i)
	{
		Col g = Shade(goods[(k + i) % 5], 0.92f + 0.06f * i);
		R->DrawEllipse(sx - 21.0f + i * 14.0f, sy - 25.0f, 12.0f, 10.0f, Shade(g, 1.12f), Shade(g, 0.80f));
	}
	// 줄무늬 차양
	R->DrawRect(sx, sy - 60.0f, 72.0f, 16.0f, Shade(awning[k], 1.10f), Shade(awning[k], 0.80f), 0.12f);
	for (int i = 0; i < 4; ++i)
		R->DrawRect(sx - 27.0f + i * 18.0f, sy - 60.0f, 8.0f, 16.0f, Fade(Col(0.97f, 0.94f, 0.88f), 0.85f));
}

static void DrawCampfire(float sx, float sy)
{
	float glow = 0.20f + sinf(g_Time * 5.3f) * 0.04f;
	R->DrawEllipse(sx, sy - 8.0f, 120.0f, 70.0f, Fade(Col(1.0f, 0.70f, 0.36f), glow));
	for (int i = 0; i < 6; ++i)
	{
		float a = i / 6.0f * 6.2832f;
		R->DrawEllipse(sx + cosf(a) * 16.0f, sy + sinf(a) * 7.0f - 2.0f, 11.0f, 8.0f,
			Shade(C_STONE, 0.95f + 0.05f * (i % 2)), Shade(C_STONE, 0.70f));
	}
	R->DrawRect(sx, sy - 4.0f, 26.0f, 5.0f, Shade(C_WOOD, 0.9f), 0.5f);
	R->DrawFlame(sx, sy - 22.0f, 30.0f, 40.0f, g_Time, 1.0f, 1.0f);
}

// 특수 건물의 간판·장식. 몸체는 DrawHouse 가 그린다
static void DrawBuildingExtra(const Entity& e, float sx, float sy)
{
	switch (e.data)
	{
	case HK_TAVERN:
	case HK_INN:
	{
		// 처마에 매단 간판 — 선술집은 잔, 여관은 달
		R->DrawRect(sx + 44.0f, sy - 60.0f, 20.0f, 3.0f, Shade(C_WOOD, 0.9f), 0.5f);
		R->DrawRect(sx + 50.0f, sy - 46.0f, 22.0f, 20.0f,
			Shade(C_WOOD, 1.35f), Shade(C_WOOD, 1.0f), 0.12f);
		if (e.data == HK_TAVERN)
			R->DrawRect(sx + 50.0f, sy - 46.0f, 9.0f, 11.0f, Col(0.95f, 0.78f, 0.36f), 0.2f);
		else
			R->DrawEllipse(sx + 50.0f, sy - 46.0f, 11.0f, 11.0f, Col(0.95f, 0.92f, 0.70f));
		break;
	}
	case HK_GUILD:
	{
		// 협회 깃발
		R->DrawRect(sx - 40.0f, sy - 92.0f, 3.0f, 56.0f, Shade(C_WOOD, 1.0f), 0.5f);
		float wave = sinf(g_Time * 2.4f) * 2.0f;
		R->DrawRect(sx - 29.0f, sy - 108.0f + wave * 0.3f, 20.0f, 16.0f,
			Col(0.30f, 0.42f, 0.70f), Col(0.20f, 0.30f, 0.54f), 0.08f);
		R->DrawEllipse(sx - 29.0f, sy - 108.0f + wave * 0.3f, 7.0f, 7.0f, Col(0.95f, 0.84f, 0.45f));
		break;
	}
	case HK_MILL:
	{
		// 풍차 날개 — 천천히 돈다
		float cx = sx, cy = sy - 70.0f;
		for (int i = 0; i < 4; ++i)
		{
			float a = g_Time * 0.6f + i * (PI * 0.5f);
			R->SetRotation(a, 0.0f, 0.5f);
			R->DrawRect(cx, cy - 26.0f, 12.0f, 52.0f,
				Fade(C_CANVAS, 0.95f), Fade(Shade(C_CANVAS, 0.8f), 0.95f), 0.10f);
			R->ClearRotation();
		}
		R->DrawEllipse(cx, cy, 10.0f, 10.0f, Shade(C_WOOD, 0.9f));
		break;
	}
	case HK_MANOR:
	{
		// 스프라이트 집은 틴트로 구분한다. 아래 덧칠은 예전 SDF 집 모양에 맞춘 것
		if (g_SprOk && g_SpHouse >= 0) break;
		// 검은 지붕 덧칠과 보라 휘장 — 마을 집들과 한눈에 달라야 한다
		R->DrawTriangle(sx, sy - 84.0f, 98.0f, 42.0f,
			Fade(Col(0.20f, 0.16f, 0.22f), 0.70f), Fade(Col(0.12f, 0.10f, 0.14f), 0.70f), 0.18f);
		R->DrawRect(sx - 30.0f, sy - 108.0f, 10.0f, 26.0f, Shade(C_STONE, 0.8f), 0.1f);
		for (int s = -1; s <= 1; s += 2)
			R->DrawRect(sx + 22.0f * s, sy - 26.0f, 10.0f, 26.0f,
				Col(0.40f, 0.14f, 0.26f), Col(0.28f, 0.08f, 0.18f), 0.1f);
		break;
	}
	default:
		break;
	}
}

// ---- 주민 --------------------------------------------------------------

// 머리 위 모양새. hy 는 머리 꼭대기 높이
static void DrawLook(int look, float sx, float hy, float flip, const Col& cloth,
	bool faceCam, float eyeY)
{
	const Col dark(0.14f, 0.12f, 0.14f);
	switch (look)
	{
	case LK_BANDIT:
	{
		Col red(0.70f, 0.16f, 0.14f);
		R->DrawEllipse(sx, hy + 7.0f, 42.0f, 18.0f, Shade(red, 1.12f), Shade(red, 0.78f));
		R->DrawTriangle(sx - 20.0f * flip, hy + 16.0f, 9.0f, 12.0f, Shade(red, 0.9f));
		R->DrawTriangle(sx - 24.0f * flip, hy + 18.0f, 8.0f, 11.0f, Shade(red, 0.8f));
		if (faceCam)
			R->DrawRect(sx, eyeY, 34.0f, 7.0f, Fade(dark, 0.85f), 0.5f);   // 복면 띠
		break;
	}
	case LK_MERCHANT:
	{
		Col cap(0.46f, 0.32f, 0.20f);
		R->DrawEllipse(sx, hy + 5.0f, 44.0f, 13.0f, Shade(cap, 1.05f), Shade(cap, 0.75f));
		R->DrawEllipse(sx + 3.0f * flip, hy - 1.0f, 32.0f, 15.0f, Shade(cap, 1.20f), Shade(cap, 0.90f));
		break;
	}
	case LK_TRAVELER:
	case LK_PILGRIM:
	{
		Col straw = (look == LK_TRAVELER) ? Col(0.82f, 0.70f, 0.44f) : Col(0.60f, 0.60f, 0.64f);
		R->DrawEllipse(sx, hy + 8.0f, 60.0f, 15.0f, Shade(straw, 1.08f), Shade(straw, 0.78f));
		R->DrawEllipse(sx, hy - 2.0f, 32.0f, 20.0f, Shade(straw, 1.18f), Shade(straw, 0.88f));
		R->DrawRect(sx, hy + 3.0f, 32.0f, 4.0f, Shade(cloth, 0.8f), 0.5f);
		break;
	}
	case LK_HUNTER:
	{
		Col cap(0.30f, 0.40f, 0.24f);
		R->DrawEllipse(sx, hy + 4.0f, 42.0f, 17.0f, Shade(cap, 1.12f), Shade(cap, 0.80f));
		R->DrawTriangle(sx + 14.0f * flip, hy - 8.0f, 6.0f, 20.0f, Col(0.92f, 0.86f, 0.72f));
		break;
	}
	case LK_BARD:
	{
		Col cap(0.76f, 0.24f, 0.30f);
		R->DrawEllipse(sx - 7.0f * flip, hy + 2.0f, 40.0f, 15.0f, Shade(cap, 1.12f), Shade(cap, 0.80f));
		R->SetRotation(0.6f * flip, 0.0f, 0.5f);
		R->DrawTriangle(sx + 12.0f * flip, hy - 12.0f, 7.0f, 26.0f, Col(0.98f, 0.92f, 0.62f));
		R->ClearRotation();
		break;
	}
	case LK_NOBLE:
	{
		R->DrawEllipse(sx, hy + 6.0f, 42.0f, 10.0f, Shade(dark, 1.3f), dark);
		R->DrawRect(sx, hy - 8.0f, 26.0f, 28.0f, Shade(dark, 1.5f), dark, 0.08f);
		R->DrawRect(sx, hy + 1.0f, 27.0f, 4.0f, Shade(cloth, 1.1f), 0.2f);
		break;
	}
	case LK_VILLAIN:
	{
		R->DrawEllipse(sx, hy + 7.0f, 54.0f, 11.0f, Shade(dark, 1.4f), dark);
		R->DrawTriangle(sx, hy - 3.0f, 50.0f, 22.0f, Shade(dark, 1.6f), dark);
		if (faceCam)
		{
			// 안대
			R->DrawRect(sx, eyeY - 4.0f, 36.0f, 2.5f, dark, 0.5f);
			R->DrawEllipse(sx + 8.5f * flip, eyeY, 11.0f, 10.0f, dark);
		}
		break;
	}
	case LK_OFFICIAL:
	{
		Col cap(0.22f, 0.28f, 0.48f);
		R->DrawEllipse(sx, hy + 5.0f, 42.0f, 14.0f, Shade(cap, 1.12f), Shade(cap, 0.78f));
		R->DrawRect(sx, hy + 5.0f, 40.0f, 3.5f, Col(0.95f, 0.82f, 0.42f), 0.5f);
		break;
	}
	case LK_GUARD:
	{
		R->DrawEllipse(sx, hy + 5.0f, 44.0f, 26.0f, Col(0.86f, 0.88f, 0.90f), Col(0.48f, 0.50f, 0.55f), 0.10f);
		R->DrawEllipse(sx, hy + 14.0f, 50.0f, 8.0f, Col(0.72f, 0.74f, 0.78f), Col(0.50f, 0.52f, 0.56f));
		break;
	}
	case LK_CLOAK:
	{
		Col hood(0.22f, 0.22f, 0.27f);
		R->DrawEllipse(sx, hy + 7.0f, 46.0f, 24.0f, Shade(hood, 1.2f), Shade(hood, 0.8f));
		break;
	}
	default:
		break;
	}
}

static void DrawAgent(const Agent& a, float sx, float sy)
{
	float faceY = a.faceY;
	float bob = DrawPerson(sx, sy, a.cloth, a.phase, a.walking, a.faceX, faceY, a.flipX, false);

	float flip = (a.flipX < -0.05f) ? -1.0f : 1.0f;
	bool faceCam = (a.faceX + faceY) * 0.5f > -0.35f;
	float top = sy - bob - 58.0f;
	DrawLook(a.look, sx, top, flip, a.cloth, faceCam, sy - bob - 42.0f);

	float pdx = g_PlayerX - a.x, pdy = g_PlayerY - a.y;
	float pd2 = pdx * pdx + pdy * pdy;

	// 튜토리얼 목표 표시 — 미라
	if (g_Quest == Q_TALK && g_Sim.GetNpcDef(a.npc).id == "mira")
	{
		float b = fabsf(sinf(g_Time * 3.0f)) * 6.0f;
		DrawDownTriangle(sx, top - 30.0f - b, 16.0f, 14.0f, Col(1.0f, 0.86f, 0.42f));
		return;
	}

	// 관계 단계 표시 (명세 16.3) — 가까이 있을 때만. 중립은 흐리게
	if (pd2 < 12.0f * 12.0f)
	{
		Sim::Band band = g_Sim.GetBand(a.npc);
		Col c = BandColor(band);
		float alpha = (band == Sim::BAND_NEUTRAL) ? 0.45f : 0.95f;
		if (band == Sim::BAND_HOSTILE) alpha = 0.70f + sinf(g_Time * 6.0f) * 0.25f;
		R->DrawDiamond(sx, top - 22.0f, 13.0f, 13.0f, Fade(Shade(c, 1.1f), alpha), Fade(Shade(c, 0.8f), alpha));
	}
}

static void DrawEntity(const Entity& e, float sx, float sy)
{
	switch (e.type)
	{
	case E_TREE:     DrawTree(e, sx, sy); break;
	case E_PINE:     DrawPine(e, sx, sy); break;
	case E_BUSH:     DrawBush(e, sx, sy); break;
	case E_ROCK:     DrawRock(sx, sy); break;
	case E_HOUSE:    DrawHouse(e, sx, sy); DrawBuildingExtra(e, sx, sy); break;
	case E_WELL:     DrawWell(sx, sy); break;
	case E_CRATE:    DrawCrate(sx, sy); break;
	case E_BARREL:   DrawBarrel(sx, sy); break;
	case E_FENCE:    DrawFence(sx, sy); break;
	case E_VINE:     DrawVine(e, sx, sy); break;
	case E_REED:     DrawReed(e, sx, sy); break;
	case E_POT:      DrawPot(sx, sy); break;
	case E_HERB:     DrawHerb(sx, sy); break;
	case E_MONSTER:  DrawMonster(e, sx, sy); break;
	case E_BEAST:    DrawBeast(sx, sy); break;
	case E_TENT:     DrawTent(e, sx, sy); break;
	case E_WAGON:    DrawWagon(e, sx, sy); break;
	case E_STALL:    DrawStall(e, sx, sy); break;
	case E_CAMPFIRE: DrawCampfire(sx, sy); break;
	case E_NPC:      DrawAgent(g_Agents[e.data], sx, sy); break;
	default: break;
	}
}

// ---- 스프라이트 캐릭터 ------------------------------------------------------

static void DrawPlayerSprite(float sx, float sy)
{
	bool flip = false;
	int dir = SpriteAtlas::SpriteDirFromWorld(g_FaceX, g_FaceY, &flip);
	R->DrawShadow(sx, sy, 32.0f, 14.0f, 0.34f, 0.22f);

	const SpriteFrame* f;
	if (g_GratAnimT >= 0.0f)
	{
		f = &g_Spr.AnimFrameAt(g_AnRinoGrat, dir, g_GratAnimT);
	}
	else if (g_Walking)
	{
		// 걷기 위상(라디안)을 그대로 프레임으로. 발이 땅을 미끄러지지 않는다
		const SpriteAnim& a = g_Spr.Anim(g_AnRinoWalk);
		float c = g_WalkPhase / (2.0f * PI);
		int k = (int)floorf((c - floorf(c)) * a.frames);
		f = &g_Spr.AnimFrameIndex(g_AnRinoWalk, dir, k);
	}
	else
	{
		f = &g_Spr.AnimFrameAt(g_AnRinoIdle, dir, g_Time);
	}
	DrawSpr(*f, sx, sy, RINO_SPRITE_SCALE, flip, Col(1.0f, 1.0f, 1.0f));
}

static void DrawTartuSprite(float sx, float sy)
{
	// 리노가 전설 식재료를 먹을수록 자란다 (명세 13장 tartu_size 1~4)
	int size = g_SimOk ? g_Sim.GetVar("tartu_size") : 1;
	if (size < 1) size = 1;
	float k = 1.15f + (size - 1) * 0.35f;

	bool flip = false;
	int dir = SpriteAtlas::SpriteDirFromWorld(g_TartuDirX, g_TartuDirY, &flip);
	R->DrawShadow(sx, sy, 24.0f * k, 10.0f * k, 0.30f, 0.24f);

	int anim = g_AnTartuIdle;
	float t = g_TartuT;
	if (g_TartuMoving)
	{
		anim = g_AnTartuWalk;
	}
	else if (g_TartuIdleT > TARTU_SNIFF_AFTER)
	{
		float c = fmodf(g_TartuIdleT - TARTU_SNIFF_AFTER, TARTU_SNIFF_LEN * 2.0f);
		if (c < TARTU_SNIFF_LEN)
		{
			anim = g_AnTartuSniff;
			t = c;
		}
	}
	DrawSpr(g_Spr.AnimFrameAt(anim, dir, t), sx, sy, k, flip, Col(1.0f, 1.0f, 1.0f));
}

static void DrawCatchFx(const CatchFx& fx, float sx, float sy)
{
	float len = g_Spr.AnimDuration(g_AnTomCaught);
	float u = fx.t / len;
	if (u > 1.0f) u = 1.0f;

	// 금빛 후광 + 위로 흩어지는 빛 알갱이. 마지막 프레임 뒤에는 빛만 잠깐 남는다
	R->DrawEllipse(sx, sy - 18.0f - u * 8.0f, 30.0f + u * 30.0f, 26.0f + u * 26.0f,
		Fade(Col(1.0f, 0.88f, 0.55f), 0.30f * (1.0f - fabsf(u * 2.0f - 1.0f)) + 0.05f));
	for (int i = 0; i < 6; ++i)
	{
		float a = i * 1.047f + fx.t * 2.0f;
		float r = 8.0f + u * 18.0f;
		float al = (u > 0.3f) ? (1.0f - u) * 0.9f : u * 3.0f * 0.9f;
		R->DrawEllipse(sx + cosf(a) * r, sy - 20.0f - u * 34.0f - (i % 3) * 5.0f + sinf(a) * r * 0.4f,
			4.0f, 4.0f, Fade(Col(1.0f, 0.95f, 0.75f), al));
	}
	if (fx.t <= len)
		DrawTomatomaSprite(g_AnTomCaught, fx.t, fx.dirX, fx.dirY, sx, sy, 1.0f - u);
}

// ---- 깊이 정렬 ----------------------------------------------------------

struct DrawItem
{
	float depth;
	int   index;    // -1 플레이어, -2 타르투, -3 - k 포획 연출 k
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

static const wchar_t* const SEASON_NAMES[4]  = { L"여름", L"가을", L"겨울", L"봄" };
static const wchar_t* const SEGMENT_NAMES[4] = { L"새벽", L"낮", L"저녁", L"밤" };

static void ObjectiveText(wchar_t* out, int cap)
{
	switch (g_Quest)
	{
	case Q_TALK:
		swprintf_s(out, cap, L"마을에서 미라를 찾아가자 (머리 위 금색 표시)");
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

static void WorldToScreen(float wx, float wy, float* sx, float* sy)
{
	IsoToScreen(wx, wy, sx, sy);
	*sx += g_CamOffX;
	*sy += g_CamOffY;
}

static void DrawBubble(float sx, float sy, const wchar_t* text, float alpha, const Col& c)
{
	int tw = R->LabelWidth(text, FONT_SMALL);
	int th = R->LabelHeight(FONT_SMALL);
	DrawPanel(sx, sy, tw + 22.0f, th + 12.0f, 0.80f * alpha);
	DrawDownTriangle(sx, sy + (th + 12.0f) * 0.5f + 4.0f, 12.0f, 8.0f,
		Fade(Col(0.09f, 0.07f, 0.07f), 0.80f * alpha));
	R->DrawLabelCentered(sx, sy - th * 0.5f - 1.0f, text, Fade(c, alpha), FONT_SMALL);
}

// 이름표와 말풍선. 월드 위치에 붙지만 사후처리를 받으면 안 되므로 UI 패스에서
static void DrawNpcOverlays()
{
	for (size_t i = 0; i < g_Agents.size(); ++i)
	{
		const Agent& a = g_Agents[i];
		if (!a.present || a.indoor) continue;

		float sx, sy;
		WorldToScreen(a.x, a.y, &sx, &sy);
		if (sx < -100.0f || sx > SCR_W + 100.0f || sy < -60.0f || sy > SCR_H + 160.0f) continue;

		float dx = g_PlayerX - a.x, dy = g_PlayerY - a.y;
		float d2 = dx * dx + dy * dy;

		if (a.bubbleT > 0.0f)
		{
			float alpha = a.bubbleT < 0.4f ? a.bubbleT / 0.4f : 1.0f;
			Col c = (g_Sim.GetBand(a.npc) == Sim::BAND_HOSTILE) ? Col(1.0f, 0.70f, 0.62f) : UI_TEXT;
			DrawBubble(sx, sy - 128.0f, a.bubble, alpha, c);
		}
		else if (d2 < 5.0f * 5.0f)
		{
			R->DrawLabelCentered(sx, sy - 112.0f, a.name, Fade(UI_TEXT, 0.9f), FONT_SMALL);
		}
	}

	if (g_PlayerBubbleT > 0.0f)
	{
		float sx, sy;
		WorldToScreen(g_PlayerX, g_PlayerY, &sx, &sy);
		float alpha = g_PlayerBubbleT < 0.4f ? g_PlayerBubbleT / 0.4f : 1.0f;
		DrawBubble(sx, sy - 136.0f, g_PlayerBubble, alpha, UI_GOLD);
	}
}

// F5 — 이 맵 주민의 관계·오늘 행동. 테스트 중 한눈에 보려는 표
static void DrawNpcPanel()
{
	if (!g_ShowNpcPanel || !g_SimOk) return;

	const int rows = 17;
	const float rowH = (float)R->LabelHeight(FONT_SMALL) + 5.0f;
	const float colW = 330.0f;
	const float x0 = 30.0f;
	const float y0 = 96.0f;

	int cols = ((int)g_Agents.size() + rows - 1) / rows;
	if (cols < 1) cols = 1;
	float w = colW * cols + 20.0f;
	float h = rowH * (rows + 2) + 16.0f;
	DrawPanel(x0 + w * 0.5f, y0 + h * 0.5f, w, h, 0.84f);

	wchar_t buf[192];
	swprintf_s(buf, 192,
		L"%d일차 %ls %d일 · %ls      번영 %d  불만 %d  치안 %d  검은 식탁 %d      명성 %d",
		g_Sim.GetDay(), SEASON_NAMES[g_Sim.GetSeason()], g_Sim.GetDayOfSeason(),
		SEGMENT_NAMES[g_Sim.GetSegment()],
		g_Sim.GetRegionValue(g_HomeRegion, Sim::RF_PROSPERITY),
		g_Sim.GetRegionValue(g_HomeRegion, Sim::RF_UNREST),
		g_Sim.GetRegionValue(g_HomeRegion, Sim::RF_SECURITY),
		g_Sim.GetRegionValue(g_HomeRegion, Sim::RF_BLACK),
		g_Sim.GetVar("fame"));
	R->DrawLabel(x0 + 12.0f, y0 + 8.0f, buf, UI_GOLD, FONT_SMALL);

	for (size_t i = 0; i < g_Agents.size(); ++i)
	{
		const Agent& a = g_Agents[i];
		int c = (int)i / rows, r = (int)i % rows;
		float x = x0 + 12.0f + c * colW;
		float y = y0 + 8.0f + rowH * (r + 1.4f);

		Sim::Band band = g_Sim.GetBand(a.npc);
		const Sim::ActionDef* act = g_Sim.GetCurrentAction(a.npc);
		const Sim::NpcState& st = g_Sim.GetNpcState(a.npc);
		const Sim::Traits& t = g_Sim.GetNpcDef(a.npc).traits;

		wchar_t actName[32], fac[24];
		actName[0] = fac[0] = 0;
		if (act) Widen(act->name, actName, 32);
		if (st.factionIdx >= 0) Widen(g_Sim.GetData().factions[st.factionIdx].name, fac, 24);

		swprintf_s(buf, 192, L"%-5ls %+d%+d%+d  %ls %4d  %ls%ls",
			a.name, t.greed, t.principle, t.bold, BandName(band), g_Sim.GetEffAffinity(a.npc),
			actName[0] ? actName : L"-", a.present ? L"" : L" (부재)");
		Col col = a.present ? BandColor(band) : UI_DIM;
		R->DrawLabel(x, y, buf, col, FONT_SMALL);
	}

	R->DrawLabel(x0 + 12.0f, y0 + h - rowH - 6.0f,
		L"성향 = 욕망 규범 기질     F6 다음 날   F7 상태 덤프(콘솔)   F8 다음 구간",
		UI_DIM, FONT_SMALL);
}

static void DrawUI()
{
	wchar_t buf[192];
	const float W = (float)SCR_W;
	const float H = (float)SCR_H;

	DrawNpcOverlays();

	// ---- 목표 표시줄 -------------------------------------------------------
	ObjectiveText(buf, 192);
	int tw = R->LabelWidth(buf, FONT_BODY);
	DrawPanel(34.0f + tw * 0.5f, 40.0f, tw + 44.0f, 42.0f, 0.66f);
	R->DrawRect(24.0f, 40.0f, 4.0f, 26.0f, UI_GOLD, 0.5f);
	R->DrawLabel(36.0f, 30.0f, buf, UI_TEXT, FONT_BODY);

	// ---- 시간 — 월드 시계와 플레이 시간 --------------------------------------
	if (g_SimOk)
	{
		swprintf_s(buf, 192, L"%ls %d일 · %ls     %02d:%02d",
			SEASON_NAMES[g_Sim.GetSeason()], g_Sim.GetDayOfSeason(),
			SEGMENT_NAMES[g_Sim.GetSegment()],
			(int)(g_Time / 60.0f), (int)fmodf(g_Time, 60.0f));
	}
	else
	{
		swprintf_s(buf, 192, L"%02d:%02d", (int)(g_Time / 60.0f), (int)fmodf(g_Time, 60.0f));
	}
	int twt = R->LabelWidth(buf, FONT_SMALL);
	DrawPanel(W - 34.0f - twt * 0.5f, 40.0f, twt + 32.0f, 34.0f, 0.55f);
	R->DrawLabel(W - 34.0f - twt, 31.0f, buf, UI_DIM, FONT_SMALL);

	// ---- 월드 알림 ---------------------------------------------------------
	if (g_NoticeT > 0.0f)
	{
		float a = g_NoticeT < 0.6f ? g_NoticeT / 0.6f : 1.0f;
		int nw = R->LabelWidth(g_NoticeText, FONT_SMALL);
		DrawPanel(W - 34.0f - nw * 0.5f, 132.0f, nw + 36.0f, 36.0f, 0.70f * a);
		R->DrawRect(W - 50.0f - nw, 132.0f, 3.0f, 20.0f, Fade(UI_GOLD, a), 0.5f);
		R->DrawLabel(W - 34.0f - nw, 122.0f, g_NoticeText, Fade(UI_TEXT, a), FONT_SMALL);
	}

	// ---- 조작 안내 --------------------------------------------------------
	if (g_Time < 20.0f && g_Quest == Q_TALK)
	{
		float a = (g_Time > 17.0f) ? (20.0f - g_Time) / 3.0f : 1.0f;
		DrawPanel(170.0f, H - 150.0f, 276.0f, 176.0f, 0.60f * a);
		R->DrawLabel(52.0f, H - 226.0f, L"W A S D    이동", Fade(UI_TEXT, a), FONT_SMALL);
		R->DrawLabel(52.0f, H - 198.0f, L"E              상호작용", Fade(UI_TEXT, a), FONT_SMALL);
		R->DrawLabel(52.0f, H - 170.0f, L"G              감사 의례 (포획 직후)", Fade(UI_TEXT, a), FONT_SMALL);
		R->DrawLabel(52.0f, H - 142.0f, L"F5            주민 현황", Fade(UI_TEXT, a), FONT_SMALL);
		R->DrawLabel(52.0f, H - 114.0f, L"F6 / F8      다음 날 / 다음 구간", Fade(UI_TEXT, a), FONT_SMALL);
		R->DrawLabel(52.0f, H - 86.0f,  L"R              다시 시작", Fade(UI_TEXT, a), FONT_SMALL);
	}

	// ---- 상호작용 안내 ----------------------------------------------------
	if (!g_DlgOpen)
	{
		const wchar_t* label = InteractLabel(FindInteractable());
		float ly = H - 190.0f;
		if (label != 0)
		{
			int lw = R->LabelWidth(label, FONT_BODY);
			float pulse = 0.86f + sinf(g_Time * 4.0f) * 0.10f;
			DrawPanel(W * 0.5f, ly, lw + 44.0f, 40.0f, 0.72f);
			R->DrawLabelCentered(W * 0.5f, ly - 12.0f, label,
				Fade(UI_GOLD, pulse), FONT_BODY);
			ly -= 48.0f;
		}
		if (g_GratitudeT > 0.0f)
		{
			const wchar_t* g = L"[G]  감사 의례";
			int gw = R->LabelWidth(g, FONT_BODY);
			float a = g_GratitudeT < 0.6f ? g_GratitudeT / 0.6f : 1.0f;
			DrawPanel(W * 0.5f, ly, gw + 44.0f, 40.0f, 0.72f * a);
			R->DrawLabelCentered(W * 0.5f, ly - 12.0f, g, Fade(Col(0.80f, 0.92f, 1.0f), a), FONT_BODY);
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

		if (!g_Dlg.who.empty())
		{
			R->DrawLabel(84.0f, ty - 32.0f, g_Dlg.who.c_str(), UI_GOLD, FONT_BODY);
		}

		if (!g_Dlg.l1.empty()) { R->DrawLabel(84.0f, ty, g_Dlg.l1.c_str(), UI_TEXT, FONT_BODY); ty += 30.0f; }
		if (!g_Dlg.l2.empty()) { R->DrawLabel(84.0f, ty, g_Dlg.l2.c_str(), UI_TEXT, FONT_BODY); ty += 30.0f; }
		if (!g_Dlg.l3.empty())
		{
			// NPC 대화의 셋째 줄은 테스트 정보라 작고 흐리게
			bool info = !g_Dlg.who.empty() && g_Dlg.l3[0] == L'[';
			R->DrawLabel(84.0f, ty + (info ? 4.0f : 0.0f), g_Dlg.l3.c_str(),
				info ? UI_DIM : UI_TEXT, info ? FONT_SMALL : FONT_BODY);
		}

		R->DrawLabel(W - 200.0f, H - 46.0f, L"[E]  계속", UI_DIM, FONT_SMALL);
	}

	// ---- 클리어 ------------------------------------------------------------
	if (g_Quest == Q_DONE && !g_DlgOpen && g_ClearAt >= 0.0f)
	{
		// 이 게임의 하이라이트는 먹는 순간이다 — 완성된 한 그릇을 크게 보여 준다
		if (g_SprOk && g_SpDishClose >= 0)
		{
			float since = g_Time - g_ClearAt;
			float in = since < 0.6f ? since / 0.6f : 1.0f;
			float bob = sinf(g_Time * 1.6f) * 3.0f;
			float dy = H * 0.5f - 238.0f + bob + (1.0f - in) * 24.0f;
			R->DrawEllipse(W * 0.5f, dy + 74.0f, 270.0f, 50.0f, Fade(Col(0.0f, 0.0f, 0.0f), 0.30f * in));
			DrawSpr(g_Spr.Frame(g_SpDishClose), W * 0.5f, dy, 1.6f, false, Fade(Col(1, 1, 1), in));
			for (int i = 0; i < 4; ++i)
			{
				float t = g_Time * 0.6f + i * 0.25f;
				float up = t - floorf(t);
				R->DrawEllipse(W * 0.5f - 40.0f + i * 26.0f + sinf(t * 4.0f + i) * 6.0f,
					dy - 30.0f - up * 70.0f, 18.0f + up * 20.0f, 16.0f + up * 18.0f,
					Fade(Col(1.0f, 0.98f, 0.94f), (1.0f - up) * 0.22f * in));
			}
		}
		DrawPanel(W * 0.5f, H * 0.5f - 50.0f, 460.0f, 168.0f, 0.86f);
		R->DrawLabelCentered(W * 0.5f, H * 0.5f - 116.0f, L"튜토리얼 완료",
			UI_GOLD, FONT_TITLE);

		swprintf_s(buf, 192, L"소요 시간   %02d:%02d",
			(int)(g_ClearAt / 60.0f), (int)fmodf(g_ClearAt, 60.0f));
		R->DrawLabelCentered(W * 0.5f, H * 0.5f - 66.0f, buf, UI_TEXT, FONT_BODY);

		swprintf_s(buf, 192, L"잡은 미식종 %d마리 · 채집 %d개",
			g_CatchCount, g_HerbCount);
		R->DrawLabelCentered(W * 0.5f, H * 0.5f - 36.0f, buf, UI_DIM, FONT_SMALL);

		R->DrawLabelCentered(W * 0.5f, H * 0.5f + 2.0f, L"[R]  다시 하기",
			UI_DIM, FONT_SMALL);
	}

	DrawNpcPanel();

	if (!g_SimOk && g_SimError[0])
	{
		int ew = R->LabelWidth(g_SimError, FONT_SMALL);
		DrawPanel(W * 0.5f, 96.0f, ew + 40.0f, 36.0f, 0.88f);
		R->DrawLabelCentered(W * 0.5f, 86.0f, g_SimError, Col(1.0f, 0.55f, 0.48f), FONT_SMALL);
	}
}

// ---- 하루 빛깔 ------------------------------------------------------------
//
// 구간이 바뀌는 게 눈에 보여야 일과 이동을 따라갈 수 있다. 사후처리 뒤에
// 반투명 판 한 장을 덮는다 (UI 는 그 위에 그린다).

struct TintKey
{
	float t;
	Col   c;
	float a;
};

static void DrawDayTint()
{
	if (!g_SimOk) return;

	static const TintKey keys[] =
	{
		{ 0.00f, Col(0.40f, 0.48f, 0.78f), 0.24f },   // 새벽
		{ 0.18f, Col(0.95f, 0.85f, 0.70f), 0.00f },
		{ 0.52f, Col(0.95f, 0.70f, 0.40f), 0.00f },   // 낮
		{ 0.66f, Col(0.95f, 0.52f, 0.24f), 0.14f },   // 저녁 노을
		{ 0.76f, Col(0.30f, 0.22f, 0.42f), 0.28f },
		{ 0.82f, Col(0.05f, 0.08f, 0.22f), 0.44f },   // 밤
		{ 1.00f, Col(0.10f, 0.14f, 0.34f), 0.36f },
	};
	const int n = sizeof(keys) / sizeof(keys[0]);

	float p = g_Sim.GetDayProgress();
	int k = 0;
	while (k + 1 < n - 1 && p > keys[k + 1].t) ++k;
	float span = keys[k + 1].t - keys[k].t;
	float f = (span > 0.0f) ? Clampf((p - keys[k].t) / span, 0.0f, 1.0f) : 0.0f;
	Col c = Mix(keys[k].c, keys[k + 1].c, f);
	float a = keys[k].a + (keys[k + 1].a - keys[k].a) * f;
	if (a < 0.005f) return;

	R->DrawRect(SCR_W * 0.5f, SCR_H * 0.5f, (float)SCR_W + 8.0f, (float)SCR_H + 8.0f, Fade(c, a));
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
	g_CamOffX = offX;
	g_CamOffY = offY;

	// 렌더러 상태는 씬끼리 공유된다. 레벨 1 의 대기 원근·나무 비침이 남아
	// 있으면 안 되므로 매 프레임 이 씬 값으로 덮는다
	R->SetFrameTime(g_Time);
	R->SetHaze(C_SKY, 0.0f, 1.0f);
	R->SetSeeThrough(false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

	// ---- 지면 — 곡선 지형 한 장 + 주변 장식 ---------------------------------
	R->DrawTerrain(g_Terrain, offX, offY, TILE_W, TILE_H);

	// 장식은 플레이어 주변 창만 돈다. 1280x720 에서 보이는 범위는 대각선
	// 방향으로 약 ±21 타일.
	const int VIEW = 26;
	int px = (int)g_PlayerX, py = (int)g_PlayerY;
	int x0 = px - VIEW < 0 ? 0 : px - VIEW;
	int y0 = py - VIEW < 0 ? 0 : py - VIEW;
	int x1 = px + VIEW >= MAP_W ? MAP_W - 1 : px + VIEW;
	int y1 = py + VIEW >= MAP_H ? MAP_H - 1 : py + VIEW;

	for (int y = y0; y <= y1; ++y)
	{
		for (int x = x0; x <= x1; ++x)
		{
			float sx, sy;
			IsoToScreen((float)x + 0.5f, (float)y + 0.5f, &sx, &sy);
			sx += offX;
			sy += offY;
			if (sx < -TILE_W || sx > SCR_W + TILE_W) continue;
			if (sy < -TILE_H || sy > SCR_H + TILE_H) continue;
			GroundDetail(x, y, offX, offY);
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

	if (TartuReady())
	{
		DrawItem td;
		td.depth = g_TartuX + g_TartuY;
		td.index = -2;
		order.push_back(td);
	}
	for (size_t k = 0; k < g_CatchFx.size(); ++k)
	{
		DrawItem fd;
		fd.depth = g_CatchFx[k].x + g_CatchFx[k].y + 0.01f;
		fd.index = -3 - (int)k;
		order.push_back(fd);
	}

	std::sort(order.begin(), order.end(), DepthLess);

	for (size_t i = 0; i < order.size(); ++i)
	{
		if (order[i].index == -1)
		{
			float sx, sy;
			IsoToScreen(g_PlayerX, g_PlayerY, &sx, &sy);
			if (g_SprOk)
				DrawPlayerSprite(sx + offX, sy + offY);
			else
				DrawPerson(sx + offX, sy + offY, Col(0.34f, 0.41f, 0.52f),
					g_WalkPhase, g_Walking, g_FaceX, g_FaceY, g_FlipX, true);
		}
		else if (order[i].index == -2)
		{
			float sx, sy;
			IsoToScreen(g_TartuX, g_TartuY, &sx, &sy);
			DrawTartuSprite(sx + offX, sy + offY);
		}
		else if (order[i].index <= -3)
		{
			const CatchFx& fx = g_CatchFx[-3 - order[i].index];
			float sx, sy;
			IsoToScreen(fx.x, fx.y, &sx, &sy);
			DrawCatchFx(fx, sx + offX, sy + offY);
		}
		else
		{
			const Entity& e = g_Ents[order[i].index];
			float sx, sy;
			IsoToScreen(e.x, e.y, &sx, &sy);
			DrawEntity(e, sx + offX, sy + offY);
		}
	}
	PROFILE_GAUGE("tutorial.drawn_entities", (double)order.size());

	// 월드에만 사후처리를 건다. UI는 그 뒤에 얹어야 비네팅에 먹히지 않는다.
	R->EndFrame();

	DrawDayTint();
	DrawUI();
}

} // namespace Game
