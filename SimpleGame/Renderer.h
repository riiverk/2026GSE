#pragma once

#include <string>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

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
// One shape = one instance. 36 floats, 144 bytes. The layout matches the
// vertex attributes of Shaders/Shape.vs one to one.
// -----------------------------------------------------------------------------
struct ShapeInstance
{
	float rect[4];     // centre x, y, half w, half h (px)
	float colA[4];
	float colB[4];
	float rot[4];      // cos, sin, pivot x, pivot y (local units)
	float params[4];   // shape, radius, softness, anchor flags
	float extra[4];    // time offset, intensity/ripple, rim, sway frequency
	float anchor[4];   // x, y (pre-zoom px), depth, unused
	float sway[4];     // pivot x, y (px), phase, amplitude
	float uv[4];       // atlas u0, v0, u1, v1
};

// Anchor flags. The anchor is a point that stands for the whole object (a
// tree's foot) so per-object effects stay uniform across its parts.
enum AnchorFlags
{
	ANCHOR_HAZE    = 1,   // aerial haze from the anchor's screen height
	ANCHOR_SEETHRU = 2    // fades when standing in front of the player
};

// Instances recorded once and drawn every frame from a GPU buffer, with the
// camera applied in the vertex shader. Only for things that never move and
// never interleave with moving things in painter order (the ground).
class StaticBatch
{
public:
	StaticBatch() : m_VBO(0), m_Uploaded(0), m_Dirty(true) {}
	~StaticBatch();

	void Clear() { m_Instances.clear(); m_Dirty = true; }
	int  Count() const { return (int)m_Instances.size(); }
	bool Empty() const { return m_Instances.empty(); }

private:
	friend class Renderer;
	std::vector<ShapeInstance> m_Instances;
	GLuint m_VBO;
	int    m_Uploaded;
	bool   m_Dirty;
};

// Ground as a continuous surface. The map is a grid of tile types, but the
// shader blends neighbouring tiles and bends the borders with noise, so no
// tile edge is ever drawn. One full-screen draw call regardless of map size.
class TerrainLayer
{
public:
	static const int MAX_TYPES = 8;

	TerrainLayer();
	~TerrainLayer();

	// Row-major types, one byte per tile (y * w + x). Copied; uploaded lazily.
	void SetTiles(const unsigned char* types, int w, int h);
	void SetTypeColor(int type, const Col& c);
	void SetTypeCount(int n) { m_TypeCount = n; }
	void SetWaterType(int type) { m_WaterType = type; }

	// warp: how far borders wander, in tiles (0 = straight bilinear blobs).
	// sharpness: blend exponent (higher = crisper borders).
	void SetShape(float warp, float sharpness) { m_Warp = warp; m_Sharp = sharpness; }

	int Width() const { return m_W; }
	int Height() const { return m_H; }

private:
	friend class Renderer;
	std::vector<unsigned char> m_Types;
	int    m_W, m_H;
	float  m_Colors[MAX_TYPES * 4];
	int    m_TypeCount;
	int    m_WaterType;
	float  m_Warp;
	float  m_Sharp;
	GLuint m_Tex;
	bool   m_Dirty;
};

// -----------------------------------------------------------------------------
// Renderer
//
// Screen-space pixel coordinates throughout, origin top-left, +y down.
//
//  1. Shapes are signed-distance fields evaluated in the fragment shader, so
//     every edge is anti-aliased and corners can be rounded.
//
//  2. The frame is rendered into an off-screen target and pushed through a
//     post chain (bright pass -> separable blur -> composite with grade and
//     vignette) before it reaches the back buffer.
//
//  3. Nothing is drawn when Draw* is called. Every shape is appended to a
//     render queue and the queue goes out as ONE instanced draw call when
//     something forces it (render target switch, a static batch, frame end).
//     Order inside the queue is submission order, and GL rasterises instances
//     in order, so painter's algorithm and alpha blending are unaffected.
//     All shapes share one program and one quad; text samples a glyph atlas
//     bound for the whole batch, so text does not break batches either.
//
// Frame protocol:  BeginFrame -> world -> EndFrame -> UI -> FinishFrame -> swap
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
	void EndFrame();      // world done: flush, post-process to the back buffer
	void FinishFrame();   // UI done: flush, close GPU timers, report counters

	// Absolute time used by animated shapes this frame (water, flame, sway).
	// Shapes store their time relative to this, so recorded batches animate.
	void SetFrameTime(float t) { m_FrameTime = t; }

	// ---- transform ------------------------------------------------------
	// 월드 줌. 화면 중앙을 기준으로, 제출되는 모든 도형의 위치와 크기를
	// 한 번에 배율한다. 카메라가 플레이어를 화면 중앙에 두므로 이게 곧
	// "카메라를 뒤로 뺀다"에 해당한다. UI 를 그리기 전에 반드시 끈다.
	void SetWorldScale(float scale);
	void ClearWorldScale();
	float GetWorldScale() const { return m_WorldScale; }

	// Rotation applies to every shape drawn until it is cleared. The pivot is
	// in LOCAL units (-0.5 .. +0.5), so a limb whose pivot is (0, -0.5) swings
	// about its top edge (the joint) instead of sliding off the body.
	void SetRotation(float radians, float pivotX = 0.0f, float pivotY = 0.0f);
	void ClearRotation();

	// ---- per-object state (sticky until cleared) ---------------------------
	void SetAnchor(float x, float y, float depth, int flags);
	void ClearAnchor();

	// Parts drawn while set orbit (pivotX, pivotY) by
	// sin(time * freq + phase) * amplitude, evaluated on the GPU.
	void SetSway(float pivotX, float pivotY, float phase, float amplitude, float freq);
	void ClearSway();

	// Scene-wide effect parameters, read by the shader at flush time.
	void SetHaze(const Col& color, float strength, float worldZoom);
	void SetSeeThrough(bool enabled, float playerDepth, float playerX, float playerY,
		float halfWidth, float up, float below, float alpha);

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

	// Lower half of an ellipse, flat edge on top. (cx, cy) is the quad centre,
	// so the flat edge sits at cy - h/2. Pair it with an ellipse on that edge
	// for anything seen open from above: a bowl, a pot, a ladle head.
	void DrawBowl(float cx, float cy, float w, float h,
		const Col& top, const Col& bottom, float rim = 0.0f);

	// Soft elliptical contact shadow. softness is in local units.
	void DrawShadow(float cx, float cy, float w, float h,
		float alpha, float softness = 0.22f);

	// Animated water tile. time is absolute.
	void DrawWater(float cx, float cy, float w, float h,
		const Col& top, const Col& bottom, float time, float ripple);

	// Procedural flame. Silhouette, turbulence and colour ramp all live in the
	// fragment shader, so one quad is one fire. time is absolute.
	void DrawFlame(float cx, float cy, float w, float h,
		float time, float intensity, float alpha);

	// ---- recording / replay ------------------------------------------------
	// Between BeginRecord and EndRecord, Draw* appends raw instances to `out`
	// instead of the queue: no world zoom, time relative to the frame time.
	// Record with the camera at the origin.
	void BeginRecord(std::vector<ShapeInstance>* out);
	void EndRecord();

	// Replays recorded instances into the queue, moved by (dx, dy) (the
	// camera) and zoomed like live shapes. Painter order is kept, so this is
	// how cached objects interleave with moving ones.
	void SubmitCached(const std::vector<ShapeInstance>& recs, float dx, float dy);

	// Continuous terrain over the whole target. (offX, offY) is where world
	// (0, 0) lands on screen before zoom; tileW / tileH the iso tile size.
	// Flushes the queue first, like a static batch.
	void DrawTerrain(TerrainLayer& terrain, float offX, float offY, float tileW, float tileH);

	void BeginRecord(StaticBatch* batch);
	// Draws a static batch now, with its own GPU buffer. Flushes the queue
	// first so earlier shapes stay behind it.
	void DrawStatic(StaticBatch& batch, float dx, float dy);

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

	// ---- stats ----------------------------------------------------------
	// glDraw* 호출 수. BeginFrame 에서 0 으로 돌아가므로, FinishFrame 뒤에
	// 읽으면 UI 까지 포함한 한 프레임치가 나온다.
	int GetDrawCallCount() const { return m_DrawCalls; }

	// Vertical sync: -1 leave the driver default, 0 off, 1 on.
	void SetVSync(int interval);

private:
	struct RenderTarget
	{
		GLuint fbo;
		GLuint tex;
		int    w, h;
		RenderTarget() : fbo(0), tex(0), w(0), h(0) {}
	};

	enum FlushReason
	{
		FLUSH_FBO_SWITCH = 0,
		FLUSH_STATIC_BATCH,
		FLUSH_FRAME_END,
		FLUSH_CAPACITY,
		FLUSH_TERRAIN,
		FLUSH_COUNT
	};

	enum GpuPass { GPU_SCENE = 0, GPU_POST, GPU_UI, GPU_PASS_COUNT };
	static const int GPU_RING = 4;

	void Initialize(int windowSizeX, int windowSizeY);
	bool ReadFile(const char* filename, std::string* target);
	void AddShader(GLuint program, const char* text, GLenum type);
	int  CompileShaders(const char* filenameVS, const char* filenameFS);
	void CreateVertexBufferObject();
	void CreateShapeVAO();
	bool CreateTarget(RenderTarget* rt, int w, int h);
	void DestroyTarget(RenderTarget* rt);

	void Submit(int shape, float cx, float cy, float w, float h,
		const Col& a, const Col& b, float radius, float soft,
		float time, float ripple, float rim, const float* uv);

	void Flush(int reason);
	void BindShapeState(GLuint instanceVBO, float offX, float offY, float scale);
	void UnbindShapeState();
	void EmitLabel(float x, float y, const wchar_t* text, const Col& c, FontSize size);

	void FullscreenPass(GLuint program);

	// 모든 glDraw* 는 여기를 거친다. 카운트가 빠지는 경로를 만들지 않기 위해서다
	void CountDraw(int instances);

	void BeginGpuPass(int pass);
	void EndGpuPass();
	void CollectGpuTimes();

	bool m_Initialized = false;
	bool m_PostReady   = false;

	int m_WindowSizeX = 0;
	int m_WindowSizeY = 0;

	GLuint m_VBOQuad = 0;
	GLuint m_ShapeVAO = 0;
	GLuint m_InstanceVBO = 0;

	// ---- queue ----
	std::vector<ShapeInstance>  m_Queue;
	std::vector<ShapeInstance>* m_Record = 0;   // non-null while recording
	StaticBatch*                m_RecordBatch = 0;

	// ---- per-frame stats ----
	int m_DrawCalls = 0;
	int m_InstancesDrawn = 0;
	int m_InstancesStatic = 0;
	int m_UploadBytes = 0;
	int m_Flushes[FLUSH_COUNT];

	// Shape program
	GLuint m_ShapeProg = 0;
	GLint  m_sScreen = -1, m_sOffset = -1, m_sScale = -1, m_sTime = -1,
		m_sHaze = -1, m_sHazeZoom = -1, m_sSeeThru = -1, m_sSeeThruShape = -1,
		m_sTex = -1;

	// Sticky state copied into each instance
	float m_WorldScale = 1.0f;
	float m_RotCos = 1.0f;
	float m_RotSin = 0.0f;
	float m_PivotX = 0.0f;
	float m_PivotY = 0.0f;
	float m_Anchor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	int   m_AnchorFlags = 0;
	float m_Sway[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	float m_SwayFreq = 0.0f;
	float m_FrameTime = 0.0f;

	// Scene-wide effect uniforms
	float m_Haze[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	float m_HazeZoom = 1.0f;
	float m_SeeThru[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	float m_SeeThruShape[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

	// Terrain program
	GLuint m_GroundProg = 0;
	GLint  m_gTypes = -1, m_gMapSize = -1, m_gScreen = -1, m_gOffset = -1, m_gZoom = -1,
		m_gTile = -1, m_gTime = -1, m_gColors = -1, m_gTypeCount = -1, m_gWater = -1,
		m_gWarp = -1, m_gSharp = -1, m_gHaze = -1;

	// Post programs
	GLuint m_BrightProg = 0;
	GLuint m_BlurProg = 0;
	GLuint m_CompositeProg = 0;
	GLint  m_bTex = -1, m_bThreshold = -1, m_bAttribPos = -1;
	GLint  m_lTex = -1, m_lDir = -1, m_lAttribPos = -1;
	GLint  m_cScene = -1, m_cBloom = -1, m_cAmount = -1, m_cVignette = -1,
		m_cAttribPos = -1;
	GLint  m_cExposure = -1, m_cWhite = -1, m_cContrast = -1, m_cPivot = -1,
		m_cSaturation = -1;

	RenderTarget m_Scene;
	RenderTarget m_BloomA;
	RenderTarget m_BloomB;

	// GPU timer queries, a small ring so reading never stalls the pipeline
	GLuint m_Queries[GPU_RING][GPU_PASS_COUNT];
	bool   m_QueryIssued[GPU_RING][GPU_PASS_COUNT];
	int    m_QuerySlot = 0;
	int    m_ActivePass = -1;
	bool   m_QueriesReady = false;

	GlyphAtlas m_Atlas;
	KoreanFont m_Fonts[FONT_COUNT];
};
