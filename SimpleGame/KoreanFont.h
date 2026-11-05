#pragma once

#include <map>

#include "Dependencies\glew.h"

// -----------------------------------------------------------------------------
// Glyph atlas + Korean text
//
// Every glyph of every font size lives in ONE single-channel texture. That is
// what lets text share the shape batch: a label is just N more instances that
// sample the same atlas, so drawing text never forces a separate draw call.
//
// The previous design cached whole strings as their own textures. Every new
// string (damage numbers, a changing HP value) cost a GDI render and a texture
// upload in the middle of a frame, and the cache never shrank. Per-glyph
// caching is bounded by the set of characters actually used, so after warm-up
// there are no uploads at all.
//
// Glyphs are still rendered by GDI (Hangul needs a real font rasteriser).
// Layout is simple advance-based; there is no kerning, which Hangul does not
// need and Latin UI text barely notices.
// -----------------------------------------------------------------------------

struct GlyphInfo
{
	float u0, v0, u1, v1;   // atlas UVs of the whole cell (glyph + padding)
	int   w, h;             // cell size in pixels
	int   advance;          // pen advance in pixels
};

class GlyphAtlas
{
public:
	GlyphAtlas();
	~GlyphAtlas();

	bool   Create(int size);
	void   Destroy();

	GLuint Texture() const { return m_Tex; }
	int    Size() const { return m_Size; }

	// Copies an alpha bitmap into free space. False when the atlas is full.
	bool   Insert(const unsigned char* alpha, int w, int h, GlyphInfo* io);

	// Throws everything away. Fonts notice through Generation() and rebuild
	// their glyphs lazily.
	void   Clear();

	unsigned int Generation() const { return m_Generation; }
	float  UsedPct() const;

private:
	GLuint       m_Tex;
	int          m_Size;
	int          m_PenX, m_PenY, m_RowH;
	unsigned int m_Generation;
};

class KoreanFont
{
public:
	KoreanFont();
	~KoreanFont();

	bool Create(int pixelHeight, bool bold, GlyphAtlas* atlas);
	void Destroy();

	// Builds the glyph on first use. Never NULL once the font is ready.
	const GlyphInfo* Glyph(wchar_t ch);

	// Width of the whole label box, padding included, as the old whole-string
	// texture used to report.
	int  Measure(const wchar_t* text);
	int  Height() const { return m_Height; }
	int  CellHeight() const { return m_CellH; }
	int  Pad() const { return PAD; }
	bool IsReady() const { return m_Font != 0; }
	int  GlyphCount() const { return (int)m_Glyphs.size(); }

private:
	static const int PAD = 2;

	bool Build(wchar_t ch, GlyphInfo* out);

	// Win32 handles kept opaque so windows.h stays out of headers.
	void* m_Font;     // HFONT
	void* m_DC;       // HDC with the font and DIB selected
	void* m_Bmp;      // HBITMAP (DIB section)
	void* m_OldBmp;
	void* m_OldFont;
	void* m_Bits;
	int   m_BmpW;
	int   m_BmpH;

	int   m_Height;
	int   m_CellH;

	GlyphAtlas*  m_Atlas;
	unsigned int m_Generation;

	std::map<wchar_t, GlyphInfo> m_Glyphs;
};
