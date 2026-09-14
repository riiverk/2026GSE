#pragma once

class Renderer;

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
}
