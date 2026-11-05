#include "stdafx.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <vector>
#include <cstring>

#include "KoreanFont.h"
#include "Profiler.h"

// "Malgun Gothic" in Hangul, written as separated escapes so this file does not
// depend on its own encoding being interpreted correctly by the compiler.
static const wchar_t* kFontFace = L"\xB9D1" L"\xC740" L" " L"\xACE0" L"\xB515";

// One texel of empty space around every cell so bilinear filtering never picks
// up a neighbour.
static const int GUTTER = 1;

// =============================================================================
// GlyphAtlas
// =============================================================================

GlyphAtlas::GlyphAtlas()
	: m_Tex(0), m_Size(0), m_PenX(0), m_PenY(0), m_RowH(0), m_Generation(0)
{
}

GlyphAtlas::~GlyphAtlas()
{
	Destroy();
}

bool GlyphAtlas::Create(int size)
{
	Destroy();
	m_Size = size;

	// Start from zero so gutters really are empty.
	std::vector<unsigned char> zero((size_t)size * size, 0);

	glGenTextures(1, &m_Tex);
	glBindTexture(GL_TEXTURE_2D, m_Tex);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, size, size, 0, GL_RED, GL_UNSIGNED_BYTE, &zero[0]);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

	m_PenX = GUTTER;
	m_PenY = GUTTER;
	m_RowH = 0;
	return m_Tex != 0;
}

void GlyphAtlas::Destroy()
{
	if (m_Tex) glDeleteTextures(1, &m_Tex);
	m_Tex = 0;
	m_Size = 0;
}

void GlyphAtlas::Clear()
{
	m_PenX = GUTTER;
	m_PenY = GUTTER;
	m_RowH = 0;
	++m_Generation;

	std::vector<unsigned char> zero((size_t)m_Size * m_Size, 0);
	glBindTexture(GL_TEXTURE_2D, m_Tex);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_Size, m_Size, GL_RED, GL_UNSIGNED_BYTE, &zero[0]);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glBindTexture(GL_TEXTURE_2D, 0);

	PROFILE_COUNT("text.atlas_resets", 1);
	Profiler::Event("text.atlas_reset", "\"reason\":\"full\"");
}

bool GlyphAtlas::Insert(const unsigned char* alpha, int w, int h, GlyphInfo* io)
{
	if (m_Tex == 0 || w <= 0 || h <= 0) return false;
	if (w + GUTTER * 2 > m_Size || h + GUTTER * 2 > m_Size) return false;

	// Shelf packing: fill a row left to right, start a new row when it runs out.
	if (m_PenX + w + GUTTER > m_Size)
	{
		m_PenX = GUTTER;
		m_PenY += m_RowH + GUTTER;
		m_RowH = 0;
	}
	if (m_PenY + h + GUTTER > m_Size) return false;

	glBindTexture(GL_TEXTURE_2D, m_Tex);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexSubImage2D(GL_TEXTURE_2D, 0, m_PenX, m_PenY, w, h, GL_RED, GL_UNSIGNED_BYTE, alpha);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glBindTexture(GL_TEXTURE_2D, 0);

	float inv = 1.0f / (float)m_Size;
	io->u0 = m_PenX * inv;
	io->v0 = m_PenY * inv;
	io->u1 = (m_PenX + w) * inv;
	io->v1 = (m_PenY + h) * inv;

	m_PenX += w + GUTTER;
	if (h > m_RowH) m_RowH = h;

	PROFILE_COUNT("text.upload_bytes", w * h);
	return true;
}

float GlyphAtlas::UsedPct() const
{
	if (m_Size <= 0) return 0.0f;
	// Rows are consumed top to bottom, so the pen height is a fair measure.
	float used = (float)(m_PenY + m_RowH) / (float)m_Size;
	return used * 100.0f;
}

// =============================================================================
// KoreanFont
// =============================================================================

KoreanFont::KoreanFont()
	: m_Font(0), m_DC(0), m_Bmp(0), m_OldBmp(0), m_OldFont(0), m_Bits(0),
	m_BmpW(0), m_BmpH(0), m_Height(0), m_CellH(0), m_Atlas(0), m_Generation(0)
{
}

KoreanFont::~KoreanFont()
{
	Destroy();
}

bool KoreanFont::Create(int pixelHeight, bool bold, GlyphAtlas* atlas)
{
	Destroy();

	m_Height = pixelHeight;
	m_Atlas = atlas;
	m_Generation = atlas ? atlas->Generation() : 0;

	HFONT f = CreateFontW(
		-pixelHeight,                 // negative = character height in pixels
		0, 0, 0,
		bold ? FW_SEMIBOLD : FW_NORMAL,
		FALSE, FALSE, FALSE,
		HANGEUL_CHARSET,
		OUT_TT_PRECIS,
		CLIP_DEFAULT_PRECIS,
		ANTIALIASED_QUALITY,          // grayscale AA, not ClearType: we want a
		VARIABLE_PITCH | FF_DONTCARE, // clean single-channel alpha
		kFontFace);
	if (f == NULL) return false;
	m_Font = (void*)f;

	// One DC and one DIB for the lifetime of the font. Creating them per glyph
	// is most of what made the old whole-string path slow.
	HDC dc = CreateCompatibleDC(NULL);
	if (dc == NULL) { Destroy(); return false; }
	m_DC = (void*)dc;
	m_OldFont = (void*)SelectObject(dc, f);

	TEXTMETRICW tm;
	GetTextMetricsW(dc, &tm);
	m_CellH = tm.tmHeight + PAD * 2;

	m_BmpW = pixelHeight * 3 + PAD * 2;
	m_BmpH = m_CellH;

	BITMAPINFO bi;
	ZeroMemory(&bi, sizeof(bi));
	bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth       = m_BmpW;
	bi.bmiHeader.biHeight      = -m_BmpH;   // top-down rows
	bi.bmiHeader.biPlanes      = 1;
	bi.bmiHeader.biBitCount    = 32;
	bi.bmiHeader.biCompression = BI_RGB;

	void* bits = NULL;
	HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
	if (bmp == NULL || bits == NULL) { Destroy(); return false; }
	m_Bmp = (void*)bmp;
	m_Bits = bits;
	m_OldBmp = (void*)SelectObject(dc, bmp);

	SetBkMode(dc, TRANSPARENT);
	SetTextColor(dc, RGB(255, 255, 255));
	return true;
}

void KoreanFont::Destroy()
{
	HDC dc = (HDC)m_DC;
	if (dc)
	{
		if (m_OldBmp)  SelectObject(dc, (HBITMAP)m_OldBmp);
		if (m_OldFont) SelectObject(dc, (HFONT)m_OldFont);
		DeleteDC(dc);
	}
	if (m_Bmp)  DeleteObject((HBITMAP)m_Bmp);
	if (m_Font) DeleteObject((HFONT)m_Font);

	m_DC = 0;
	m_Bmp = 0;
	m_OldBmp = 0;
	m_OldFont = 0;
	m_Bits = 0;
	m_Font = 0;
	m_Glyphs.clear();
}

bool KoreanFont::Build(wchar_t ch, GlyphInfo* out)
{
	PROFILE_SCOPE("text.glyph_build");
	PROFILE_COUNT("text.glyph_builds", 1);

	HDC dc = (HDC)m_DC;

	SIZE sz;
	sz.cx = 0;
	sz.cy = 0;
	GetTextExtentPoint32W(dc, &ch, 1, &sz);

	int w = sz.cx + PAD * 2;
	int h = m_CellH;
	if (w > m_BmpW) w = m_BmpW;
	if (w < 1) w = 1;

	out->w = w;
	out->h = h;
	out->advance = sz.cx;
	out->u0 = out->v0 = out->u1 = out->v1 = 0.0f;

	// Whitespace: advance only, nothing to store.
	if (ch == L' ' || ch == L'\t' || sz.cx == 0) return true;

	RECT rc;
	rc.left = 0; rc.top = 0; rc.right = w; rc.bottom = h;
	HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
	FillRect(dc, &rc, black);
	TextOutW(dc, PAD, PAD, &ch, 1);
	GdiFlush();

	std::vector<unsigned char> alpha((size_t)w * h);
	const unsigned char* src = (const unsigned char*)m_Bits;
	for (int y = 0; y < h; ++y)
	{
		const unsigned char* row = src + (size_t)y * m_BmpW * 4;
		for (int x = 0; x < w; ++x)
		{
			unsigned int bb = row[x * 4 + 0];
			unsigned int gg = row[x * 4 + 1];
			unsigned int rr = row[x * 4 + 2];
			alpha[(size_t)y * w + x] = (unsigned char)((rr * 77 + gg * 151 + bb * 28) >> 8);
		}
	}

	if (!m_Atlas->Insert(&alpha[0], w, h, out))
	{
		// Full. Everything goes and is rebuilt on demand; this frame may show
		// a wrong glyph or two, which the log reports as ATLAS_RESET.
		m_Atlas->Clear();
		m_Glyphs.clear();
		m_Generation = m_Atlas->Generation();
		if (!m_Atlas->Insert(&alpha[0], w, h, out)) return false;
	}
	return true;
}

const GlyphInfo* KoreanFont::Glyph(wchar_t ch)
{
	if (m_Atlas && m_Generation != m_Atlas->Generation())
	{
		// Another font filled the atlas and cleared it.
		m_Glyphs.clear();
		m_Generation = m_Atlas->Generation();
	}

	std::map<wchar_t, GlyphInfo>::iterator it = m_Glyphs.find(ch);
	if (it != m_Glyphs.end()) return &it->second;

	GlyphInfo g;
	if (!Build(ch, &g))
	{
		g.w = g.h = 0;
		g.advance = m_Height / 2;
		g.u0 = g.v0 = g.u1 = g.v1 = 0.0f;
	}
	m_Glyphs[ch] = g;
	return &m_Glyphs[ch];
}

int KoreanFont::Measure(const wchar_t* text)
{
	if (text == 0 || text[0] == L'\0' || m_Font == 0) return 0;
	int w = 0;
	for (const wchar_t* p = text; *p; ++p) w += Glyph(*p)->advance;
	return w + PAD * 2;
}
