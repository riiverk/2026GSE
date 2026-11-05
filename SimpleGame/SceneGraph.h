#pragma once

#include <vector>

#include "Actor.h"

// -----------------------------------------------------------------------------
// SceneGraph
//
// 루트 Actor 하나를 들고 트리 전체의 갱신, 컬링, 그리기를 맡는다.
//
//  Update  깊이 우선, 자식 목록 순서대로 OnUpdate. 비활성 노드는 서브트리째
//          건너뛴다. 끝나면 Flush.
//
//  Render  1) 더러워진 경로만 따라 서브트리 경계를 다시 계산한다.
//          2) 루트부터 내려가며 서브트리 경계가 화면 밖인 노드는 자손까지
//             통째로 버린다 (계층 컬링). 스크린 좌표는 부모 좌표에 자식의
//             로컬 오프셋을 더해 내려보내므로 노드마다 조상 체인을 다시
//             타지 않는다.
//          3) 살아남은 Actor 를 (레이어, 깊이) 로 정렬해 OnRender.
//             그리는 순서는 트리 구조와 무관하다 — 쿼터뷰에서는 부모 자식
//             관계보다 화면상 앞뒤가 우선이기 때문이다.
//
// 컬링이 효과를 보려면 정적인 오브젝트를 공간 단위 그룹(셀)으로 묶어야 한다.
// 평평하게 한 그룹에 몰아넣으면 그룹 경계가 맵 전체가 되어 아무것도 못 버린다.
// -----------------------------------------------------------------------------
class SceneGraph
{
public:
	struct Stats
	{
		int visited;        // Render 에서 방문한 노드
		int culledTrees;    // 서브트리째 버린 노드
		int culledSelf;     // 자기만 버린 노드
		int drawn;          // OnRender 호출 수
	};

	SceneGraph();
	~SceneGraph();

	// 클래스 이름 하나로 update / render 스코프 두 개를 등록한다.
	// 예) "monster" -> scene.update.actor.monster, scene.render.actor.monster
	static ActorProfile RegisterActorProfile(const char* className);

	Actor* GetRoot() { return &m_Root; }

	void SetProjection(float tileW, float tileH, float heightScale);

	// 그리지 않는 묶음 노드를 만들어 parent(NULL 이면 루트) 아래에 붙인다
	Actor* CreateGroup(Actor* parent = 0);

	// 루트 아래를 전부 즉시 삭제한다
	void Clear();

	void Update(float dt);

	// 대기 중인 추가/삭제를 반영한다. Update 가 알아서 부르지만, 씬을
	// 새로 짠 직후처럼 바로 목록이 필요할 때 직접 부른다
	void Flush();

	// rc 의 투영 상수는 SetProjection 값으로 덮어쓴다
	void Render(const RenderContext& rc, int firstLayer, int lastLayer);

	const Stats& GetStats() const { return m_Stats; }
	int  CountActors() const;

private:
	struct DrawEntry
	{
		int    layer;
		float  depth;
		float  sx, sy;
		Actor* actor;
	};

	static bool DrawLess(const DrawEntry& a, const DrawEntry& b);

	void UpdateNode(Actor* node, float dt);
	void RefreshBounds(Actor* node);
	void CollectNode(Actor* node, float sx, float sy, float wx, float wy,
		int firstLayer, int lastLayer);
	static int CountNode(const Actor* node);
	static void DirtyAll(Actor* node);

	Actor                  m_Root;
	RenderContext          m_Ctx;
	std::vector<DrawEntry> m_DrawList;
	Stats                  m_Stats;
};
