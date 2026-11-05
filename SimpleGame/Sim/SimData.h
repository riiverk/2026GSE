#pragma once

#include <string>
#include <vector>

#include "SimTypes.h"
#include "SimGgd.h"

// -----------------------------------------------------------------------------
// Sim::Data - loads Data/Sim/*.ggd, resolves ids to indices, compiles
// conditions and effects (STORY_SIM.md 15.2, 15.3) and validates.
//
// Currently loaded: world, factions, events, actions, npcs. Conditions may
// already mention plan: / legend: / fact:; those compile to stubs that read 0
// (reported once at load) until the plan and legend stages exist.
// -----------------------------------------------------------------------------
namespace Sim
{
	class Data
	{
	public:
		Data();

		bool Load(const char* dataDir);
		const std::string& GetError() const { return m_Err; }

		int FindVar(const std::string& id) const;
		int FindRegion(const std::string& id) const;
		int FindFaction(const std::string& id) const;
		int FindNpc(const std::string& id) const;
		int FindAction(const std::string& id) const;
		int FindEvent(const std::string& id) const;
		int FindNotice(const std::string& id) const;
		int FindFlag(const std::string& id) const;

		std::vector<VarDef>     vars;
		std::vector<RegionDef>  regions;
		std::vector<FactionDef> factions;
		std::vector<NoticeDef>  notices;
		std::vector<EventDef>   events;
		std::vector<ActionDef>  actions;
		std::vector<NpcDef>     npcs;
		std::vector<ChoiceDef>  globalChoices;
		std::vector<std::string> flags;         // registered on first mention
		std::vector<int>        frelInit;       // factions x factions, symmetric
		int                     stubRefs;       // plan/legend/fact mentions

	private:
		// id registration (pass 1)
		bool Register(std::vector<std::string>& ids, const GgdBlock& b);

		void Build(const GgdFile& f);
		void BuildVar(const GgdBlock& b);
		void BuildRegion(const GgdBlock& b);
		void BuildNotice(const GgdBlock& b);
		void BuildFaction(const GgdBlock& b);
		void BuildFrel(const GgdBlock& b);
		void BuildEvent(const GgdBlock& b);
		void BuildAction(const GgdBlock& b);
		void BuildNpc(const GgdBlock& b);
		void BuildGlobalChoice(const GgdBlock& b);

		// compilation (pass 2)
		bool ParseRef(const std::string& s, Ref* out);
		bool ParseCond(const std::string& s, Cond* out);
		bool ParseTerm(const std::string& s, CondTerm* out);
		bool ParseEffects(const std::string& s, EffectList* out);
		bool ParseEffect(const std::string& s, Effect* out);
		int  ParseNpcArg(const std::string& s);
		int  InternFlag(const std::string& name);
		bool ParseInt(const std::string& s, int* out);

		void Fail(int line, const std::string& msg);
		void Warn(int line, const std::string& msg);

		std::vector<std::string> m_VarIds, m_RegionIds, m_FactionIds, m_NpcIds,
			m_ActionIds, m_EventIds, m_NoticeIds, m_ChoiceIds;

		std::string m_File;
		int         m_Line;
		std::string m_Err;
		bool        m_Failed;
	};
}
