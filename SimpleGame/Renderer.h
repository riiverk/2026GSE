#pragma once

#include <string>
#include <cstdlib>
#include <fstream>
#include <iostream>

#include "Dependencies\glew.h"

#include "KoreanFont.h"

// -----------------------------------------------------------------------------
// RGBA colour.
// -----------------------------------------------------------------------------
struct Col
{
	float r, g, b, a;

	Col() : r(1.0f), g(1.0f), b(1.0f), a(1.0f) {}
	Col(float R, float G, float B, float A = 1.0f) : r(R), g(G), b(B), a(A) {}
};

// Multiply the rgb by k, keeping alpha. Used everywhere to derive a lit top
// edge and a shaded bottom edge from one base colour.
Col Shade(const Col& c, float k);
Col Fade(const Col& c, float alpha);
Col Mix(const Col& a, const Col& b, float t);

enum FontSize
{
	FONT_SMALL = 0,
	FONT_BODY,
	FONT_TITLE,
	FONT_COUNT
};

// -----------------------------------------------------------------------------
// Renderer
//
// Screen-space pixel coordinates throughout, origin top-left, +y down.
//
// Two things changed structurally from the first prototype:
//
//  1. Shapes are signed-distance fields evaluated in the fragment shader, so
//     every edge is anti-aliased and corners can be rounded. That is what
//     removes the blocky "pixel art" read.
//
//  2. The frame is rendered into an off-screen target and pushed through a
//     post chain (bright pass -> separable blur -> composite with grade and
//     vignette) before it reaches the back buffer.
// -----------------------------------------------------------------------------
class Renderer
{
public:
	Renderer(int windowSizeX, int windowSizeY);
	~Renderer();

	bool IsInitialized() const;
	bool HasPostProcessing() const { return m_PostReady; }

	// ---- frame ----------------------------------------------------------
	void BeginFrame(const Col& clearColor);
	void EndFrame();

	// ---- shapes ---------------------------------------------------------
	// radius is in local units: 0 = square corners, 0.5 = fully rounded.
	void DrawRect(float cx, float cy, float w, float h,
		const Col& top, const Col& bottom, float radius = 0.0f, float rim = 0.0f);
	void DrawRect(float cx, float cy, float w, float h,
		const Col& flat, float radius = 0.0f, float rim = 0.0f);

	void DrawEllipse(float cx, float cy, float w, float h,
		const Col& top, const Col& bottom, float rim = 0.0f);
	void DrawEllipse(float cx, float cy, float w, float h,
		const Col& flat, float rim = 0.0f);

	void DrawDiamond(float cx, float cy, float w, float h,
		const Col& top, const Col& bottom);
	void DrawDiamond(float cx, float cy, float w, float h, const Col& flat);

	void DrawTriangle(float cx, float cy, float w, float h,
		const Col& top, const Col& bottom, float rim = 0.0f);
	void DrawTriangle(float cx, float cy, float w, float h,
		const Col& flat, float rim = 0.0f);

	// Soft elliptical contact shadow. softness is in local units.
	void DrawShadow(float cx, float cy, float w, float h,
		float alpha, float softness = 0.22f);

	// Animated water tile.
	void DrawWater(float cx, float cy, float w, float h,
		const Col& top, const Col& bottom, float time, float ripple);

	void DrawTexture(float cx, float cy, float w, float h,
		GLuint tex, const Col& tint);

	// ---- text -----------------------------------------------------------
	// x, y is the TOP-LEFT of the text box.
	void DrawLabel(float x, float y, const wchar_t* text,
		const Col& c, FontSize size = FONT_BODY);
	void DrawLabelCentered(float cx, float y, const wchar_t* text,
		const Col& c, FontSize size = FONT_BODY);
	int  LabelWidth(const wchar_t* text, FontSize size = FONT_BODY);
	int  LabelHeight(FontSize size = FONT_BODY);

	int GetWidth() const { return m_WindowSizeX; }
	int GetHeight() const { return m_WindowSizeY; }

private:
	struct RenderTarget
	{
		GLuint fbo;
		GLuint tex;
		int    w, h;
		RenderTarget() : fbo(0), tex(0), w(0), h(0) {}
	};

	void Initialize(int windowSizeX, int windowSizeY);
	bool ReadFile(const char* filename, std::string* target);
	void AddShader(GLuint program, const char* text, GLenum type);
	int  CompileShaders(const char* filenameVS, const char* filenameFS);
	void CreateVertexBufferObject();
	bool CreateTarget(RenderTarget* rt, int w, int h);
	void DestroyTarget(RenderTarget* rt);

	void Submit(int shape, float cx, float cy, float w, float h,
		const Col& a, const Col& b, float radius, float soft,
		float time, float ripple, float rim, GLuint tex);

	void FullscreenPass(GLuint program);

	bool m_Initialized = false;
	bool m_PostReady   = false;

	int m_WindowSizeX = 0;
	int m_WindowSizeY = 0;

	GLuint m_VBOQuad = 0;

	// Shape program
	GLuint m_ShapeProg = 0;
	GLint  m_sRect = -1, m_sScreen = -1, m_sShape = -1, m_sColorA = -1,
		m_sColorB = -1, m_sRadius = -1, m_sSoft = -1, m_sExtra = -1,
		m_sTex = -1, m_sAttribPos = -1;

	// Post programs
	GLuint m_BrightProg = 0;
	GLuint m_BlurProg = 0;
	GLuint m_CompositeProg = 0;
	GLint  m_bTex = -1, m_bThreshold = -1, m_bAttribPos = -1;
	GLint  m_lTex = -1, m_lDir = -1, m_lAttribPos = -1;
	GLint  m_cScene = -1, m_cBloom = -1, m_cAmount = -1, m_cVignette = -1,
		m_cAttribPos = -1;

	RenderTarget m_Scene;
	RenderTarget m_BloomA;
	RenderTarget m_BloomB;

	KoreanFont m_Fonts[FONT_COUNT];
};
