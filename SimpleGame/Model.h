#pragma once

#include <vector>

#include "Renderer.h"

// -----------------------------------------------------------------------------
// Model data
//
// A model is a small skeleton plus a list of shape parts bolted onto its
// joints. Joints form a parent/child tree, so rotating a shoulder carries the
// forearm and the hand with it. That is what fixes the "limbs detached from the
// torso" look: previously a limb could only be translated, so it slid off the
// body instead of swinging from it.
//
// Models are built procedurally once and then serialised to Data/models.bin.
// On every later run the file is loaded straight back, so the build step is
// paid exactly once.
// -----------------------------------------------------------------------------

enum PartShape
{
	PART_RECT = 0,
	PART_ELLIPSE,
	PART_DIAMOND,
	PART_TRI
};

// Parts take their colour from the instance palette so one model can serve the
// player, twelve villagers and every monster variant.
enum ColorSlot
{
	CS_FIXED = -1,
	CS_CLOTH = 0,
	CS_SKIN,
	CS_HAIR,
	CS_TROUSERS,
	CS_SHOE,
	CS_ACCENT,
	CS_DARK,
	CS_COUNT
};

enum PartFlags
{
	PF_NONE      = 0,
	PF_FACE_ONLY = 1 << 0,   // 화면을 향할 때만
	PF_PLAYER    = 1 << 1,   // 플레이어 전용 — 셰프 모자, 앞치마
	PF_VILLAGER  = 1 << 2,   // 주민 전용 — 머리카락
	PF_EYE_OPEN  = 1 << 3,
	PF_EYE_SHUT  = 1 << 4,

	// 걸을 때 두 손이 몸통 앞뒤로 교차한다. 어느 손이 앞으로 나와 있는지에
	// 따라 그리기 순서가 달라져야 해서, 손 파트를 앞/뒤 양쪽에 하나씩 두고
	// 이 플래그로 한 쌍만 통과시킨다.
	PF_HANDS_NORMAL = 1 << 5,   // 뒤쪽 손이 뒤, 앞쪽 손이 앞
	PF_HANDS_SWAP   = 1 << 6    // 반대
};

struct ModelJoint
{
	int   parent;       // -1 = root
	float ox, oy;       // 부모 관절 기준 오프셋 (px, 회전 전)
};

struct ModelPart
{
	int   joint;
	int   shape;
	float ox, oy;           // 관절 기준 오프셋
	float w, h;
	float pivotX, pivotY;   // 로컬 피벗 (-0.5 .. +0.5)
	float radius;
	float rim;
	int   slot;             // ColorSlot, 또는 CS_FIXED
	float shadeTop;
	float shadeBot;
	Col   fixedTop;
	Col   fixedBot;
	int   order;            // 낮을수록 먼저(뒤에) 그린다
	int   flags;
};

struct Model
{
	std::vector<ModelJoint> joints;
	std::vector<ModelPart>  parts;
};

struct Palette
{
	Col c[CS_COUNT];
};

// 모델 인덱스
enum ModelId
{
	MODEL_HUMAN = 0,
	MODEL_TOMATOMA,
	MODEL_BOAR,
	MODEL_TREE,   // 활엽수
	MODEL_PINE,   // 침엽수
	MODEL_BUSH,
	MODEL_ROCK,
	MODEL_COUNT
};

// 사람 관절.
//
// 팔다리 뼈가 없다. 관절을 쪼개 인체를 흉내내면 프리미티브로는 반드시
// 대벌레가 되므로, 몸통 하나 + 떠 있는 손발로 간다. 움직임은 관절 회전이
// 아니라 몸 전체의 기울기, 스쿼시, 손발의 지연 오프셋으로 만든다.
enum HumanJoint
{
	HJ_ROOT = 0,
	HJ_BODY,
	HJ_HEAD,
	HJ_HAND_B,
	HJ_HAND_F,
	HJ_FOOT_B,
	HJ_FOOT_F,
	HJ_COUNT
};

// 루트가 발바닥에 있으므로 0. 호출부는 지면 좌표를 그대로 넘기면 된다.
extern const float HUMAN_ROOT_HEIGHT;

namespace Models
{
	// 캐시가 있으면 읽고, 없거나 형식이 바뀌었으면 새로 만들어 저장한다.
	void Load();

	const Model& Get(int modelId);

	// 그리기.
	//   angles   관절 수만큼, 라디안. NULL 이면 전부 0
	//   offsets  관절당 (x, y) 2개씩. NULL 이면 전부 0. 떠 있는 손발이 이걸 쓴다
	//   squash   1.0 기준. 1 보다 크면 길쭉, 작으면 납작해진다
	void Draw(Renderer* r, int modelId, float originX, float originY,
		const float* angles, const float* offsets, const Palette& pal,
		float scale, float squash, float flipX, int flagMask, float alphaMul);

	// Draw 와 같지만 joint 에 붙은 파트만 그린다. 손에 쥔 물건 위로 손을
	// 다시 얹을 때처럼, 모델의 일부만 다른 것보다 나중에 그려야 할 때 쓴다.
	void DrawJoint(Renderer* r, int modelId, float originX, float originY,
		const float* angles, const float* offsets, const Palette& pal,
		float scale, float squash, float flipX, int flagMask, float alphaMul, int joint);

	// 정지 포즈로 그리되, swayJoint 아래 파트는 GPU 에서
	// sin(time * freq + phase) * amplitude 만큼 그 관절을 축으로 흔들린다.
	// 결과를 한 번 기록해 두고 매 프레임 재생할 수 있게 하려는 것이다.
	void DrawSwaying(Renderer* r, int modelId, float originX, float originY,
		const Palette& pal, float scale, float alphaMul,
		int swayJoint, float phase, float amplitude, float freq);

	// 관절 하나의 스크린 위치와 누적 각(라디안). 인자는 Draw 와 같고 계산도
	// 같다. 손에 무언가를 쥐여 줄 때 쓴다. outAngle 은 NULL 이어도 된다.
	void JointPosition(int modelId, float originX, float originY,
		const float* angles, const float* offsets, float scale, float squash,
		float flipX, int joint, float* outX, float* outY, float* outAngle);

	// 사람 포즈 계산기.
	//   angles     HJ_COUNT 개
	//   offsets    HJ_COUNT * 2 개
	//   squash     출력
	//   poseFlags  출력. 호출부의 flagMask 에 OR 해서 넘긴다
	// 반환값은 수직 바운스(px).
	float HumanPose(float* angles, float* offsets, float* squash,
		int* poseFlags, float phase, bool walking, float attackT);

	Palette MakeHumanPalette(const Col& cloth, bool player);
}
