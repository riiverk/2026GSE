#pragma once

class JsonWriter;

// -----------------------------------------------------------------------------
// Sim monitor output - the strategy-layer state as JSON for Tools/monitor.html
//
// Read-only (STORY_SIM.md 0.1): writes members into the JSON object the caller
// has open - clock, vars, flags, regions, factions, npcs, notices. Indices in
// the output are the data-file definition order, so the page can join NPCs to
// regions and factions without string lookups.
//
// Screen-side facts (positions on a map, mobs) are not Sim state and are
// written by the scene that owns them (Game / Level1 WriteMonitorMap).
// -----------------------------------------------------------------------------
namespace Sim
{
	class World;

	void WriteMonitorJson(const World& world, JsonWriter& w);
}
