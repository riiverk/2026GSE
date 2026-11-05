#pragma once

#include <string>
#include <vector>
#include <cstdint>

#include "SimTypes.h"
#include "SimData.h"

// -----------------------------------------------------------------------------
// Sim::World - clock, world state, daily strategy tick (STORY_SIM.md 4, 7, 8, 10)
//
// Implemented now: clock and segments, timed flags, RNG, NPC action choice
// and effects, player events with witnesses, bands and greetings,
// recruit_hostile / join / move / capture.
// Not yet: plans (9), rumors (11), facts (12), legends (13), ending (14), save.
// The DailyTick order already reserves their slots.
//
// The screen side reads state and reports player actions. It never writes
// NpcState directly.
// -----------------------------------------------------------------------------
namespace Sim
{
	class Rng
	{
	public:
		Rng() : m_S(0x9E3779B97F4A7C15ull) {}
		void     Seed(uint64_t s) { m_S = s ? s : 0x9E3779B97F4A7C15ull; }
		uint64_t Next();
		bool     Chance(int pct);
		int      Range(int lo, int hi);          // [lo, hi]
		int      Jitter(int value, int pct);     // value +- |value| * pct%
		uint64_t GetState() const { return m_S; }

	private:
		uint64_t m_S;
	};

	// Where is this NPC on the current map? The game registers this so Sim
	// can pick witnesses without knowing about Actors (16.2).
	typedef bool (*NpcLocator)(int npcIdx, float* wx, float* wy, void* user);

	class World
	{
	public:
		World();

		bool Load(const char* dataDir, uint64_t seed);
		bool IsLoaded() const { return m_Loaded; }

		void Update(float dt);
		void SetPaused(bool paused) { m_Paused = paused; }
		bool IsPaused() const { return m_Paused; }
		void SetDayLength(float seconds);       // tests shorten the day
		float GetDayLength() const { return m_DayLength; }
		void SkipToNextDawn();
		void DebugAdvanceDays(int days);
		void DebugAdvanceSegment();

		// ---- read -------------------------------------------------------------
		int    GetDay() const { return m_Day; }
		int    GetSegment() const;
		float  GetSegmentProgress() const;      // 0..1 inside the segment
		float  GetDayProgress() const;          // 0..1 inside the day
		Season GetSeason() const { return (Season)((m_Day / SIM_SEASON_DAYS) % 4); }
		int    GetDayOfSeason() const { return m_Day % SIM_SEASON_DAYS + 1; }

		const Data& GetData() const { return m_Data; }
		int  GetNpcCount() const { return (int)m_Npcs.size(); }
		const NpcDef&   GetNpcDef(int idx) const { return m_Data.npcs[idx]; }
		const NpcState& GetNpcState(int idx) const { return m_Npcs[idx]; }
		int  GetEffAffinity(int npcIdx) const;
		Band GetBand(int npcIdx) const;
		const char* GetGreeting(int npcIdx) const;
		const ActionDef* GetCurrentAction(int npcIdx) const;   // NULL before the first tick

		int  GetVar(const char* id) const;
		bool GetFlag(const char* id) const;
		int  GetRegionValue(int region, int field) const;
		int  GetFactionRep(int faction) const;
		int  GetFactionTreasury(int faction) const
		{
			return (faction >= 0) ? m_FacTreasury[faction] : 0;
		}
		const std::vector<Notice>& GetNotices() const { return m_Notices; }
		// Total notices ever pushed. The list keeps only the last 16, so the
		// screen side compares serials to find the new ones.
		int  GetNoticeSerial() const { return m_NoticeSerial; }

		// ---- player -------------------------------------------------------------
		void SetPlayerRegion(int region) { m_PlayerRegion = region; }
		void SetNpcLocator(NpcLocator fn, void* user) { m_Locator = fn; m_LocatorUser = user; }

		// Returns how many NPCs reacted (witnesses, or 1 for victim-only events)
		int  OnPlayerEvent(const char* eventId, float wx, float wy, int victimNpc = -1);

		void DumpState() const;

	private:
		struct Ctx
		{
			int self;
			int victim;
		};

		void DailyTick();
		void ExpireFlags();
		void RunNpcActions();
		void ApplyEvent(int evIdx, int npcIdx, int victimNpc, int scalePct);
		void ApplyEventGlobals(int evIdx, int victimNpc);

		int  ReadRef(const Ref& r, const Ctx& c) const;
		void WriteRef(const Ref& r, int value, const Ctx& c);
		bool EvalCond(const Cond& cond, const Ctx& c) const;
		void Run(const EffectList& fx, const Ctx& c);
		int  ResolveNpc(int idx, const Ctx& c) const;
		int  ResolveFaction(int idx, const Ctx& c) const;
		int  ResolveRegion(int idx, const Ctx& c) const;

		void Join(int npc, int faction);
		void Notify(int notice, int region);

		Data  m_Data;
		Rng   m_Rng;
		bool  m_Loaded;
		bool  m_Paused;
		float m_DayLength;
		float m_T;           // seconds into the current day
		int   m_Day;
		int   m_TickedDay;   // last day DailyTick ran for

		std::vector<int>       m_Vars;
		std::vector<int>       m_Region;          // regions x RF_COUNT
		std::vector<int>       m_FacRep;
		std::vector<int>       m_FacTreasury;
		std::vector<int>       m_Frel;            // factions x factions
		std::vector<NpcState>  m_Npcs;
		std::vector<char>      m_Flags;
		std::vector<int>       m_FlagExpire;      // day to clear, -1 permanent
		std::vector<Notice>    m_Notices;
		int                    m_NoticeSerial;

		int        m_PlayerRegion;
		NpcLocator m_Locator;
		void*      m_LocatorUser;
	};
}
