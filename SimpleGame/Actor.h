#pragma once

#include <vector>

class Renderer;
class SceneGraph;

// -----------------------------------------------------------------------------
// 그리기 레이어
//
// 레이어가 깊이보다 우선한다. 지면은 깊이와 상관없이 항상 먼저, 오버레이는
// EndFrame 뒤(줌·블룸·비네팅이 꺼진 뒤)에 따로 그린다.
// -----------------------------------------------------------------------------
enum ActorLayer
{
	LAYER_NONE = -1,   // 그리지 않는다. 그룹 노드용
	LAYER_GROUND = 0,
	LAYER_WORLD,
	LAYER_OVERLAY,
	LAYER_COUNT
};

// -----------------------------------------------------------------------------
// 한 번의 Render 호출 동안 모든 Actor 가 공유하는 값
//
// 투영 상수(tileW, tileH, heightScale)는 SceneGraph 가 채운다. 호출부는
// 카메라(offX, offY)와 컬링 사각형, 시간만 넣는다.
// -----------------------------------------------------------------------------
struct RenderContext
{
	Renderer* renderer;

	float tileW;
	float tileH;
	float heightScale;
	float offX;
	float offY;

	// 컬링 사각형 (스크린 px, 줌 적용 전)
	float cullL;
	float cullT;
	float cullR;
	float cullB;

	float time;

	RenderContext();

	// 카메라 없는 아이소 투영. 부모 기준 오프셋을 스크린 오프셋으로 바꿀 때 쓴다
	void Project(float wx, float wy, float wz, float* px, float* py) const;
	void WorldToScreen(float wx, float wy, float wz, float* sx, float* sy) const;
};

// -----------------------------------------------------------------------------
// 프로파일 식별자. 클래스마다 한 번 SceneGraph::RegisterActorProfile 로 만들어
// 생성자에서 SetProfile 로 붙인다. 붙이면 그 클래스의 OnUpdate / OnRender
// 시간이 scene.update.actor.<이름> / scene.render.actor.<이름> 으로 쌓인다.
// -----------------------------------------------------------------------------
struct ActorProfile
{
	int updateId;
	int renderId;
	ActorProfile() : updateId(-1), renderId(-1) {}
};

// -----------------------------------------------------------------------------
// Actor
//
// 화면에 놓이는 모든 오브젝트의 기반이자 씬 그래프의 노드.
//
// 트랜스폼
//   위치는 부모 기준 월드 좌표(타일 단위), z 는 지면에서 뜬 높이. 부모가
//   움직이면 자식이 따라온다. 위치는 반드시 setter 로 바꾼다 — 경계 캐시를
//   무효화해야 하기 때문이다.
//
// 경계 (컬링)
//   SetBounds 는 자기 그림이 발 위치 기준으로 차지하는 스크린 범위(px).
//   씬 그래프는 여기에 자손 전체를 합친 "서브트리 경계"를 노드마다 캐시한다.
//   서브트리 경계가 화면 밖이면 자손을 하나도 방문하지 않고 통째로 버린다.
//   이것이 공간별로 묶은 그룹 노드(셀, 청크)가 최적화가 되는 이유다.
//
//   경계는 노드 자신의 발 위치 기준이므로, 노드가 움직여도 자기 캐시는
//   그대로고 조상만 다시 계산하면 된다. 정적인 셀은 한 번 계산하면 끝이다.
//
//   그리는 Actor 가 경계를 설정하지 않으면 "무한" 으로 취급해 절대 컬링되지
//   않고, 조상의 서브트리 경계도 무한이 된다. 그룹 노드(LAYER_NONE)는 자기
//   경계 없이 자식 경계만 합친다.
//
// 수명
//   소유권은 부모. AddChild 와 Destroy 는 SceneGraph::Flush 때 반영된다.
//   순회 도중 몬스터가 죽고 드롭이 생겨도 자식 목록이 흔들리지 않게 하기
//   위해서다. 그래서 Destroy 된 Actor 도 그 프레임 동안은 목록에 남아 있고,
//   순회하는 쪽은 IsDestroyed() 로 걸러야 한다.
// -----------------------------------------------------------------------------
class Actor
{
public:
	Actor();
	virtual ~Actor();

	// ---- 계층 -----------------------------------------------------------
	void   AddChild(Actor* child);
	void   Destroy();
	bool   IsDestroyed() const { return m_Destroyed; }

	Actor* GetParent() const { return m_Parent; }
	const std::vector<Actor*>& GetChildren() const { return m_Children; }

	// ---- 트랜스폼 -------------------------------------------------------
	void  SetPosition(float x, float y);
	void  SetPosition(float x, float y, float z);
	void  SetX(float x);
	void  SetY(float y);
	void  SetHeight(float z);
	void  Translate(float dx, float dy);

	float GetX() const { return m_X; }
	float GetY() const { return m_Y; }
	float GetZ() const { return m_Z; }

	float GetWorldX() const;
	float GetWorldY() const;
	float GetWorldZ() const;

	// ---- 상태 -----------------------------------------------------------
	// 비활성이면 자신과 자손 전체의 Update 가, 숨김이면 Render 가 빠진다
	void SetActive(bool active) { m_Active = active; }
	void SetVisible(bool visible) { m_Visible = visible; }
	bool IsActive() const { return m_Active; }
	bool IsVisible() const { return m_Visible; }

	// ---- 그리기 순서 ----------------------------------------------------
	void  SetLayer(int layer);
	int   GetLayer() const { return m_Layer; }
	void  SetDepthBias(float bias) { m_DepthBias = bias; }

	// painter 정렬 키. 쿼터뷰에서 카메라와의 거리는 (x + y) 축이다
	float GetDepth() const { return GetWorldX() + GetWorldY() + m_DepthBias; }

	// ---- 경계 -----------------------------------------------------------
	void SetBounds(float left, float right, float up, float down);

	// ---- 프로파일 -------------------------------------------------------
	void SetProfile(const ActorProfile& p) { m_Profile = p; }

protected:
	virtual void OnUpdate(float dt) {}

	// sx, sy 는 발 위치의 스크린 좌표
	virtual void OnRender(const RenderContext& rc, float sx, float sy) {}

private:
	friend class SceneGraph;

	enum TreeState
	{
		TREE_EMPTY = 0,   // 그릴 것이 없다
		TREE_FINITE,
		TREE_INFINITE     // 경계 없는 그리기 Actor 가 섞여 있다
	};

	void MarkTreeDirty();
	void MarkFlushPending();
	void FlushChildren();

	float m_X;
	float m_Y;
	float m_Z;

	Actor*              m_Parent;
	std::vector<Actor*> m_Children;
	std::vector<Actor*> m_Pending;

	bool  m_Active;
	bool  m_Visible;
	bool  m_Destroyed;
	bool  m_FlushPending;   // 자신이나 자손에게 반영할 추가/삭제가 있다

	int   m_Layer;
	float m_DepthBias;

	// 자기 경계. 발 기준 px, 왼쪽/위는 음수 방향
	bool  m_HasBounds;
	float m_BoundL, m_BoundR, m_BoundU, m_BoundD;

	// 서브트리 경계 캐시. 발 기준 px 사각형 (l, t) - (r, b)
	bool  m_TreeDirty;
	int   m_TreeState;
	float m_TreeL, m_TreeT, m_TreeR, m_TreeB;

	ActorProfile m_Profile;

	Actor(const Actor&);
	Actor& operator=(const Actor&);
};
