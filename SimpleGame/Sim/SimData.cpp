#include "SimData.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Sim
{

static const char* const FILES[] =
{
	"world.ggd", "factions.ggd", "events.ggd", "actions.ggd", "npcs.ggd"
};
static const int FILE_COUNT = 5;

static int IndexOf(const std::vector<std::string>& ids, const std::string& id)
{
	for (size_t i = 0; i < ids.size(); ++i)
		if (ids[i] == id) return (int)i;
	return -1;
}

static bool StartsWith(const std::string& s, const char* p)
{
	return s.compare(0, strlen(p), p) == 0;
}

Data::Data()
	: stubRefs(0), m_Line(0), m_Failed(false)
{
}

int Data::FindVar(const std::string& id) const     { return IndexOf(m_VarIds, id); }
int Data::FindRegion(const std::string& id) const  { return IndexOf(m_RegionIds, id); }
int Data::FindFaction(const std::string& id) const { return IndexOf(m_FactionIds, id); }
int Data::FindNpc(const std::string& id) const     { return IndexOf(m_NpcIds, id); }
int Data::FindAction(const std::string& id) const  { return IndexOf(m_ActionIds, id); }
int Data::FindEvent(const std::string& id) const   { return IndexOf(m_EventIds, id); }
int Data::FindNotice(const std::string& id) const  { return IndexOf(m_NoticeIds, id); }
int Data::FindFlag(const std::string& id) const    { return IndexOf(flags, id); }

void Data::Fail(int line, const std::string& msg)
{
	if (m_Failed) return;   // keep the first error; later ones are usually fallout
	char buf[64];
	snprintf(buf, sizeof(buf), ":%d: ", line);
	m_Err = m_File + buf + msg;
	m_Failed = true;
}

void Data::Warn(int line, const std::string& msg)
{
	printf("[Sim] warning %s:%d: %s\n", m_File.c_str(), line, msg.c_str());
}

bool Data::ParseInt(const std::string& s, int* out)
{
	std::string t = Trim(s);
	if (t.empty()) return false;
	char* end = 0;
	long v = strtol(t.c_str(), &end, 10);
	if (end == t.c_str() || *end != 0) return false;
	*out = (int)v;
	return true;
}

int Data::InternFlag(const std::string& name)
{
	int i = IndexOf(flags, name);
	if (i >= 0) return i;
	flags.push_back(name);
	return (int)flags.size() - 1;
}

bool Data::Register(std::vector<std::string>& ids, const GgdBlock& b)
{
	if (IndexOf(ids, b.id) >= 0)
	{
		Fail(b.line, "duplicate id '" + b.id + "'");
		return false;
	}
	ids.push_back(b.id);
	return true;
}

// =============================================================================
// Load
// =============================================================================

bool Data::Load(const char* dataDir)
{
	GgdFile files[FILE_COUNT];

	for (int i = 0; i < FILE_COUNT; ++i)
	{
		std::string path = std::string(dataDir) + "/" + FILES[i];
		std::string err;
		if (!ParseGgd(path.c_str(), &files[i], &err))
		{
			m_Err = err;
			return false;
		}
	}

	// ---- pass 1: ids. Everything can refer to everything, so every id must
	// exist before the first condition is compiled.
	for (int i = 0; i < FILE_COUNT; ++i)
	{
		m_File = files[i].path;
		for (size_t k = 0; k < files[i].blocks.size(); ++k)
		{
			const GgdBlock& b = files[i].blocks[k];
			if      (b.type == "var")     Register(m_VarIds, b);
			else if (b.type == "region")  Register(m_RegionIds, b);
			else if (b.type == "notice")  Register(m_NoticeIds, b);
			else if (b.type == "faction") Register(m_FactionIds, b);
			else if (b.type == "event")   Register(m_EventIds, b);
			else if (b.type == "action")  Register(m_ActionIds, b);
			else if (b.type == "npc")     Register(m_NpcIds, b);
			else if (b.type == "choice")  Register(m_ChoiceIds, b);
		}
		if (m_Failed) return false;
	}

	vars.resize(m_VarIds.size());
	regions.resize(m_RegionIds.size());
	notices.resize(m_NoticeIds.size());
	factions.resize(m_FactionIds.size());
	events.resize(m_EventIds.size());
	actions.resize(m_ActionIds.size());
	npcs.resize(m_NpcIds.size());
	frelInit.assign(m_FactionIds.size() * m_FactionIds.size(), 0);

	// ---- pass 2: contents
	for (int i = 0; i < FILE_COUNT; ++i)
	{
		Build(files[i]);
		if (m_Failed) return false;
	}

	if (IndexOf(m_ActionIds, "idle") < 0)
	{
		m_Err = "actions.ggd: action 'idle' is required (fallback when nothing scores)";
		return false;
	}

	printf("[Sim] loaded: %d npcs, %d actions, %d events, %d factions, %d regions",
		(int)npcs.size(), (int)actions.size(), (int)events.size(),
		(int)factions.size(), (int)regions.size());
	if (stubRefs > 0)
		printf(" (%d plan/legend/fact refs stubbed until those stages)", stubRefs);
	printf("\n");
	return true;
}

void Data::Build(const GgdFile& f)
{
	m_File = f.path;
	for (size_t k = 0; k < f.blocks.size() && !m_Failed; ++k)
	{
		const GgdBlock& b = f.blocks[k];
		m_Line = b.line;
		if      (b.type == "var")     BuildVar(b);
		else if (b.type == "region")  BuildRegion(b);
		else if (b.type == "notice")  BuildNotice(b);
		else if (b.type == "faction") BuildFaction(b);
		else if (b.type == "frel")    BuildFrel(b);
		else if (b.type == "event")   BuildEvent(b);
		else if (b.type == "action")  BuildAction(b);
		else if (b.type == "npc")     BuildNpc(b);
		else if (b.type == "choice")  BuildGlobalChoice(b);
		else Warn(b.line, "unknown block type '" + b.type + "' ignored");
	}
}

// ---- simple blocks ------------------------------------------------------------

void Data::BuildVar(const GgdBlock& b)
{
	VarDef& v = vars[FindVar(b.id)];
	v.id = b.id;
	v.init = 0;
	v.minV = 0;
	v.maxV = 1000000;
	for (size_t i = 0; i < b.entries.size(); ++i)
	{
		const GgdEntry& e = b.entries[i];
		int* dst = 0;
		if (e.key == "init") dst = &v.init;
		else if (e.key == "min") dst = &v.minV;
		else if (e.key == "max") dst = &v.maxV;
		else { Warn(e.line, "unknown key '" + e.key + "'"); continue; }
		if (!ParseInt(e.value, dst)) { Fail(e.line, "number expected"); return; }
	}
}

void Data::BuildRegion(const GgdBlock& b)
{
	RegionDef& r = regions[FindRegion(b.id)];
	r.id = b.id;
	r.closed = false;
	for (int i = 0; i < RF_COUNT; ++i) r.init[i] = 0;

	for (size_t i = 0; i < b.entries.size(); ++i)
	{
		const GgdEntry& e = b.entries[i];
		int field = -1;
		if (e.key == "name") r.name = e.value;
		else if (e.key == "map") r.map = e.value;
		else if (e.key == "closed") r.closed = (e.value == "true");
		else if (e.key == "prosperity") field = RF_PROSPERITY;
		else if (e.key == "black") field = RF_BLACK;
		else if (e.key == "unrest") field = RF_UNREST;
		else if (e.key == "security") field = RF_SECURITY;
		else Warn(e.line, "unknown key '" + e.key + "'");

		if (field >= 0 && !ParseInt(e.value, &r.init[field]))
		{
			Fail(e.line, "number expected");
			return;
		}
	}
}

void Data::BuildNotice(const GgdBlock& b)
{
	NoticeDef& n = notices[FindNotice(b.id)];
	n.id = b.id;
	for (size_t i = 0; i < b.entries.size(); ++i)
	{
		if (b.entries[i].key == "text") n.text = Unescape(b.entries[i].value);
		else Warn(b.entries[i].line, "unknown key '" + b.entries[i].key + "'");
	}
}

void Data::BuildFaction(const GgdBlock& b)
{
	FactionDef& f = factions[FindFaction(b.id)];
	f.id = b.id;
	f.rep = 0;
	f.treasury = 0;
	for (size_t i = 0; i < b.entries.size(); ++i)
	{
		const GgdEntry& e = b.entries[i];
		if (e.key == "name") f.name = e.value;
		else if (e.key == "rep") { if (!ParseInt(e.value, &f.rep)) { Fail(e.line, "number expected"); return; } }
		else if (e.key == "treasury") { if (!ParseInt(e.value, &f.treasury)) { Fail(e.line, "number expected"); return; } }
		else Warn(e.line, "unknown key '" + e.key + "'");
	}
}

void Data::BuildFrel(const GgdBlock& b)
{
	const int n = (int)factions.size();
	for (size_t i = 0; i < b.entries.size(); ++i)
	{
		const GgdEntry& e = b.entries[i];
		if (e.key != "pair") { Warn(e.line, "unknown key '" + e.key + "'"); continue; }

		// a.b:value
		size_t dot = e.value.find('.');
		size_t colon = e.value.find(':');
		int v = 0;
		if (dot == std::string::npos || colon == std::string::npos || colon < dot
			|| !ParseInt(e.value.substr(colon + 1), &v))
		{
			Fail(e.line, "expected 'a.b:value'");
			return;
		}
		int fa = FindFaction(Trim(e.value.substr(0, dot)));
		int fb = FindFaction(Trim(e.value.substr(dot + 1, colon - dot - 1)));
		if (fa < 0 || fb < 0) { Fail(e.line, "unknown faction in '" + e.value + "'"); return; }
		frelInit[fa * n + fb] = v;
		frelInit[fb * n + fa] = v;
	}
}

void Data::BuildEvent(const GgdBlock& b)
{
	EventDef& ev = events[FindEvent(b.id)];
	ev.id = b.id;
	ev.delta = ev.wGreed = ev.wPrinciple = ev.wBold = ev.trust = ev.fame = 0;
	ev.toVictim = false;

	for (size_t i = 0; i < b.entries.size(); ++i)
	{
		const GgdEntry& e = b.entries[i];
		int* dst = 0;
		if (e.key == "delta") dst = &ev.delta;
		else if (e.key == "wGreed") dst = &ev.wGreed;
		else if (e.key == "wPrinciple") dst = &ev.wPrinciple;
		else if (e.key == "wBold") dst = &ev.wBold;
		else if (e.key == "trust") dst = &ev.trust;
		else if (e.key == "fame") dst = &ev.fame;
		else if (e.key == "target") ev.toVictim = (e.value == "victim");
		else if (e.key == "faction_delta")
		{
			std::vector<std::string> items = SplitList(e.value, '|');
			for (size_t k = 0; k < items.size(); ++k)
			{
				size_t colon = items[k].find(':');
				int v = 0;
				if (colon == std::string::npos || !ParseInt(items[k].substr(colon + 1), &v))
				{
					Fail(e.line, "expected 'faction:value'");
					return;
				}
				std::string fid = Trim(items[k].substr(0, colon));
				int fi = (fid == "@victim_faction") ? IDX_VICTIM_FACTION : FindFaction(fid);
				if (fi == -1) { Fail(e.line, "unknown faction '" + fid + "'"); return; }
				ev.factionDelta.push_back(std::make_pair(fi, v));
			}
		}
		else Warn(e.line, "unknown key '" + e.key + "'");

		if (dst && !ParseInt(e.value, dst)) { Fail(e.line, "number expected"); return; }
	}
}

void Data::BuildAction(const GgdBlock& b)
{
	ActionDef& a = actions[FindAction(b.id)];
	a.id = b.id;
	a.base = a.wGreed = a.wPrinciple = a.wBold = 0;
	a.eventIdx = -1;
	a.behavior = "stay";
	bool hasBase = false;

	for (size_t i = 0; i < b.entries.size(); ++i)
	{
		const GgdEntry& e = b.entries[i];
		m_Line = e.line;
		int* dst = 0;
		if (e.key == "base") { dst = &a.base; hasBase = true; }
		else if (e.key == "wGreed") dst = &a.wGreed;
		else if (e.key == "wPrinciple") dst = &a.wPrinciple;
		else if (e.key == "wBold") dst = &a.wBold;
		else if (e.key == "goal") a.goal = e.value;
		else if (e.key == "name") a.name = e.value;
		else if (e.key == "behavior") a.behavior = e.value;
		else if (e.key == "cond") { if (!ParseCond(e.value, &a.cond)) return; }
		else if (e.key == "effect") { if (!ParseEffects(e.value, &a.effect)) return; }
		else if (e.key == "event")
		{
			a.eventIdx = FindEvent(e.value);
			if (a.eventIdx < 0) { Fail(e.line, "unknown event '" + e.value + "'"); return; }
		}
		else Warn(e.line, "unknown key '" + e.key + "'");

		if (dst && !ParseInt(e.value, dst)) { Fail(e.line, "number expected"); return; }
	}
	if (!hasBase) Fail(b.line, "action '" + b.id + "' needs 'base'");
	if (a.name.empty()) a.name = a.id;
}

void Data::BuildGlobalChoice(const GgdBlock& b)
{
	ChoiceDef c;
	c.id = b.id;
	for (size_t i = 0; i < b.entries.size(); ++i)
	{
		const GgdEntry& e = b.entries[i];
		m_Line = e.line;
		if (e.key == "text") c.text = Unescape(e.value);
		else if (e.key == "cond") { if (!ParseCond(e.value, &c.cond)) return; }
		else if (e.key == "effect") { if (!ParseEffects(e.value, &c.effect)) return; }
		else Warn(e.line, "unknown key '" + e.key + "'");
	}
	globalChoices.push_back(c);
}

// ---- npc ----------------------------------------------------------------------

void Data::BuildNpc(const GgdBlock& b)
{
	NpcDef& n = npcs[FindNpc(b.id)];
	n.id = b.id;
	n.factionIdx = -1;
	n.traits.greed = n.traits.principle = n.traits.bold = 0;
	n.homeRegion = -1;
	n.initAffinity = n.initTrust = n.initPower = n.initMeter = 0;
	n.hasCloth = false;
	n.cloth[0] = n.cloth[1] = n.cloth[2] = 0.5f;

	bool hasName = false, hasFaction = false, hasRegion = false, hasActions = false;

	for (size_t i = 0; i < b.entries.size(); ++i)
	{
		const GgdEntry& e = b.entries[i];
		const std::string& k = e.key;
		m_Line = e.line;
		int* dst = 0;

		if (k == "name") { n.name = e.value; hasName = true; }
		else if (k == "title") n.title = e.value;
		else if (k == "faction")
		{
			hasFaction = true;
			if (e.value != "none")
			{
				n.factionIdx = FindFaction(e.value);
				if (n.factionIdx < 0) { Fail(e.line, "unknown faction '" + e.value + "'"); return; }
			}
		}
		else if (k == "region")
		{
			hasRegion = true;
			n.homeRegion = FindRegion(e.value);
			if (n.homeRegion < 0) { Fail(e.line, "unknown region '" + e.value + "'"); return; }
		}
		else if (k == "traits")
		{
			int t[3] = { 0, 0, 0 };
			std::vector<std::string> parts;
			std::string cur;
			for (size_t c = 0; c <= e.value.size(); ++c)
			{
				if (c == e.value.size() || e.value[c] == ' ' || e.value[c] == '\t')
				{
					if (!cur.empty()) parts.push_back(cur);
					cur.clear();
				}
				else cur += e.value[c];
			}
			if (parts.size() != 3 || !ParseInt(parts[0], &t[0]) || !ParseInt(parts[1], &t[1])
				|| !ParseInt(parts[2], &t[2]))
			{
				Fail(e.line, "traits = <greed> <principle> <bold>");
				return;
			}
			for (int c = 0; c < 3; ++c)
			{
				if (t[c] < -2 || t[c] > 2) { Fail(e.line, "trait out of range -2..2"); return; }
			}
			n.traits.greed = t[0];
			n.traits.principle = t[1];
			n.traits.bold = t[2];
		}
		else if (k == "goal") n.goal = e.value;
		else if (k == "likes") n.likes = SplitList(e.value, '|');
		else if (k == "dislikes") n.dislikes = SplitList(e.value, '|');
		else if (k == "actions")
		{
			hasActions = true;
			std::vector<std::string> ids = SplitList(e.value, '|');
			for (size_t c = 0; c < ids.size(); ++c)
			{
				int ai = FindAction(ids[c]);
				if (ai < 0) { Fail(e.line, "unknown action '" + ids[c] + "'"); return; }
				n.actions.push_back(ai);
			}
		}
		else if (k == "schedule")
		{
			n.schedule = SplitList(e.value, '|');
			if (n.schedule.size() != (size_t)SIM_SEGMENTS)
			{
				Fail(e.line, "schedule needs 4 locations (dawn|day|evening|night)");
				return;
			}
		}
		else if (k == "rel")
		{
			std::vector<std::string> items = SplitList(e.value, '|');
			for (size_t c = 0; c < items.size(); ++c)
			{
				size_t colon = items[c].find(':');
				int v = 0;
				int ni = (colon == std::string::npos) ? -1
					: FindNpc(Trim(items[c].substr(0, colon)));
				if (ni < 0 || !ParseInt(items[c].substr(colon + 1), &v))
				{
					Fail(e.line, "rel expects 'npc:value', got '" + items[c] + "'");
					return;
				}
				n.relInit.push_back(std::make_pair(ni, v));
			}
		}
		else if (k == "init_affinity") dst = &n.initAffinity;
		else if (k == "init_trust") dst = &n.initTrust;
		else if (k == "power") dst = &n.initPower;
		else if (k == "meter") dst = &n.initMeter;
		else if (StartsWith(k, "greet."))
		{
			static const char* const bands[5] = { "hostile", "wary", "neutral", "friendly", "ally" };
			std::string band = k.substr(6);
			int bi = -1;
			for (int c = 0; c < 5; ++c) if (band == bands[c]) bi = c;
			if (bi < 0) { Fail(e.line, "unknown band '" + band + "'"); return; }
			n.greet[bi] = Unescape(e.value);
		}
		else if (StartsWith(k, "ask."))
		{
			// facts are a later stage; keep the text, count the reference
			n.asks.push_back(std::make_pair(k.substr(4), Unescape(e.value)));
			++stubRefs;
		}
		else if (StartsWith(k, "choice."))
		{
			std::string rest = k.substr(7);
			size_t dot = rest.rfind('.');
			if (dot == std::string::npos) { Fail(e.line, "choice.<id>.<text|cond|effect>"); return; }
			std::string cid = rest.substr(0, dot);
			std::string field = rest.substr(dot + 1);

			ChoiceDef* c = 0;
			for (size_t q = 0; q < n.choices.size(); ++q)
				if (n.choices[q].id == cid) c = &n.choices[q];
			if (!c)
			{
				n.choices.push_back(ChoiceDef());
				c = &n.choices.back();
				c->id = cid;
			}
			if (field == "text") c->text = Unescape(e.value);
			else if (field == "cond") { if (!ParseCond(e.value, &c->cond)) return; }
			else if (field == "effect") { if (!ParseEffects(e.value, &c->effect)) return; }
			else { Fail(e.line, "choice field must be text, cond or effect"); return; }
		}
		else if (k == "on_served") { if (!ParseEffects(e.value, &n.onServed)) return; }
		else if (StartsWith(k, "react."))
		{
			int ev = FindEvent(k.substr(6));
			if (ev < 0) { Fail(e.line, "unknown event '" + k.substr(6) + "'"); return; }
			EffectList fx;
			if (!ParseEffects(e.value, &fx)) return;
			n.reacts.push_back(std::make_pair(ev, fx));
		}
		else if (k == "cloth")
		{
			float c[3];
#ifdef _MSC_VER
			int got = sscanf_s(e.value.c_str(), "%f %f %f", &c[0], &c[1], &c[2]);
#else
			int got = sscanf(e.value.c_str(), "%f %f %f", &c[0], &c[1], &c[2]);
#endif
			if (got != 3) { Fail(e.line, "cloth = <r> <g> <b>"); return; }
			n.hasCloth = true;
			n.cloth[0] = c[0]; n.cloth[1] = c[1]; n.cloth[2] = c[2];
		}
		else if (k == "look") n.look = e.value;
		else if (StartsWith(k, "bark."))
		{
			n.barks.push_back(std::make_pair(k.substr(5), Unescape(e.value)));
		}
		else Warn(e.line, "unknown key '" + k + "'");

		if (dst && !ParseInt(e.value, dst)) { Fail(e.line, "number expected"); return; }
	}

	if (!hasName)    { Fail(b.line, "npc '" + b.id + "' needs 'name'"); return; }
	if (!hasFaction) { Fail(b.line, "npc '" + b.id + "' needs 'faction' (or 'none')"); return; }
	if (!hasRegion)  { Fail(b.line, "npc '" + b.id + "' needs 'region'"); return; }
	if (!hasActions) n.actions.push_back(FindAction("idle"));
}

// =============================================================================
// Expressions
// =============================================================================

int Data::ParseNpcArg(const std::string& s)
{
	std::string t = Trim(s);
	if (t == "@self") return IDX_SELF;
	return FindNpc(t);
}

bool Data::ParseRef(const std::string& src, Ref* out)
{
	std::string s = Trim(src);
	out->kind = REF_NONE;
	out->a = out->b = out->field = 0;

	int num;
	if (ParseInt(s, &num)) { out->kind = REF_CONST; out->a = num; return true; }
	if (s == "day")     { out->kind = REF_DAY; return true; }
	if (s == "season")  { out->kind = REF_SEASON; return true; }
	if (s == "segment") { out->kind = REF_SEGMENT; return true; }

	size_t colon = s.find(':');
	if (colon == std::string::npos) { Fail(m_Line, "unknown reference '" + s + "'"); return false; }
	std::string ns = s.substr(0, colon);
	std::string rest = s.substr(colon + 1);

	if (ns == "plan" || ns == "legend")
	{
		out->kind = REF_STUB;
		++stubRefs;
		return true;
	}

	if (ns == "var")
	{
		out->kind = REF_VAR;
		out->a = FindVar(rest);
		if (out->a < 0) { Fail(m_Line, "unknown var '" + rest + "'"); return false; }
		return true;
	}

	size_t dot = rest.find('.');
	if (dot == std::string::npos) { Fail(m_Line, "expected '<id>.<field>' in '" + s + "'"); return false; }
	std::string id = rest.substr(0, dot);
	std::string field = rest.substr(dot + 1);

	if (ns == "region")
	{
		out->kind = REF_REGION;
		out->a = (id == "@region") ? IDX_SELF_REGION : FindRegion(id);
		static const char* const names[RF_COUNT] = { "prosperity", "black", "unrest", "security" };
		out->field = -1;
		for (int i = 0; i < RF_COUNT; ++i) if (field == names[i]) out->field = i;
		if (out->a == -1) { Fail(m_Line, "unknown region '" + id + "'"); return false; }
		if (out->field < 0) { Fail(m_Line, "unknown region field '" + field + "'"); return false; }
		return true;
	}
	if (ns == "npc")
	{
		out->kind = REF_NPC;
		out->a = ParseNpcArg(id);
		static const char* const names[] = { "affinity", "raw_affinity", "trust", "power",
			"meter", "mark", "alive", "captured", "band", "black" };
		out->field = -1;
		for (int i = 0; i < 10; ++i) if (field == names[i]) out->field = i;
		if (out->a == -1) { Fail(m_Line, "unknown npc '" + id + "'"); return false; }
		if (out->field < 0) { Fail(m_Line, "unknown npc field '" + field + "'"); return false; }
		return true;
	}
	if (ns == "faction")
	{
		out->kind = REF_FACTION;
		out->a = (id == "@faction") ? IDX_SELF_FACTION : FindFaction(id);
		out->field = (field == "rep") ? FF_REP : (field == "treasury") ? FF_TREASURY : -1;
		if (out->a == -1) { Fail(m_Line, "unknown faction '" + id + "'"); return false; }
		if (out->field < 0) { Fail(m_Line, "unknown faction field '" + field + "'"); return false; }
		return true;
	}
	if (ns == "frel")
	{
		out->kind = REF_FREL;
		out->a = FindFaction(id);
		out->b = FindFaction(field);
		if (out->a < 0 || out->b < 0) { Fail(m_Line, "unknown faction in '" + s + "'"); return false; }
		return true;
	}
	if (ns == "rel")
	{
		out->kind = REF_REL;
		out->a = ParseNpcArg(id);
		out->b = ParseNpcArg(field);
		if (out->a == -1 || out->b == -1) { Fail(m_Line, "unknown npc in '" + s + "'"); return false; }
		return true;
	}

	Fail(m_Line, "unknown reference '" + s + "'");
	return false;
}

bool Data::ParseTerm(const std::string& src, CondTerm* t)
{
	std::string s = Trim(src);
	t->type = TERM_TRUE;
	t->negate = false;
	t->flag = -1;
	t->op = OP_EQ;
	t->lhs.kind = t->rhs.kind = REF_NONE;

	if (s == "true")  { t->type = TERM_TRUE; return true; }
	if (s == "false") { t->type = TERM_FALSE; return true; }

	std::string body = s;
	if (!body.empty() && body[0] == '!' && (body.size() < 2 || body[1] != '='))
	{
		t->negate = true;
		body = Trim(body.substr(1));
	}

	// operator search: two-char operators first at each position
	size_t opPos = std::string::npos;
	int opLen = 0;
	for (size_t i = 0; i < body.size() && opPos == std::string::npos; ++i)
	{
		char c = body[i];
		char d = (i + 1 < body.size()) ? body[i + 1] : 0;
		if ((c == '<' || c == '>' || c == '=' || c == '!') && d == '=')
		{
			opPos = i; opLen = 2;
			t->op = (c == '<') ? OP_LE : (c == '>') ? OP_GE : (c == '=') ? OP_EQ : OP_NE;
		}
		else if (c == '<' || c == '>')
		{
			opPos = i; opLen = 1;
			t->op = (c == '<') ? OP_LT : OP_GT;
		}
	}

	if (opPos == std::string::npos)
	{
		if (StartsWith(body, "flag:"))
		{
			t->type = TERM_FLAG;
			t->flag = InternFlag(Trim(body.substr(5)));
			return true;
		}
		if (StartsWith(body, "fact:"))
		{
			t->type = TERM_STUB;
			++stubRefs;
			return true;
		}
		Fail(m_Line, "cannot read condition '" + s + "'");
		return false;
	}

	if (t->negate) { Fail(m_Line, "'!' only applies to flag: and fact:"); return false; }

	t->type = TERM_CMP;
	if (!ParseRef(body.substr(0, opPos), &t->lhs)) return false;
	if (!ParseRef(body.substr(opPos + opLen), &t->rhs)) return false;
	return true;
}

// "a && b || c" -> {{a, b}, {c}}. No parentheses by design (15.2).
bool Data::ParseCond(const std::string& src, Cond* out)
{
	out->ors.clear();
	std::string s = Trim(src);
	if (s.empty()) return true;

	size_t start = 0;
	for (;;)
	{
		size_t p = s.find("||", start);
		std::string part = s.substr(start, p == std::string::npos ? std::string::npos : p - start);

		std::vector<CondTerm> ands;
		size_t as = 0;
		for (;;)
		{
			size_t q = part.find("&&", as);
			CondTerm t;
			if (!ParseTerm(part.substr(as, q == std::string::npos ? std::string::npos : q - as), &t))
				return false;
			ands.push_back(t);
			if (q == std::string::npos) break;
			as = q + 2;
		}
		out->ors.push_back(ands);

		if (p == std::string::npos) break;
		start = p + 2;
	}
	return true;
}

// Split on ';' outside of braces
bool Data::ParseEffects(const std::string& src, EffectList* out)
{
	out->clear();
	std::string s = Trim(src);
	int depth = 0;
	size_t start = 0;
	for (size_t i = 0; i <= s.size(); ++i)
	{
		if (i < s.size() && s[i] == '{') ++depth;
		if (i < s.size() && s[i] == '}') --depth;
		if (i == s.size() || (s[i] == ';' && depth == 0))
		{
			std::string cmd = Trim(s.substr(start, i - start));
			if (!cmd.empty())
			{
				Effect fx;
				if (!ParseEffect(cmd, &fx)) return false;
				out->push_back(fx);
			}
			start = i + 1;
		}
	}
	if (depth != 0) { Fail(m_Line, "unbalanced '{' in effect"); return false; }
	return true;
}

bool Data::ParseEffect(const std::string& src, Effect* fx)
{
	fx->op = EF_STUB;
	fx->ref.kind = REF_NONE;
	fx->n = fx->a = fx->b = 0;
	fx->s.clear();
	fx->sub.clear();

	// words
	std::vector<std::string> w;
	{
		std::string cur;
		for (size_t i = 0; i <= src.size(); ++i)
		{
			if (i == src.size() || src[i] == ' ' || src[i] == '\t')
			{
				if (!cur.empty()) w.push_back(cur);
				cur.clear();
			}
			else cur += src[i];
		}
	}
	if (w.empty()) { Fail(m_Line, "empty effect"); return false; }
	const std::string& op = w[0];

	if (op == "add" || op == "set")
	{
		if (w.size() != 3) { Fail(m_Line, op + " <ref> <n>"); return false; }
		if (!ParseRef(w[1], &fx->ref)) return false;
		if (!ParseInt(w[2], &fx->n)) { Fail(m_Line, "number expected in '" + src + "'"); return false; }
		int k = fx->ref.kind;
		if (k == REF_STUB) { fx->op = EF_STUB; fx->s = src; return true; }
		bool writable = (k == REF_VAR || k == REF_REGION || k == REF_FACTION || k == REF_FREL
			|| k == REF_REL || (k == REF_NPC && fx->ref.field >= NF_AFFINITY
				&& fx->ref.field <= NF_MARK));
		if (!writable) { Fail(m_Line, "reference is read-only in '" + src + "'"); return false; }
		// writing "affinity" means the personal value; faction rep is separate
		if (k == REF_NPC && fx->ref.field == NF_AFFINITY) fx->ref.field = NF_RAW_AFFINITY;
		fx->op = (op == "add") ? EF_ADD : EF_SET;
		return true;
	}
	if (op == "flag" || op == "unflag")
	{
		if (w.size() < 2) { Fail(m_Line, op + " <name>"); return false; }
		fx->op = (op == "flag") ? EF_FLAG : EF_UNFLAG;
		fx->a = InternFlag(w[1]);
		if (op == "flag" && w.size() >= 3 && !ParseInt(w[2], &fx->n))
		{
			Fail(m_Line, "days expected in '" + src + "'");
			return false;
		}
		return true;
	}
	if (op == "event")
	{
		if (w.size() < 2) { Fail(m_Line, "event <id> [subject]"); return false; }
		fx->op = EF_EVENT;
		fx->a = FindEvent(w[1]);
		fx->b = -1;
		if (fx->a < 0) { Fail(m_Line, "unknown event '" + w[1] + "'"); return false; }
		if (w.size() >= 3)
		{
			fx->b = ParseNpcArg(w[2]);
			if (fx->b == -1) { Fail(m_Line, "unknown npc '" + w[2] + "'"); return false; }
		}
		return true;
	}
	if (op == "log")
	{
		fx->op = EF_LOG;
		fx->s = Trim(src.substr(3));
		return true;
	}
	if (op == "notify")
	{
		if (w.size() != 2) { Fail(m_Line, "notify <noticeId>"); return false; }
		fx->op = EF_NOTIFY;
		fx->a = FindNotice(w[1]);
		if (fx->a < 0) { Fail(m_Line, "unknown notice '" + w[1] + "'"); return false; }
		return true;
	}
	if (op == "join" || op == "move")
	{
		if (w.size() != 3) { Fail(m_Line, op + " <npc> <" + (op == "join" ? "faction>" : "region>")); return false; }
		fx->op = (op == "join") ? EF_JOIN : EF_MOVE;
		fx->a = ParseNpcArg(w[1]);
		fx->b = (op == "join") ? FindFaction(w[2]) : FindRegion(w[2]);
		if (fx->a == -1) { Fail(m_Line, "unknown npc '" + w[1] + "'"); return false; }
		if (fx->b < 0) { Fail(m_Line, "unknown id '" + w[2] + "'"); return false; }
		return true;
	}
	if (op == "kill" || op == "capture" || op == "free")
	{
		if (w.size() != 2) { Fail(m_Line, op + " <npc>"); return false; }
		fx->op = (op == "kill") ? EF_KILL : (op == "capture") ? EF_CAPTURE : EF_FREE;
		fx->a = ParseNpcArg(w[1]);
		if (fx->a == -1) { Fail(m_Line, "unknown npc '" + w[1] + "'"); return false; }
		return true;
	}
	if (op == "chance")
	{
		size_t ob = src.find('{');
		size_t cb = src.rfind('}');
		if (w.size() < 2 || ob == std::string::npos || cb == std::string::npos || cb < ob
			|| !ParseInt(w[1], &fx->n))
		{
			Fail(m_Line, "chance <pct> { <effects> }");
			return false;
		}
		fx->op = EF_CHANCE;
		return ParseEffects(src.substr(ob + 1, cb - ob - 1), &fx->sub);
	}
	if (op == "recruit_hostile")
	{
		fx->op = EF_RECRUIT_HOSTILE;
		return true;
	}
	if (op == "take" || op == "plan" || op == "reveal" || op == "move_player")
	{
		fx->op = EF_STUB;
		fx->s = src;
		++stubRefs;
		return true;
	}

	Fail(m_Line, "unknown effect '" + op + "'");
	return false;
}

} // namespace Sim
