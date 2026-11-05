#include "stdafx.h"

#include "Actor.h"

// =============================================================================
// RenderContext
// =============================================================================

RenderContext::RenderContext()
	: renderer(0), tileW(1.0f), tileH(1.0f), heightScale(1.0f),
	offX(0.0f), offY(0.0f),
	cullL(0.0f), cullT(0.0f), cullR(0.0f), cullB(0.0f),
	time(0.0f)
{
}

void RenderContext::Project(float wx, float wy, float wz, float* px, float* py) const
{
	*px = (wx - wy) * (tileW * 0.5f);
	*py = (wx + wy) * (tileH * 0.5f) - wz * heightScale;
}

void RenderContext::WorldToScreen(float wx, float wy, float wz,
	float* sx, float* sy) const
{
	Project(wx, wy, wz, sx, sy);
	*sx += offX;
	*sy += offY;
}

// =============================================================================
// Actor
// =============================================================================

Actor::Actor()
	: m_X(0.0f), m_Y(0.0f), m_Z(0.0f),
	m_Parent(0),
	m_Active(true), m_Visible(true), m_Destroyed(false), m_FlushPending(false),
	m_Layer(LAYER_WORLD), m_DepthBias(0.0f),
	m_HasBounds(false), m_BoundL(0.0f), m_BoundR(0.0f), m_BoundU(0.0f), m_BoundD(0.0f),
	m_TreeDirty(true), m_TreeState(TREE_EMPTY),
	m_TreeL(0.0f), m_TreeT(0.0f), m_TreeR(0.0f), m_TreeB(0.0f)
{
}

Actor::~Actor()
{
	for (size_t i = 0; i < m_Children.size(); ++i) delete m_Children[i];
	for (size_t i = 0; i < m_Pending.size(); ++i)  delete m_Pending[i];
}

// ---- 계층 ---------------------------------------------------------------

void Actor::AddChild(Actor* child)
{
	if (child == 0 || child->m_Parent != 0) return;
	child->m_Parent = this;
	m_Pending.push_back(child);
	MarkFlushPending();
}

void Actor::Destroy()
{
	m_Destroyed = true;
	if (m_Parent) m_Parent->MarkFlushPending();
}

// ---- 트랜스폼 -----------------------------------------------------------
// 내가 움직이면 내 서브트리 경계(내 발 기준)는 그대로고, 부모 쪽 경계만
// 달라진다. 그래서 무효화는 부모부터 올라간다.

void Actor::SetPosition(float x, float y)
{
	m_X = x;
	m_Y = y;
	if (m_Parent) m_Parent->MarkTreeDirty();
}

void Actor::SetPosition(float x, float y, float z)
{
	m_X = x;
	m_Y = y;
	m_Z = z;
	if (m_Parent) m_Parent->MarkTreeDirty();
}

void Actor::SetX(float x)
{
	m_X = x;
	if (m_Parent) m_Parent->MarkTreeDirty();
}

void Actor::SetY(float y)
{
	m_Y = y;
	if (m_Parent) m_Parent->MarkTreeDirty();
}

void Actor::SetHeight(float z)
{
	m_Z = z;
	if (m_Parent) m_Parent->MarkTreeDirty();
}

void Actor::Translate(float dx, float dy)
{
	m_X += dx;
	m_Y += dy;
	if (m_Parent) m_Parent->MarkTreeDirty();
}

float Actor::GetWorldX() const
{
	return m_Parent ? m_Parent->GetWorldX() + m_X : m_X;
}

float Actor::GetWorldY() const
{
	return m_Parent ? m_Parent->GetWorldY() + m_Y : m_Y;
}

float Actor::GetWorldZ() const
{
	return m_Parent ? m_Parent->GetWorldZ() + m_Z : m_Z;
}

// ---- 그리기 / 경계 ------------------------------------------------------

void Actor::SetLayer(int layer)
{
	// 그룹 <-> 그리기 Actor 전환은 경계 규칙(무한 여부)을 바꾼다
	m_Layer = layer;
	MarkTreeDirty();
}

void Actor::SetBounds(float left, float right, float up, float down)
{
	m_HasBounds = true;
	m_BoundL = left;
	m_BoundR = right;
	m_BoundU = up;
	m_BoundD = down;
	MarkTreeDirty();
}

void Actor::MarkTreeDirty()
{
	// 더러운 노드의 조상은 항상 더럽다. 이미 더러운 곳에서 멈춰도 된다
	for (Actor* a = this; a != 0 && !a->m_TreeDirty; a = a->m_Parent)
		a->m_TreeDirty = true;
}

void Actor::MarkFlushPending()
{
	for (Actor* a = this; a != 0 && !a->m_FlushPending; a = a->m_Parent)
		a->m_FlushPending = true;
}

// 변경이 없는 서브트리는 들어가지 않는다. 매 프레임 트리 전체를 훑지 않기 위해서다
void Actor::FlushChildren()
{
	if (!m_FlushPending) return;
	m_FlushPending = false;

	bool changed = false;

	// 삭제 먼저. 파괴된 부모의 자식은 부모 소멸자가 같이 지운다
	size_t w = 0;
	for (size_t i = 0; i < m_Children.size(); ++i)
	{
		Actor* c = m_Children[i];
		if (c->m_Destroyed) { delete c; changed = true; }
		else m_Children[w++] = c;
	}
	m_Children.resize(w);

	for (size_t i = 0; i < m_Pending.size(); ++i)
	{
		if (m_Pending[i]->m_Destroyed) delete m_Pending[i];
		else m_Children.push_back(m_Pending[i]);
		changed = true;
	}
	m_Pending.clear();

	if (changed) MarkTreeDirty();

	// 방금 붙은 자식이 품고 있던 대기 목록까지 한 번에 반영한다
	for (size_t i = 0; i < m_Children.size(); ++i)
		m_Children[i]->FlushChildren();
}
