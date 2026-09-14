#pragma once

#include <map>
#include <string>

#include "Dependencies\glew.h"

// -----------------------------------------------------------------------------
// Korean text rendering.
//
// GLUT's bitmap fonts are ASCII only, which is why the first prototype was in
// English. This renders whole strings with GDI into an off-screen DIB, uploads
// the result as an alpha texture, and caches it by string.
//
// Whole strings rather than per-glyph atlases: Hangul is composed of 11,172
// possible syllables, so a glyph atlas is the wrong trade for a prototype. The
// dialogue and UI use a bounded set of strings, so the cache stays small.
// -----------------------------------------------------------------------------
class KoreanFont
{
public:
	KoreanFont();
	~KoreanFont();

	bool Create(int pixelHeight, bool bold);
	void Destroy();

	// Returns a cached texture for the string, building it on first use.
	GLuint GetTexture(const wchar_t* text, int* outW, int* outH);

	int  Measure(const wchar_t* text);
	int  Height() const { return m_Height; }
	bool IsReady() const { return m_Font != 0; }

private:
	struct Item
	{
		GLuint tex;
		int    w, h;
	};

	GLuint Build(const wchar_t* text, int* outW, int* outH);

	void* m_Font;      // HFONT, kept opaque so windows.h stays out of headers
	int   m_Height;

	std::map<std::wstring, Item> m_Cache;
};
