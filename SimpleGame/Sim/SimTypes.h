#pragma once

#include <string>
#include <vector>
#include <cstdint>

// -----------------------------------------------------------------------------
// Sim - shared types (STORY_SIM.md 7.1, 15.2, 15.3)
//
// Pure logic. Nothing here may include GL, Renderer, Actor or windows.h.
// All text is UTF-8 std::string; the screen side converts when it draws.
// -----------------------------------------------------------------------------
namespace Sim
{
	// ---- clock (4.1) --------------------------------------------------------
	static const float SIM_DAY_SECONDS  = 1200.0f;
	static const int   SIM_SEGMENTS     = 4;       // dawn, day, evening, night
	static const int   SIM_SEASON_DAYS  = 28;
	static const int   SIM_ECLIPSE_DAY  = 111;

	enum Segment { SEG_DAWN = 0, SEG_DAY, SEG_EVENING, SEG_NIGHT };
	enum Season  { SEASON_SUMMER = 0, SEASON_AUTUMN, SEASON_WINTER, SEASON_SPRING };

	// ---- relationship bands (7.2) --------------------------------------------
	enum Band
	{
		BAND_HOSTILE  = -2,
		BAND_WARY     = -1,
		BAND_NEUTRAL  =  0,
		BAND_FRIENDLY =  1,
		BAND_ALLY     =  2
	};

	static const int IDLE_THRESHOLD  = 5;
	static const float WITNESS_RADIUS = 8.0f;   // tiles

	// ---- references (15.2) ---------------------------------------------------
	enum RefKind
	{
		REF_NONE = 0,
		REF_CONST,
		REF_DAY,
		REF_SEASON,
		REF_SEGMENT,
		REF_VAR,
		REF_REGION,
		REF_NPC,
		REF_FACTION,
		REF_FREL,
		REF_REL,
		REF_STUB     // plan: / legend: / fact: - systems not loaded yet, reads 0
	};

	// Placeholders resolved at run time from the acting NPC (10, "@self" etc.)
	static const int IDX_SELF           = -2;
	static const int IDX_SELF_REGION    = -3;
	static const int IDX_SELF_FACTION   = -4;
	static const int IDX_VICTIM_FACTION = -5;

	enum NpcField
	{
		NF_AFFINITY = 0,   // effective (with faction rep)
		NF_RAW_AFFINITY,
		NF_TRUST,
		NF_POWER,
		NF_METER,
		NF_MARK,
		NF_ALIVE,
		NF_CAPTURED,
		NF_BAND,
		NF_BLACK
	};

	enum RegionField { RF_PROSPERITY = 0, RF_BLACK, RF_UNREST, RF_SECURITY, RF_COUNT };
	enum FactionField { FF_REP = 0, FF_TREASURY };

	struct Ref
	{
		int kind;
		int a;       // var / region / npc / faction index, or const value
		int b;       // second index for frel / rel
		int field;
	};

	enum CmpOp { OP_LT = 0, OP_LE, OP_GT, OP_GE, OP_EQ, OP_NE };

	enum TermType
	{
		TERM_TRUE = 0,
		TERM_FALSE,
		TERM_FLAG,     // a = flag index
		TERM_CMP,
		TERM_STUB      // fact:<id> - always false until facts exist
	};

	struct CondTerm
	{
		int  type;
		bool negate;
		int  flag;
		Ref  lhs;
		int  op;
		Ref  rhs;
	};

	// OR of AND groups. Empty = true.
	struct Cond
	{
		std::vector<std::vector<CondTerm> > ors;
	};

	enum EffectOp
	{
		EF_ADD = 0,
		EF_SET,
		EF_FLAG,        // a = flag, n = days (0 = permanent)
		EF_UNFLAG,
		EF_EVENT,       // a = event, b = subject npc (-1 = player)
		EF_LOG,         // s
		EF_NOTIFY,      // a = notice
		EF_JOIN,        // a = npc, b = faction
		EF_MOVE,        // a = npc, b = region
		EF_KILL,
		EF_CAPTURE,
		EF_FREE,
		EF_CHANCE,      // n = pct, sub
		EF_RECRUIT_HOSTILE,
		EF_STUB         // take / plan / reveal / move_player - later stages
	};

	struct Effect
	{
		int  op;
		Ref  ref;
		int  n;
		int  a;
		int  b;
		std::string s;
		std::vector<Effect> sub;
	};

	typedef std::vector<Effect> EffectList;

	// ---- definitions (fixed, from data files) --------------------------------
	struct Traits
	{
		int greed;       // - restraint / + greed
		int principle;   // - pragmatic / + principled
		int bold;        // - cautious  / + bold
	};

	struct VarDef
	{
		std::string id;
		int init, minV, maxV;
	};

	struct RegionDef
	{
		std::string id;
		std::string name;
		int  init[RF_COUNT];
		std::string map;
		bool closed;
	};

	struct FactionDef
	{
		std::string id;
		std::string name;
		int rep;
		int treasury;
	};

	struct NoticeDef
	{
		std::string id;
		std::string text;
	};

	struct EventDef
	{
		std::string id;
		int delta, wGreed, wPrinciple, wBold;
		int trust;
		int fame;
		std::vector<std::pair<int, int> > factionDelta;   // faction idx (or IDX_VICTIM_FACTION)
		bool toVictim;   // applies to the victim NPC only, not to witnesses
	};

	struct ActionDef
	{
		std::string id;
		std::string name;       // display name (UTF-8)
		std::string behavior;   // hint for the screen-side daily layer
		std::string goal;
		int base, wGreed, wPrinciple, wBold;
		Cond cond;
		EffectList effect;
		int eventIdx;
	};

	struct ChoiceDef
	{
		std::string id;
		std::string text;
		Cond cond;
		EffectList effect;
	};

	struct NpcDef
	{
		std::string id;
		std::string name;
		std::string title;
		int factionIdx;
		Traits traits;
		std::string goal;
		int homeRegion;
		std::vector<std::string> likes, dislikes;
		std::vector<int> actions;
		std::vector<std::string> schedule;               // map location id per segment
		std::vector<std::pair<int, int> > relInit;       // npc idx, value
		int initAffinity, initTrust, initPower, initMeter;

		std::string greet[5];                            // index = band + 2
		std::vector<std::pair<std::string, std::string> > asks;   // fact id, text
		std::vector<ChoiceDef> choices;
		EffectList onServed;
		std::vector<std::pair<int, EffectList> > reacts; // event idx, script

		// screen hints (not in 7.1; used by the map that spawns the NPC)
		bool  hasCloth;
		float cloth[3];
		std::string look;
		std::vector<std::pair<std::string, std::string> > barks;   // behavior, line
	};

	// ---- state (mutable, save target) ----------------------------------------
	struct NpcState
	{
		int  affinity;
		int  trust;
		int  power;
		int  meter;
		int  mark;
		int  factionIdx;
		bool alive;
		bool captured;
		int  region;
		int  lastAction;      // action index, -1 = none yet
		int  lastScore;
		int  lastServedDay;
		std::vector<int> rel;
	};

	struct Notice
	{
		int day;
		std::string text;
	};
}
