#pragma once

class Renderer;
class JsonWriter;

// -----------------------------------------------------------------------------
// Tutorial level. SimpleGame.cpp owns the window and the input plumbing; every
// piece of world state, simulation and drawing lives here.
// -----------------------------------------------------------------------------
namespace Game
{
	void Init(Renderer* renderer, int screenW, int screenH);
	void Reset();

	void Update(float dt);
	void Render();

	void SetKey(unsigned char key, bool down);
	void Interact();

	// Thanks ritual right after a catch (G). Villagers who see it react.
	void Gratitude();

	// GLUT special key: F5 resident panel, F6 next day, F7 dump, F8 next segment
	void SpecialKey(int key);

	// Monitor (Tools/monitor.html). Read-only.
	// WriteMonitorSim writes "sim": strategy state (null if data failed to load).
	// WriteMonitorMap appends this map's object to the open "maps" array:
	// residents' bodies on the map and the mob counts.
	void WriteMonitorSim(JsonWriter& w);
	void WriteMonitorMap(JsonWriter& w, bool active);
}
