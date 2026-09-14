#include "stdafx.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <vector>

#include "KoreanFont.h"

// "Malgun Gothic" in Hangul, written as separated escapes so this file does not
// depend on its own encoding being interpreted correctly by the compiler.
static const wchar_t* kFontFace = L"\xB9D1" L"\xC740" L" " L"\xACE0" L"\xB515";

KoreanFont::KoreanFont()
	: m_Font(0)
	, m_Height(0)
{
}

KoreanFont::~KoreanFont()
{
	Destroy();
}

bool KoreanFont::Create(int pixelHeight, bool bold)
{
	Destroy();

	m_Height = pixelHeight;

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

	// CreateFontW substitutes a Hangul-capable face if this one is missing, so
	// a null result here means something more fundamental went wrong.
	m_Font = (void*)f;
	return m_Font != 0;
}

void KoreanFont::Destroy()
{
	for (std::map<std::wstring, Item>::iterator it = m_Cache.begin();
		it != m_Cache.end(); ++it)
	{
		if (it->second.tex) glDeleteTextures(1, &it->second.tex);
	}
	m_Cache.clear();

	if (m_Font)
	{
		DeleteObject((HFONT)m_Font);
		m_Font = 0;
	}
}

GLuint KoreanFont::Build(const wchar_t* text, int* outW, int* outH)
{
	*outW = 0;
	*outH = 0;

	if (m_Font == 0 || text == 0 || text[0] == L'\0') return 0;

	const int len = (int)wcslen(text);

	HDC dc = CreateCompatibleDC(NULL);
	if (dc == NULL) return 0;

	HFONT oldFont = (HFONT)SelectObject(dc, (HFONT)m_Font);

	SIZE sz;
	sz.cx = 0;
	sz.cy = 0;
	GetTextExtentPoint32W(dc, text, len, &sz);

	const int pad = 2;
	int w = sz.cx + pad * 2;
	int h = sz.cy + pad * 2;
	if (w < 1) w = 1;
	if (h < 1) h = 1;

	BITMAPINFO bi;
	ZeroMemory(&bi, sizeof(bi));
	bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth       = w;
	bi.bmiHeader.biHeight      = -h;   // negative = top-down rows
	bi.bmiHeader.biPlanes      = 1;
	bi.bmiHeader.biBitCount    = 32;
	bi.bmiHeader.biCompression = BI_RGB;

	void* bits = NULL;
	HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
	if (bmp == NULL || bits == NULL)
	{
		SelectObject(dc, oldFont);
		DeleteDC(dc);
		return 0;
	}

	HBITMAP oldBmp = (HBITMAP)SelectObject(dc, bmp);

	RECT rc;
	rc.left = 0; rc.top = 0; rc.right = w; rc.bottom = h;
	HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
	FillRect(dc, &rc, black);
	DeleteObject(black);

	SetBkMode(dc, TRANSPARENT);
	SetTextColor(dc, RGB(255, 255, 255));
	TextOutW(dc, pad, pad, text, len);
	GdiFlush();

	// White pixels with the glyph coverage as alpha. Tinting happens in the
	// shader, so one texture serves every colour the UI needs.
	std::vector<unsigned char> rgba((size_t)w * h * 4);
	const unsigned char* src = (const unsigned char*)bits;
	for (int i = 0; i < w * h; ++i)
	{
		unsigned int bb = src[i * 4 + 0];
		unsigned int gg = src[i * 4 + 1];
		unsigned int rr = src[i * 4 + 2];
		unsigned char a = (unsigned char)((rr * 77 + gg * 151 + bb * 28) >> 8);

		rgba[i * 4 + 0] = 255;
		rgba[i * 4 + 1] = 255;
		rgba[i * 4 + 2] = 255;
		rgba[i * 4 + 3] = a;
	}

	GLuint tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, &rgba[0]);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

	SelectObject(dc, oldBmp);
	SelectObject(dc, oldFont);
	DeleteObject(bmp);
	DeleteDC(dc);

	*outW = w;
	*outH = h;
	return tex;
}

GLuint KoreanFont::GetTexture(const wchar_t* text, int* outW, int* outH)
{
	if (text == 0 || text[0] == L'\0')
	{
		if (outW) *outW = 0;
		if (outH) *outH = 0;
		return 0;
	}

	std::wstring key(text);

	std::map<std::wstring, Item>::iterator it = m_Cache.find(key);
	if (it != m_Cache.end())
	{
		if (outW) *outW = it->second.w;
		if (outH) *outH = it->second.h;
		return it->second.tex;
	}

	Item item;
	item.tex = Build(text, &item.w, &item.h);
	m_Cache[key] = item;

	if (outW) *outW = item.w;
	if (outH) *outH = item.h;
	return item.tex;
}

int KoreanFont::Measure(const wchar_t* text)
{
	int w = 0, h = 0;
	GetTexture(text, &w, &h);
	return w;
}
