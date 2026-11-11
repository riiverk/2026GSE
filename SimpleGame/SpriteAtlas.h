#pragma once

#include <string>
#include <vector>

// -----------------------------------------------------------------------------
// 블렌더에서 구운 스프라이트 아틀라스 (Data/Sprites/sprites.txt + sprites.png).
//
// 데이터와 픽셀만 다룬다. GL 호출은 하지 않는다 — 텍스처 업로드는
// Renderer::SetSpriteAtlas 가 한다 (CLAUDE.md: GL 은 Renderer 안에서만).
//
// 좌표 규약
//   - 모든 크기·오프셋은 게임 1배 px. 아틀라스는 2~3배로 구웠지만 로더가
//     이미 나눠 두었다.
//   - (ox, oy) 는 잘라낸 사각형의 좌상단이 피벗(발 = 월드 위치)에서
//     얼마나 떨어져 있는지. 그리는 쪽은 발의 스크린 좌표에 더하기만 하면 된다.
//   - 방향은 화면 기준 5개 (s, se, e, ne, n). nw / w / sw 는 ne / e / se 를
//     좌우 반전해서 쓴다 (SpriteDirFromWorld).
// -----------------------------------------------------------------------------

struct SpriteFrame
{
	float u0, v0, u1, v1;   // 아틀라스 UV (v 는 위가 0)
	float w, h;             // 1배 px
	float ox, oy;           // 피벗 기준 좌상단 오프셋 (1배 px)
	float ax, ay;           // 앵커 (피벗 기준, 1배 px). 주민 몸은 머리, 모자는 모자 자리
	bool  hasAnchor;        // 겹쳐 그리는 부품을 프레임마다 따라 옮길 때만 있다
};

struct SpriteAnim
{
	std::string name;       // "rino.walk"
	int   first;            // m_Frames 시작 인덱스. [dir * frames + f]
	int   frames;
	int   dirs;             // 5 (방향 있음) 또는 1
	float fps;
	bool  loop;
};

enum SpriteDir
{
	SDIR_S = 0, SDIR_SE, SDIR_E, SDIR_NE, SDIR_N,
	SDIR_COUNT
};

class Renderer;
struct Col;

class SpriteAtlas
{
public:
	SpriteAtlas() : m_W(0), m_H(0) {}

	// txt 를 읽고 같은 폴더의 PNG 를 RGBA 로 디코드한다. 실패하면 false 와
	// 이유를 Error() 에 남긴다.
	bool Load(const char* txtPath);

	const unsigned char* Pixels() const { return m_Pixels.empty() ? 0 : &m_Pixels[0]; }
	int  Width() const { return m_W; }
	int  Height() const { return m_H; }
	// GPU 에 올린 뒤에는 CPU 사본이 필요 없다 (4096^2 RGBA = 64MB)
	void FreePixels() { std::vector<unsigned char>().swap(m_Pixels); }

	int FindAnim(const char* name) const;     // 없으면 -1
	int FindSprite(const char* name) const;   // 정적 스프라이트 ("house"), 없으면 -1

	const SpriteAnim&  Anim(int idx) const { return m_Anims[idx]; }
	const SpriteFrame& Frame(int idx) const { return m_Frames[idx]; }

	// 애니메이션 프레임. t 는 초 (loop 면 감고, 아니면 마지막에서 멈춘다)
	const SpriteFrame& AnimFrameAt(int anim, int dir5, float t) const;
	const SpriteFrame& AnimFrameIndex(int anim, int dir5, int frame) const;
	// 루프하지 않는 애니메이션의 길이 (초)
	float AnimDuration(int anim) const;

	const std::string& Error() const { return m_Error; }

	// 게임 월드 방향 (wx, wy) -> 5방향 인덱스 + 좌우 반전 여부
	static int SpriteDirFromWorld(float fx, float fy, bool* flip);

private:
	struct Named
	{
		std::string name;
		int frame;
	};

	std::vector<SpriteFrame>   m_Frames;
	std::vector<SpriteAnim>    m_Anims;
	std::vector<Named>         m_Statics;
	std::vector<unsigned char> m_Pixels;
	int         m_W, m_H;
	std::string m_Error;
};

// ---- 게임 전체가 쓰는 아틀라스 하나 ----
// Game::Init 이 읽어 GPU 에 올리고 SetReady 한다. Level1 은 읽기만 한다.
// 실패하면 Ready 가 false 라 각 씬이 SDF 그림으로 그린다.
SpriteAtlas& SharedSprites();
bool SharedSpritesReady();
void SetSharedSpritesReady(bool ready);

// 프레임 한 장. (sx, sy) = 발(피벗)의 스크린 좌표, scale 은 1배 기준 배율.
// flip 이면 피벗 기준 좌우 반전. 렌더 큐에 넣기만 한다 (GL 호출 없음)
void DrawSpriteFrame(Renderer* r, const SpriteFrame& f, float sx, float sy, float scale,
	bool flip, const Col& tint);
