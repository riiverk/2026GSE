#include "stdafx.h"

#include <algorithm>

#include "SceneGraph.h"
#include "Profiler.h"

#include <string>

SceneGraph::SceneGraph()
{
	m_Root.SetLayer(LAYER_NONE);
	m_Stats.visited = m_Stats.culledTrees = m_Stats.culledSelf = m_Stats.drawn = 0;
}

SceneGraph::~SceneGraph()
{
	Clear();
}

void SceneGraph::SetProjection(float tileW, float tileH, float heightScale)
{
	m_Ctx.tileW = tileW;
	m_Ctx.tileH = tileH;
	m_Ctx.heightScale = heightScale;

	// 투영이 바뀌면 모든 오프셋 환산이 달라진다
	DirtyAll(&m_Root);
}

void SceneGraph::DirtyAll(Actor* node)
{
	node->m_TreeDirty = true;
	for (size_t i = 0; i < node->m_Children.size(); ++i)
		DirtyAll(node->m_Children[i]);
}

ActorProfile SceneGraph::RegisterActorProfile(const char* className)
{
	ActorProfile p;
	std::string u = std::string("scene.update.actor.") + className;
	std::string r = std::string("scene.render.actor.") + className;
	p.updateId = Profiler::RegisterScope(u.c_str());
	p.renderId = Profiler::RegisterScope(r.c_str());
	return p;
}

Actor* SceneGraph::CreateGroup(Actor* parent)
{
	Actor* g = new Actor();
	g->SetLayer(LAYER_NONE);
	(parent ? parent : &m_Root)->AddChild(g);
	return g;
}

void SceneGraph::Clear()
{
	for (size_t i = 0; i < m_Root.m_Children.size(); ++i) delete m_Root.m_Children[i];
	for (size_t i = 0; i < m_Root.m_Pending.size(); ++i)  delete m_Root.m_Pending[i];
	m_Root.m_Children.clear();
	m_Root.m_Pending.clear();
	m_Root.m_FlushPending = false;
	m_Root.m_TreeDirty = true;
	m_DrawList.clear();
}

// ---- 갱신 ---------------------------------------------------------------

void SceneGraph::Update(float dt)
{
	{
		PROFILE_SCOPE("scene.update");
		UpdateNode(&m_Root, dt);
	}
	Flush();

	// 노드 수는 트리를 한 번 도는 값이라 싸다. 수가 늘면 모든 순회 비용이
	// 같이 느는 선행 지표라 매 프레임 남긴다.
	PROFILE_COUNT("scene.nodes", CountActors());
}

void SceneGraph::Flush()
{
	PROFILE_SCOPE("scene.flush");
	m_Root.FlushChildren();
}

void SceneGraph::UpdateNode(Actor* node, float dt)
{
	if (node->m_Destroyed || !node->m_Active) return;

	if (node->m_Profile.updateId >= 0)
	{
		Profiler::ScopeTimer t(node->m_Profile.updateId);
		node->OnUpdate(dt);
	}
	else
	{
		node->OnUpdate(dt);
	}

	// 순회 중의 AddChild 는 대기 목록으로만 가므로 m_Children 은 안 바뀐다
	for (size_t i = 0; i < node->m_Children.size(); ++i)
		UpdateNode(node->m_Children[i], dt);
}

// ---- 경계 ---------------------------------------------------------------

void SceneGraph::RefreshBounds(Actor* node)
{
	if (!node->m_TreeDirty) return;

	int   state = Actor::TREE_EMPTY;
	float l = 0.0f, t = 0.0f, r = 0.0f, b = 0.0f;

	if (node->m_Layer != LAYER_NONE)
	{
		if (node->m_HasBounds)
		{
			state = Actor::TREE_FINITE;
			l = -node->m_BoundL;
			r =  node->m_BoundR;
			t = -node->m_BoundU;
			b =  node->m_BoundD;
		}
		else
		{
			state = Actor::TREE_INFINITE;
		}
	}

	for (size_t i = 0; i < node->m_Children.size(); ++i)
	{
		Actor* c = node->m_Children[i];
		if (c->m_Destroyed) continue;

		RefreshBounds(c);

		if (c->m_TreeState == Actor::TREE_EMPTY) continue;
		if (c->m_TreeState == Actor::TREE_INFINITE || state == Actor::TREE_INFINITE)
		{
			state = Actor::TREE_INFINITE;
			continue;   // 무한이 되면 합칠 필요가 없지만 자식 캐시는 계속 갱신한다
		}

		float ox, oy;
		m_Ctx.Project(c->m_X, c->m_Y, c->m_Z, &ox, &oy);
		float cl = c->m_TreeL + ox, cr = c->m_TreeR + ox;
		float ct = c->m_TreeT + oy, cb = c->m_TreeB + oy;

		if (state == Actor::TREE_EMPTY)
		{
			state = Actor::TREE_FINITE;
			l = cl; r = cr; t = ct; b = cb;
		}
		else
		{
			if (cl < l) l = cl;
			if (cr > r) r = cr;
			if (ct < t) t = ct;
			if (cb > b) b = cb;
		}
	}

	node->m_TreeState = state;
	node->m_TreeL = l;
	node->m_TreeT = t;
	node->m_TreeR = r;
	node->m_TreeB = b;
	node->m_TreeDirty = false;
}

// ---- 그리기 -------------------------------------------------------------

bool SceneGraph::DrawLess(const DrawEntry& a, const DrawEntry& b)
{
	if (a.layer != b.layer) return a.layer < b.layer;
	return a.depth < b.depth;
}

void SceneGraph::Render(const RenderContext& rc, int firstLayer, int lastLayer)
{
	float tileW = m_Ctx.tileW, tileH = m_Ctx.tileH, hs = m_Ctx.heightScale;
	m_Ctx = rc;
	m_Ctx.tileW = tileW;
	m_Ctx.tileH = tileH;
	m_Ctx.heightScale = hs;

	{
		PROFILE_SCOPE("scene.render.bounds");
		RefreshBounds(&m_Root);
	}

	m_Stats.visited = m_Stats.culledTrees = m_Stats.culledSelf = m_Stats.drawn = 0;
	m_DrawList.clear();

	{
		PROFILE_SCOPE("scene.render.collect");
		float sx, sy;
		m_Ctx.WorldToScreen(m_Root.m_X, m_Root.m_Y, m_Root.m_Z, &sx, &sy);
		CollectNode(&m_Root, sx, sy, m_Root.m_X, m_Root.m_Y, firstLayer, lastLayer);
	}
	{
		PROFILE_SCOPE("scene.render.sort");
		std::sort(m_DrawList.begin(), m_DrawList.end(), DrawLess);
	}
	{
		PROFILE_SCOPE("scene.render.emit");
		for (size_t i = 0; i < m_DrawList.size(); ++i)
		{
			const DrawEntry& e = m_DrawList[i];
			if (e.actor->m_Profile.renderId >= 0)
			{
				Profiler::ScopeTimer t(e.actor->m_Profile.renderId);
				e.actor->OnRender(m_Ctx, e.sx, e.sy);
			}
			else
			{
				e.actor->OnRender(m_Ctx, e.sx, e.sy);
			}
		}
	}
	m_Stats.drawn = (int)m_DrawList.size();

	PROFILE_COUNT("scene.visited", m_Stats.visited);
	PROFILE_COUNT("scene.culled_subtrees", m_Stats.culledTrees);
	PROFILE_COUNT("scene.culled_self", m_Stats.culledSelf);
	PROFILE_COUNT("scene.drawn_actors", m_Stats.drawn);
}

void SceneGraph::CollectNode(Actor* node, float sx, float sy, float wx, float wy,
	int firstLayer, int lastLayer)
{
	if (node->m_Destroyed || !node->m_Visible) return;
	if (node->m_TreeState == Actor::TREE_EMPTY) return;

	++m_Stats.visited;

	// 서브트리 전체가 화면 밖이면 자손을 방문하지 않는다
	if (node->m_TreeState == Actor::TREE_FINITE)
	{
		if (sx + node->m_TreeR < m_Ctx.cullL || sx + node->m_TreeL > m_Ctx.cullR ||
			sy + node->m_TreeB < m_Ctx.cullT || sy + node->m_TreeT > m_Ctx.cullB)
		{
			++m_Stats.culledTrees;
			return;
		}
	}

	int layer = node->m_Layer;
	if (layer != LAYER_NONE && layer >= firstLayer && layer <= lastLayer)
	{
		bool out = false;
		if (node->m_HasBounds)
		{
			out = sx + node->m_BoundR < m_Ctx.cullL || sx - node->m_BoundL > m_Ctx.cullR ||
				sy + node->m_BoundD < m_Ctx.cullT || sy - node->m_BoundU > m_Ctx.cullB;
		}

		if (out)
		{
			++m_Stats.culledSelf;
		}
		else
		{
			DrawEntry e;
			e.layer = layer;
			e.depth = wx + wy + node->m_DepthBias;
			e.sx = sx;
			e.sy = sy;
			e.actor = node;
			m_DrawList.push_back(e);
		}
	}

	for (size_t i = 0; i < node->m_Children.size(); ++i)
	{
		Actor* c = node->m_Children[i];
		float ox, oy;
		m_Ctx.Project(c->m_X, c->m_Y, c->m_Z, &ox, &oy);
		CollectNode(c, sx + ox, sy + oy, wx + c->m_X, wy + c->m_Y,
			firstLayer, lastLayer);
	}
}

// ---- 기타 ---------------------------------------------------------------

int SceneGraph::CountActors() const
{
	return CountNode(&m_Root) - 1;
}

int SceneGraph::CountNode(const Actor* node)
{
	int n = 1;
	for (size_t i = 0; i < node->m_Children.size(); ++i)
		n += CountNode(node->m_Children[i]);
	return n;
}
