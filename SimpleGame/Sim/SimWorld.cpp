#include "SimWorld.h"

#include <cstdio>
#include <cstdlib>
#include <climits>

#include "../Profiler.h"

namespace Sim
{

static int Clamp(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

// ---- Rng (xorshift64*) ------------------------------------------------------------

uint64_t Rng::Next()
{
	uint64_t x = m_S;
	x ^= x >> 12;
	x ^= x << 25;
	x ^= x >> 27;
	m_S = x;
	return x * 2685821657736338717ull;
}

bool Rng::Chance(int pct)
{
	return (int)(Next() % 100ull) < pct;
}

int Rng::Range(int lo, int hi)
{
	if (hi <= lo) return lo;
	return lo + (int)(Next() % (uint64_t)(hi - lo + 1));
}

int Rng::Jitter(int value, int pct)
{
	int spread = abs(value) * pct / 100;
	if (spread == 0) return value;
	return value + Range(-spread, spread);
}

// =============================================================================
// World
// =============================================================================

World::World()
	: m_Loaded(false), m_Paused(false), m_DayLength(SIM_DAY_SECONDS), m_T(0.0f),
	m_Day(0), m_TickedDay(-1), m_NoticeSerial(0), m_PlayerRegion(-1), m_Locator(0), m_LocatorUser(0)
{
}

bool World::Load(const char* dataDir, uint64_t seed)
{
	m_Loaded = false;
	m_Data = Data();
	if (!m_Data.Load(dataDir))
	{
		printf("[Sim] load failed: %s\n", m_Data.GetError().c_str());
		return false;
	}

	m_Rng.Seed(seed);

	const Data& d = m_Data;
	m_Vars.resize(d.vars.size());
	for (size_t i = 0; i < d.vars.size(); ++i) m_Vars[i] = d.vars[i].init;

	m_Region.assign(d.regions.size() * RF_COUNT, 0);
	for (size_t i = 0; i < d.regions.size(); ++i)
		for (int f = 0; f < RF_COUNT; ++f)
			m_Region[i * RF_COUNT + f] = d.regions[i].init[f];

	m_FacRep.resize(d.factions.size());
	m_FacTreasury.resize(d.factions.size());
	for (size_t i = 0; i < d.factions.size(); ++i)
	{
		m_FacRep[i] = d.factions[i].rep;
		m_FacTreasury[i] = d.factions[i].treasury;
	}
	m_Frel = d.frelInit;

	m_Npcs.resize(d.npcs.size());
	for (size_t i = 0; i < d.npcs.size(); ++i)
	{
		const NpcDef& def = d.npcs[i];
		NpcState& s = m_Npcs[i];
		s.affinity = def.initAffinity;
		s.trust = def.initTrust;
		s.power = def.initPower;
		s.meter = def.initMeter;
		s.mark = 0;
		s.factionIdx = def.factionIdx;
		s.alive = true;
		s.captured = false;
		s.region = def.homeRegion;
		s.lastAction = -1;
		s.lastScore = 0;
		s.lastServedDay = -1;
		s.rel.assign(d.npcs.size(), 0);
		for (size_t k = 0; k < def.relInit.size(); ++k)
			s.rel[def.relInit[k].first] = def.relInit[k].second;
	}

	m_Flags.assign(d.flags.size(), 0);
	m_FlagExpire.assign(d.flags.size(), -1);
	m_Notices.clear();
	m_NoticeSerial = 0;

	m_Day = 0;
	m_T = 0.0f;
	m_TickedDay = -1;
	m_Loaded = true;
	return true;
}

void World::SetDayLength(float seconds)
{
	if (seconds < 8.0f) seconds = 8.0f;
	// keep the position inside the day when the length changes
	float k = m_T / m_DayLength;
	m_DayLength = seconds;
	m_T = k * m_DayLength;
}

int World::GetSegment() const
{
	int s = (int)(m_T / (m_DayLength / SIM_SEGMENTS));
	return Clamp(s, 0, SIM_SEGMENTS - 1);
}

float World::GetSegmentProgress() const
{
	float seg = m_DayLength / SIM_SEGMENTS;
	float t = m_T - GetSegment() * seg;
	return t / seg;
}

float World::GetDayProgress() const
{
	return m_T / m_DayLength;
}

void World::Update(float dt)
{
	if (!m_Loaded) return;

	// Day 0's tick is the dawn the game starts on
	if (m_TickedDay < m_Day) DailyTick();
	if (m_Paused) return;

	m_T += dt;
	while (m_T >= m_DayLength)
	{
		m_T -= m_DayLength;
		++m_Day;
		DailyTick();
	}
}

void World::SkipToNextDawn()
{
	if (!m_Loaded) return;
	m_T = 0.0f;
	++m_Day;
	DailyTick();
}

void World::DebugAdvanceDays(int days)
{
	for (int i = 0; i < days; ++i) SkipToNextDawn();
}

void World::DebugAdvanceSegment()
{
	if (!m_Loaded) return;
	float seg = m_DayLength / SIM_SEGMENTS;
	int next = GetSegment() + 1;
	if (next >= SIM_SEGMENTS) SkipToNextDawn();
	else m_T = next * seg + 0.01f;
}

// ---- daily tick (4.3) ---------------------------------------------------------

void World::DailyTick()
{
	PROFILE_SCOPE("sim.tick");
	m_TickedDay = m_Day;

	static const char* const seasons[4] = { "summer", "autumn", "winter", "spring" };
	printf("[Sim] ---- day %d (%s %d) ----\n", m_Day, seasons[GetSeason()], GetDayOfSeason());

	// Order is fixed (4.3). Slots 2, 4, 5 and the file log arrive with
	// stages 3 (plans), 5 (rumors), 8 (judgement) and 2 (world log).
	ExpireFlags();     // 1
	RunNpcActions();   // 3
}

void World::ExpireFlags()
{
	for (size_t i = 0; i < m_Flags.size(); ++i)
	{
		if (m_Flags[i] && m_FlagExpire[i] >= 0 && m_FlagExpire[i] <= m_Day)
		{
			m_Flags[i] = 0;
			m_FlagExpire[i] = -1;
		}
	}
}

void World::RunNpcActions()
{
	PROFILE_SCOPE("sim.brain");
	const int idle = m_Data.FindAction("idle");
	int executed = 0;

	for (size_t i = 0; i < m_Npcs.size(); ++i)
	{
		NpcState& s = m_Npcs[i];
		if (!s.alive || s.captured) continue;

		const NpcDef& def = m_Data.npcs[i];
		Ctx c;
		c.self = (int)i;
		c.victim = -1;

		// score(a) = base + weights . traits + goal bonus, jittered 10% (10)
		int best = -1;
		int bestScore = INT_MIN;
		for (size_t k = 0; k < def.actions.size(); ++k)
		{
			const ActionDef& a = m_Data.actions[def.actions[k]];
			if (!EvalCond(a.cond, c)) continue;

			int score = a.base
				+ a.wGreed * def.traits.greed
				+ a.wPrinciple * def.traits.principle
				+ a.wBold * def.traits.bold
				+ ((!a.goal.empty() && a.goal == def.goal) ? 20 : 0);
			score = m_Rng.Jitter(score, 10);

			if (score > bestScore)   // ties keep the earlier one
			{
				best = def.actions[k];
				bestScore = score;
			}
		}
		if (best < 0 || bestScore < IDLE_THRESHOLD)
		{
			best = idle;
			if (bestScore == INT_MIN) bestScore = 0;
		}

		s.lastAction = best;
		s.lastScore = bestScore;
		++executed;

		const ActionDef& a = m_Data.actions[best];
		Run(a.effect, c);

		if (a.eventIdx >= 0)
		{
			for (size_t j = 0; j < m_Npcs.size(); ++j)
			{
				if (j == i || !m_Npcs[j].alive || m_Npcs[j].captured) continue;
				if (m_Npcs[j].region != s.region) continue;
				ApplyEvent(a.eventIdx, (int)j, (int)i, 100);
			}
			ApplyEventGlobals(a.eventIdx, (int)i);
		}

		if (s.region == m_PlayerRegion)
		{
			printf("[Sim]   %-10s %-14s score %d\n", def.id.c_str(), a.id.c_str(), bestScore);
		}
	}

	PROFILE_COUNT("sim.actions", executed);
}

// ---- events (8) -----------------------------------------------------------------

void World::ApplyEvent(int evIdx, int npcIdx, int victimNpc, int scalePct)
{
	const EventDef& ev = m_Data.events[evIdx];
	const Traits& t = m_Data.npcs[npcIdx].traits;
	NpcState& s = m_Npcs[npcIdx];

	int delta = ev.delta + ev.wGreed * t.greed + ev.wPrinciple * t.principle
		+ ev.wBold * t.bold;
	delta = delta * scalePct / 100;

	s.affinity = Clamp(s.affinity + delta, -100, 100);
	s.trust = Clamp(s.trust + ev.trust, 0, 100);

	const NpcDef& def = m_Data.npcs[npcIdx];
	for (size_t k = 0; k < def.reacts.size(); ++k)
	{
		if (def.reacts[k].first != evIdx) continue;
		Ctx c;
		c.self = npcIdx;
		c.victim = victimNpc;
		Run(def.reacts[k].second, c);
	}
}

void World::ApplyEventGlobals(int evIdx, int victimNpc)
{
	const EventDef& ev = m_Data.events[evIdx];
	for (size_t k = 0; k < ev.factionDelta.size(); ++k)
	{
		int f = ev.factionDelta[k].first;
		if (f == IDX_VICTIM_FACTION)
			f = (victimNpc >= 0) ? m_Npcs[victimNpc].factionIdx : -1;
		if (f < 0) continue;
		m_FacRep[f] = Clamp(m_FacRep[f] + ev.factionDelta[k].second, -100, 100);
	}
	if (ev.fame != 0)
	{
		int fv = m_Data.FindVar("fame");
		if (fv >= 0)
			m_Vars[fv] = Clamp(m_Vars[fv] + ev.fame, m_Data.vars[fv].minV, m_Data.vars[fv].maxV);
	}
}

int World::OnPlayerEvent(const char* eventId, float wx, float wy, int victimNpc)
{
	if (!m_Loaded) return 0;
	int ev = m_Data.FindEvent(eventId);
	if (ev < 0)
	{
		printf("[Sim] unknown player event '%s'\n", eventId);
		return 0;
	}

	int count = 0;
	if (m_Data.events[ev].toVictim)
	{
		if (victimNpc >= 0 && victimNpc < (int)m_Npcs.size())
		{
			ApplyEvent(ev, victimNpc, victimNpc, 100);
			count = 1;
		}
	}
	else
	{
		for (size_t j = 0; j < m_Npcs.size(); ++j)
		{
			const NpcState& s = m_Npcs[j];
			if (!s.alive || s.captured || s.region != m_PlayerRegion) continue;

			if (m_Locator)
			{
				float nx, ny;
				if (!m_Locator((int)j, &nx, &ny, m_LocatorUser)) continue;
				float dx = nx - wx, dy = ny - wy;
				if (dx * dx + dy * dy > WITNESS_RADIUS * WITNESS_RADIUS) continue;
			}
			ApplyEvent(ev, (int)j, victimNpc, 100);
			++count;
		}
	}
	ApplyEventGlobals(ev, victimNpc);

	printf("[Sim] player event %s: %d npc(s) reacted\n", eventId, count);
	return count;
}

// ---- relationships (7.2) ----------------------------------------------------------

int World::GetEffAffinity(int npcIdx) const
{
	const NpcState& s = m_Npcs[npcIdx];
	int rep = (s.factionIdx >= 0) ? m_FacRep[s.factionIdx] : 0;
	return Clamp(s.affinity + rep / 2, -100, 100);
}

Band World::GetBand(int npcIdx) const
{
	int a = GetEffAffinity(npcIdx);
	if (a >= 60)  return BAND_ALLY;
	if (a >= 20)  return BAND_FRIENDLY;
	if (a > -20)  return BAND_NEUTRAL;
	if (a > -60)  return BAND_WARY;
	return BAND_HOSTILE;
}

const char* World::GetGreeting(int npcIdx) const
{
	const NpcDef& def = m_Data.npcs[npcIdx];
	int b = (int)GetBand(npcIdx) + 2;
	if (!def.greet[b].empty()) return def.greet[b].c_str();
	return def.greet[2].c_str();
}

const ActionDef* World::GetCurrentAction(int npcIdx) const
{
	int a = m_Npcs[npcIdx].lastAction;
	return (a >= 0) ? &m_Data.actions[a] : 0;
}

int World::GetVar(const char* id) const
{
	int i = m_Data.FindVar(id);
	return (i >= 0) ? m_Vars[i] : 0;
}

bool World::GetFlag(const char* id) const
{
	int i = m_Data.FindFlag(id);
	return (i >= 0) ? (m_Flags[i] != 0) : false;
}

int World::GetRegionValue(int region, int field) const
{
	if (region < 0 || field < 0 || field >= RF_COUNT) return 0;
	return m_Region[region * RF_COUNT + field];
}

int World::GetFactionRep(int faction) const
{
	return (faction >= 0) ? m_FacRep[faction] : 0;
}

// ---- expressions ------------------------------------------------------------------

int World::ResolveNpc(int idx, const Ctx& c) const
{
	if (idx == IDX_SELF) return c.self;
	return idx;
}

int World::ResolveFaction(int idx, const Ctx& c) const
{
	if (idx == IDX_SELF_FACTION) return (c.self >= 0) ? m_Npcs[c.self].factionIdx : -1;
	if (idx == IDX_VICTIM_FACTION) return (c.victim >= 0) ? m_Npcs[c.victim].factionIdx : -1;
	return idx;
}

int World::ResolveRegion(int idx, const Ctx& c) const
{
	if (idx == IDX_SELF_REGION) return (c.self >= 0) ? m_Npcs[c.self].region : m_PlayerRegion;
	return idx;
}

int World::ReadRef(const Ref& r, const Ctx& c) const
{
	switch (r.kind)
	{
	case REF_CONST:   return r.a;
	case REF_DAY:     return m_Day;
	case REF_SEASON:  return (int)GetSeason();
	case REF_SEGMENT: return GetSegment();
	case REF_VAR:     return m_Vars[r.a];
	case REF_REGION:
	{
		int g = ResolveRegion(r.a, c);
		return (g >= 0) ? m_Region[g * RF_COUNT + r.field] : 0;
	}
	case REF_NPC:
	{
		int n = ResolveNpc(r.a, c);
		if (n < 0) return 0;
		const NpcState& s = m_Npcs[n];
		switch (r.field)
		{
		case NF_AFFINITY:     return GetEffAffinity(n);
		case NF_RAW_AFFINITY: return s.affinity;
		case NF_TRUST:        return s.trust;
		case NF_POWER:        return s.power;
		case NF_METER:        return s.meter;
		case NF_MARK:         return s.mark;
		case NF_ALIVE:        return s.alive ? 1 : 0;
		case NF_CAPTURED:     return s.captured ? 1 : 0;
		case NF_BAND:         return (int)GetBand(n);
		case NF_BLACK:        return (s.factionIdx >= 0
			&& s.factionIdx == m_Data.FindFaction("black_table")) ? 1 : 0;
		}
		return 0;
	}
	case REF_FACTION:
	{
		int f = ResolveFaction(r.a, c);
		if (f < 0) return 0;
		return (r.field == FF_REP) ? m_FacRep[f] : m_FacTreasury[f];
	}
	case REF_FREL:
		return m_Frel[r.a * m_Data.factions.size() + r.b];
	case REF_REL:
	{
		int a = ResolveNpc(r.a, c), b = ResolveNpc(r.b, c);
		return (a >= 0 && b >= 0) ? m_Npcs[a].rel[b] : 0;
	}
	default:
		return 0;
	}
}

void World::WriteRef(const Ref& r, int v, const Ctx& c)
{
	switch (r.kind)
	{
	case REF_VAR:
		m_Vars[r.a] = Clamp(v, m_Data.vars[r.a].minV, m_Data.vars[r.a].maxV);
		break;
	case REF_REGION:
	{
		int g = ResolveRegion(r.a, c);
		if (g >= 0) m_Region[g * RF_COUNT + r.field] = Clamp(v, 0, 100);
		break;
	}
	case REF_NPC:
	{
		int n = ResolveNpc(r.a, c);
		if (n < 0) break;
		NpcState& s = m_Npcs[n];
		switch (r.field)
		{
		case NF_RAW_AFFINITY: s.affinity = Clamp(v, -100, 100); break;
		case NF_TRUST:        s.trust = Clamp(v, 0, 100); break;
		case NF_POWER:        s.power = Clamp(v, 0, 999); break;
		case NF_METER:        s.meter = Clamp(v, 0, 100); break;
		case NF_MARK:         s.mark = Clamp(v, 0, 999); break;
		}
		break;
	}
	case REF_FACTION:
	{
		int f = ResolveFaction(r.a, c);
		if (f < 0) break;
		if (r.field == FF_REP) m_FacRep[f] = Clamp(v, -100, 100);
		else                   m_FacTreasury[f] = Clamp(v, 0, 100);
		break;
	}
	case REF_FREL:
	{
		size_t n = m_Data.factions.size();
		m_Frel[r.a * n + r.b] = m_Frel[r.b * n + r.a] = Clamp(v, -100, 100);
		break;
	}
	case REF_REL:
	{
		int a = ResolveNpc(r.a, c), b = ResolveNpc(r.b, c);
		if (a >= 0 && b >= 0) m_Npcs[a].rel[b] = Clamp(v, -100, 100);
		break;
	}
	default:
		break;
	}
}

bool World::EvalCond(const Cond& cond, const Ctx& c) const
{
	if (cond.ors.empty()) return true;

	for (size_t o = 0; o < cond.ors.size(); ++o)
	{
		bool all = true;
		const std::vector<CondTerm>& ands = cond.ors[o];
		for (size_t k = 0; k < ands.size() && all; ++k)
		{
			const CondTerm& t = ands[k];
			bool v = false;
			switch (t.type)
			{
			case TERM_TRUE:  v = true; break;
			case TERM_FALSE: v = false; break;
			case TERM_FLAG:  v = m_Flags[t.flag] != 0; break;
			case TERM_STUB:  v = false; break;
			case TERM_CMP:
			{
				int a = ReadRef(t.lhs, c), b = ReadRef(t.rhs, c);
				switch (t.op)
				{
				case OP_LT: v = a < b; break;
				case OP_LE: v = a <= b; break;
				case OP_GT: v = a > b; break;
				case OP_GE: v = a >= b; break;
				case OP_EQ: v = a == b; break;
				case OP_NE: v = a != b; break;
				}
				break;
			}
			}
			if (t.negate) v = !v;
			all = v;
		}
		if (all) return true;
	}
	return false;
}

void World::Join(int npc, int faction)
{
	if (npc < 0) return;
	m_Npcs[npc].factionIdx = faction;
	printf("[Sim] %s joins %s\n", m_Data.npcs[npc].id.c_str(),
		faction >= 0 ? m_Data.factions[faction].id.c_str() : "none");
}

void World::Notify(int notice, int region)
{
	// only what the player could plausibly hear about (15.3 notify)
	if (region >= 0 && region != m_PlayerRegion) return;
	Notice n;
	n.day = m_Day;
	n.text = m_Data.notices[notice].text;
	m_Notices.push_back(n);
	++m_NoticeSerial;
	if (m_Notices.size() > 16) m_Notices.erase(m_Notices.begin());
	printf("[Sim] notice: %s\n", m_Data.notices[notice].id.c_str());
}

void World::Run(const EffectList& fx, const Ctx& c)
{
	for (size_t i = 0; i < fx.size(); ++i)
	{
		const Effect& e = fx[i];
		switch (e.op)
		{
		case EF_ADD: WriteRef(e.ref, ReadRef(e.ref, c) + e.n, c); break;
		case EF_SET: WriteRef(e.ref, e.n, c); break;
		case EF_FLAG:
			m_Flags[e.a] = 1;
			m_FlagExpire[e.a] = (e.n > 0) ? m_Day + e.n : -1;
			break;
		case EF_UNFLAG:
			m_Flags[e.a] = 0;
			m_FlagExpire[e.a] = -1;
			break;
		case EF_EVENT:
		{
			// strategy layer witnesses: everyone in the same region (8.2)
			int subject = ResolveNpc(e.b, c);
			int region = (c.self >= 0) ? m_Npcs[c.self].region : m_PlayerRegion;
			for (size_t j = 0; j < m_Npcs.size(); ++j)
			{
				if (!m_Npcs[j].alive || m_Npcs[j].captured || m_Npcs[j].region != region) continue;
				ApplyEvent(e.a, (int)j, subject, 100);
			}
			ApplyEventGlobals(e.a, subject);
			break;
		}
		case EF_LOG:
			printf("[Sim] log: %s\n", e.s.c_str());
			break;
		case EF_NOTIFY:
			Notify(e.a, (c.self >= 0) ? m_Npcs[c.self].region : -1);
			break;
		case EF_JOIN:
			Join(ResolveNpc(e.a, c), e.b);
			break;
		case EF_MOVE:
		{
			int n = ResolveNpc(e.a, c);
			if (n >= 0) m_Npcs[n].region = e.b;
			break;
		}
		case EF_KILL:    { int n = ResolveNpc(e.a, c); if (n >= 0) m_Npcs[n].alive = false; break; }
		case EF_CAPTURE: { int n = ResolveNpc(e.a, c); if (n >= 0) m_Npcs[n].captured = true; break; }
		case EF_FREE:    { int n = ResolveNpc(e.a, c); if (n >= 0) m_Npcs[n].captured = false; break; }
		case EF_CHANCE:
			if (m_Rng.Chance(e.n)) Run(e.sub, c);
			break;
		case EF_RECRUIT_HOSTILE:
		{
			// 15.9: first NPC (definition order) that is not black_table, alive,
			// hostile to the player and greedy
			int bt = m_Data.FindFaction("black_table");
			for (size_t j = 0; j < m_Npcs.size(); ++j)
			{
				const NpcState& s = m_Npcs[j];
				if (s.factionIdx == bt || !s.alive || s.captured) continue;
				if ((int)GetBand((int)j) > (int)BAND_HOSTILE) continue;
				if (m_Data.npcs[j].traits.greed < 1) continue;
				Join((int)j, bt);
				break;
			}
			break;
		}
		case EF_STUB:
		default:
			break;
		}
	}
}

// ---- debug -------------------------------------------------------------------------

void World::DumpState() const
{
	if (!m_Loaded) return;
	printf("[Sim] ==== state day %d segment %d ====\n", m_Day, GetSegment());
	for (size_t i = 0; i < m_Vars.size(); ++i)
		printf("  var %-14s %d\n", m_Data.vars[i].id.c_str(), m_Vars[i]);
	for (size_t i = 0; i < m_Data.regions.size(); ++i)
		printf("  region %-8s prosperity %d black %d unrest %d security %d\n",
			m_Data.regions[i].id.c_str(), m_Region[i * RF_COUNT + 0], m_Region[i * RF_COUNT + 1],
			m_Region[i * RF_COUNT + 2], m_Region[i * RF_COUNT + 3]);
	for (size_t i = 0; i < m_Data.factions.size(); ++i)
		printf("  faction %-12s rep %d treasury %d\n", m_Data.factions[i].id.c_str(),
			m_FacRep[i], m_FacTreasury[i]);
	for (size_t i = 0; i < m_Npcs.size(); ++i)
	{
		const NpcState& s = m_Npcs[i];
		const ActionDef* a = GetCurrentAction((int)i);
		printf("  npc %-10s %-12s aff %4d (eff %4d, band %2d) trust %3d power %3d %s%s%s\n",
			m_Data.npcs[i].id.c_str(),
			s.factionIdx >= 0 ? m_Data.factions[s.factionIdx].id.c_str() : "none",
			s.affinity, GetEffAffinity((int)i), (int)GetBand((int)i), s.trust, s.power,
			a ? a->id.c_str() : "-", s.alive ? "" : " DEAD", s.captured ? " CAPTURED" : "");
	}
}

} // namespace Sim
