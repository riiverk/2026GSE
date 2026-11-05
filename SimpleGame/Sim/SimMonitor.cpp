#include "SimMonitor.h"

#include "SimWorld.h"
#include "../JsonWriter.h"

namespace Sim
{
	static const char* const SEASON_IDS[4] = { "summer", "autumn", "winter", "spring" };
	static const char* const SEGMENT_IDS[SIM_SEGMENTS] = { "dawn", "day", "evening", "night" };
	static const char* const REGION_FIELD_IDS[RF_COUNT] =
		{ "prosperity", "black", "unrest", "security" };

	static void WriteIdOrNull(JsonWriter& w, const char* key,
		const std::vector<RegionDef>& list, int idx)
	{
		if (idx >= 0 && idx < (int)list.size()) w.Str(key, list[idx].id);
		else w.Null(key);
	}

	void WriteMonitorJson(const World& world, JsonWriter& w)
	{
		const Data& d = world.GetData();
		int day = world.GetDay();
		int seg = world.GetSegment();
		int season = (int)world.GetSeason();

		// ---- clock ----
		w.BeginObject("clock");
		w.Int("day", day);
		w.Int("year", day / (SIM_SEASON_DAYS * 4) + 1);
		w.Int("season", season);
		w.Str("seasonId", SEASON_IDS[season]);
		w.Int("dayOfSeason", world.GetDayOfSeason());
		w.Int("segment", seg);
		w.Str("segmentId", SEGMENT_IDS[seg]);
		w.Num("segmentProgress", world.GetSegmentProgress(), 3);
		w.Num("dayProgress", world.GetDayProgress(), 3);
		w.Num("dayLength", world.GetDayLength(), 1);
		w.Bool("paused", world.IsPaused());
		w.Int("eclipseDay", SIM_ECLIPSE_DAY);
		w.EndObject();

		// ---- globals ----
		w.BeginObject("vars");
		for (size_t i = 0; i < d.vars.size(); ++i)
			w.Int(d.vars[i].id.c_str(), world.GetVar(d.vars[i].id.c_str()));
		w.EndObject();

		w.BeginArray("flags");   // only the ones that are on
		for (size_t i = 0; i < d.flags.size(); ++i)
			if (world.GetFlag(d.flags[i].c_str())) w.Str(0, d.flags[i]);
		w.EndArray();

		// ---- regions (5.2) ----
		w.BeginArray("regions");
		for (size_t i = 0; i < d.regions.size(); ++i)
		{
			const RegionDef& r = d.regions[i];
			w.BeginObject();
			w.Int("i", (int)i);
			w.Str("id", r.id);
			w.Str("name", r.name);
			if (r.map.empty()) w.Null("map");
			else w.Str("map", r.map);
			w.Bool("closed", r.closed);
			for (int f = 0; f < RF_COUNT; ++f)
				w.Int(REGION_FIELD_IDS[f], world.GetRegionValue((int)i, f));
			w.EndObject();
		}
		w.EndArray();

		// ---- factions (6) ----
		w.BeginArray("factions");
		for (size_t i = 0; i < d.factions.size(); ++i)
		{
			const FactionDef& f = d.factions[i];
			w.BeginObject();
			w.Int("i", (int)i);
			w.Str("id", f.id);
			w.Str("name", f.name);
			w.Int("rep", world.GetFactionRep((int)i));
			w.Int("treasury", world.GetFactionTreasury((int)i));
			w.EndObject();
		}
		w.EndArray();

		// ---- npcs (7) ----
		w.BeginArray("npcs");
		for (int i = 0; i < world.GetNpcCount(); ++i)
		{
			const NpcDef& def = world.GetNpcDef(i);
			const NpcState& st = world.GetNpcState(i);
			const ActionDef* act = world.GetCurrentAction(i);

			w.BeginObject();
			w.Int("i", i);
			w.Str("id", def.id);
			w.Str("name", def.name);
			w.Str("title", def.title);
			w.Str("goal", def.goal);

			if (st.factionIdx >= 0 && st.factionIdx < (int)d.factions.size())
				w.Str("faction", d.factions[st.factionIdx].id);
			else w.Null("faction");
			WriteIdOrNull(w, "home", d.regions, def.homeRegion);
			WriteIdOrNull(w, "region", d.regions, st.region);

			w.Bool("alive", st.alive);
			w.Bool("captured", st.captured);
			w.Int("band", (int)world.GetBand(i));
			w.Int("affinity", world.GetEffAffinity(i));
			w.Int("rawAffinity", st.affinity);
			w.Int("trust", st.trust);
			w.Int("power", st.power);
			w.Int("meter", st.meter);
			w.Int("mark", st.mark);

			w.BeginArray("traits");   // greed, principle, bold
			w.Int(0, def.traits.greed);
			w.Int(0, def.traits.principle);
			w.Int(0, def.traits.bold);
			w.EndArray();

			if (act)
			{
				w.Str("action", act->id);
				w.Str("actionName", act->name.empty() ? act->id : act->name);
				w.Str("behavior", act->behavior);
				w.Int("score", st.lastScore);
			}
			else
			{
				w.Null("action");
				w.Null("actionName");
				w.Null("behavior");
				w.Null("score");
			}
			w.EndObject();
		}
		w.EndArray();

		// ---- notices: the page dedups by serial ----
		const std::vector<Notice>& notices = world.GetNotices();
		w.BeginObject("notices");
		w.Int("serial", world.GetNoticeSerial());
		w.BeginArray("list");
		for (size_t i = 0; i < notices.size(); ++i)
		{
			w.BeginObject();
			w.Int("day", notices[i].day);
			w.Str("text", notices[i].text);
			w.EndObject();
		}
		w.EndArray();
		w.EndObject();
	}
}
