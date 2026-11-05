#include "stdafx.h"
#include "Renderer.h"

#include <cstring>
#include <vector>
#include <cmath>

#include "Dependencies\freeglut.h"

#include "Profiler.h"
#include "Platform.h"

// -----------------------------------------------------------------------------
// 화면 톤 — 여기 한 곳만 만지면 전체 분위기가 바뀐다
//
// 톤커브를 바꾸면 밝기 균형이 통째로 움직이므로 블룸 임계값과 그림자 세기도
// 같이 잡아야 한다. 그래서 흩어 두지 않고 모아 둔다.
// -----------------------------------------------------------------------------

// 피벗은 "이 밝기는 변하지 않는다"는 기준점이다. 0.42 로 두면 중간톤보다
// 어두운 영역이 전부 아래로 딸려 내려가 화면이 어두워진다. 0.50 이 중립.
static const float POST_BRIGHT_THRESHOLD = 0.66f;   // 낮으면 화면 전체가 번진다
static const float POST_BLOOM_AMOUNT     = 0.42f;
static const float POST_EXPOSURE         = 1.18f;
static const float POST_WHITE            = 1.30f;   // 롤오프 화이트 포인트
static const float POST_CONTRAST         = 1.14f;
static const float POST_PIVOT            = 0.50f;
static const float POST_SATURATION       = 1.16f;   // 더 올리면 황토색이 튄다
static const float POST_VIGNETTE         = 0.78f;

// 그림자는 호출부가 20곳 넘게 흩어져 있어서 배수로 한 번에 조정한다.
static const float SHADOW_GAIN = 1.12f;
static const float SHADOW_MAX  = 0.46f;

// -----------------------------------------------------------------------------
// Colour helpers
// -----------------------------------------------------------------------------

Col Shade(const Col& c, float k)
{
	return Col(c.r * k, c.g * k, c.b * k, c.a);
}

Col Fade(const Col& c, float alpha)
{
	return Col(c.r, c.g, c.b, c.a * alpha);
}

Col Mix(const Col& a, const Col& b, float t)
{
	return Col(a.r + (b.r - a.r) * t,
		a.g + (b.g - a.g) * t,
		a.b + (b.b - a.b) * t,
		a.a + (b.a - a.a) * t);
}

// -----------------------------------------------------------------------------

// Instances per flush before the queue is sent early. Bounds the size of one
// buffer upload; the world pass of Level1 is about 3,000.
static const size_t QUEUE_CAPACITY = 65536;

static const char* const FLUSH_REASON_NAMES[] =
{
	"fbo_switch", "static_batch", "frame_end", "capacity", "terrain"
};

StaticBatch::~StaticBatch()
{
	// 정적 배치는 씬(정적 객체)과 함께 프로그램 종료 때도 소멸한다. 그때는
	// 컨텍스트가 이미 없다.
	if (m_VBO && Platform::HasGLContext()) glDeleteBuffers(1, &m_VBO);
}

Renderer::Renderer(int windowSizeX, int windowSizeY)
{
	for (int i = 0; i < FLUSH_COUNT; ++i) m_Flushes[i] = 0;
	for (int r = 0; r < GPU_RING; ++r)
		for (int p = 0; p < GPU_PASS_COUNT; ++p)
		{
			m_Queries[r][p] = 0;
			m_QueryIssued[r][p] = false;
		}
	Initialize(windowSizeX, windowSizeY);
}

Renderer::~Renderer()
{
	for (int i = 0; i < FONT_COUNT; ++i) m_Fonts[i].Destroy();
	m_Atlas.Destroy();

	DestroyTarget(&m_Scene);
	DestroyTarget(&m_BloomA);
	DestroyTarget(&m_BloomB);

	if (m_QueriesReady) glDeleteQueries(GPU_RING * GPU_PASS_COUNT, &m_Queries[0][0]);
	if (m_InstanceVBO)   glDeleteBuffers(1, &m_InstanceVBO);
	if (m_ShapeVAO)      glDeleteVertexArrays(1, &m_ShapeVAO);
	if (m_VBOQuad)       glDeleteBuffers(1, &m_VBOQuad);
	if (m_ShapeProg)     glDeleteProgram(m_ShapeProg);
	if (m_GroundProg)    glDeleteProgram(m_GroundProg);
	if (m_BrightProg)    glDeleteProgram(m_BrightProg);
	if (m_BlurProg)      glDeleteProgram(m_BlurProg);
	if (m_CompositeProg) glDeleteProgram(m_CompositeProg);
}

void Renderer::Initialize(int windowSizeX, int windowSizeY)
{
	m_WindowSizeX = windowSizeX;
	m_WindowSizeY = windowSizeY;

	int shape = CompileShaders("./Shaders/Shape.vs", "./Shaders/Shape.fs");
	if (shape < 0)
	{
		std::cout << "Renderer: shape program failed.\n";
		return;
	}
	m_ShapeProg = (GLuint)shape;

	m_sScreen       = glGetUniformLocation(m_ShapeProg, "u_Screen");
	m_sOffset       = glGetUniformLocation(m_ShapeProg, "u_Offset");
	m_sScale        = glGetUniformLocation(m_ShapeProg, "u_Scale");
	m_sTime         = glGetUniformLocation(m_ShapeProg, "u_Time");
	m_sHaze         = glGetUniformLocation(m_ShapeProg, "u_Haze");
	m_sHazeZoom     = glGetUniformLocation(m_ShapeProg, "u_HazeZoom");
	m_sSeeThru      = glGetUniformLocation(m_ShapeProg, "u_SeeThru");
	m_sSeeThruShape = glGetUniformLocation(m_ShapeProg, "u_SeeThruShape");
	m_sTex          = glGetUniformLocation(m_ShapeProg, "u_Tex");

	CreateVertexBufferObject();
	CreateShapeVAO();

	if (m_VBOQuad > 0 && m_ShapeVAO > 0 && m_InstanceVBO > 0 && m_sScreen >= 0)
	{
		m_Initialized = true;
	}
	else
	{
		std::cout << "Renderer: shape setup failed.\n";
		return;
	}

	m_Queue.reserve(8192);

	// ---- terrain ----------------------------------------------------------------
	int ground = CompileShaders("./Shaders/Ground.vs", "./Shaders/Ground.fs");
	if (ground >= 0)
	{
		m_GroundProg = (GLuint)ground;
		m_gTypes     = glGetUniformLocation(m_GroundProg, "u_Types");
		m_gMapSize   = glGetUniformLocation(m_GroundProg, "u_MapSize");
		m_gScreen    = glGetUniformLocation(m_GroundProg, "u_Screen");
		m_gOffset    = glGetUniformLocation(m_GroundProg, "u_Offset");
		m_gZoom      = glGetUniformLocation(m_GroundProg, "u_Zoom");
		m_gTile      = glGetUniformLocation(m_GroundProg, "u_Tile");
		m_gTime      = glGetUniformLocation(m_GroundProg, "u_Time");
		m_gColors    = glGetUniformLocation(m_GroundProg, "u_Colors");
		m_gTypeCount = glGetUniformLocation(m_GroundProg, "u_TypeCount");
		m_gWater     = glGetUniformLocation(m_GroundProg, "u_WaterType");
		m_gWarp      = glGetUniformLocation(m_GroundProg, "u_Warp");
		m_gSharp     = glGetUniformLocation(m_GroundProg, "u_Sharp");
		m_gHaze      = glGetUniformLocation(m_GroundProg, "u_Haze");
	}
	else
	{
		std::cout << "Renderer: ground program failed, terrain will not draw.\n";
	}

	// ---- post-processing chain ---------------------------------------------
	int bright = CompileShaders("./Shaders/Post.vs", "./Shaders/Bright.fs");
	int blur   = CompileShaders("./Shaders/Post.vs", "./Shaders/Blur.fs");
	int comp   = CompileShaders("./Shaders/Post.vs", "./Shaders/Composite.fs");

	if (bright >= 0 && blur >= 0 && comp >= 0)
	{
		m_BrightProg    = (GLuint)bright;
		m_BlurProg      = (GLuint)blur;
		m_CompositeProg = (GLuint)comp;

		m_bTex       = glGetUniformLocation(m_BrightProg, "u_Tex");
		m_bThreshold = glGetUniformLocation(m_BrightProg, "u_Threshold");
		m_bAttribPos = glGetAttribLocation(m_BrightProg, "a_Position");

		m_lTex       = glGetUniformLocation(m_BlurProg, "u_Tex");
		m_lDir       = glGetUniformLocation(m_BlurProg, "u_Dir");
		m_lAttribPos = glGetAttribLocation(m_BlurProg, "a_Position");

		m_cScene     = glGetUniformLocation(m_CompositeProg, "u_Scene");
		m_cBloom     = glGetUniformLocation(m_CompositeProg, "u_Bloom");
		m_cAmount    = glGetUniformLocation(m_CompositeProg, "u_BloomAmount");
		m_cVignette  = glGetUniformLocation(m_CompositeProg, "u_Vignette");
		m_cExposure  = glGetUniformLocation(m_CompositeProg, "u_Exposure");
		m_cWhite     = glGetUniformLocation(m_CompositeProg, "u_White");
		m_cContrast  = glGetUniformLocation(m_CompositeProg, "u_Contrast");
		m_cPivot     = glGetUniformLocation(m_CompositeProg, "u_Pivot");
		m_cSaturation= glGetUniformLocation(m_CompositeProg, "u_Saturation");
		m_cAttribPos = glGetAttribLocation(m_CompositeProg, "a_Position");

		int qw = windowSizeX / 4;
		int qh = windowSizeY / 4;
		if (qw < 1) qw = 1;
		if (qh < 1) qh = 1;

		if (CreateTarget(&m_Scene, windowSizeX, windowSizeY) &&
			CreateTarget(&m_BloomA, qw, qh) &&
			CreateTarget(&m_BloomB, qw, qh))
		{
			m_PostReady = true;
		}
		else
		{
			std::cout << "Renderer: framebuffer setup failed, post disabled.\n";
		}
	}
	else
	{
		std::cout << "Renderer: post programs failed, post disabled.\n";
	}

	// ---- GPU timers -----------------------------------------------------------
	glGenQueries(GPU_RING * GPU_PASS_COUNT, &m_Queries[0][0]);
	m_QueriesReady = (m_Queries[0][0] != 0);

	// ---- fonts --------------------------------------------------------------
	// 2048 x 2048 single channel = 4 MB. Holds thousands of Hangul glyphs, so a
	// reset (and a frame of wrong glyphs) should never happen in practice.
	m_Atlas.Create(2048);
	m_Fonts[FONT_SMALL].Create(15, false, &m_Atlas);
	m_Fonts[FONT_BODY].Create(19, false, &m_Atlas);
	m_Fonts[FONT_TITLE].Create(30, true, &m_Atlas);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_DEPTH_TEST);
}

bool Renderer::IsInitialized() const
{
	return m_Initialized;
}

void Renderer::SetVSync(int interval)
{
	if (interval < 0) return;
	Platform::SetSwapInterval(interval);
}

void Renderer::CreateVertexBufferObject()
{
	float quad[] =
	{
		-0.5f, -0.5f, 0.f,   -0.5f,  0.5f, 0.f,    0.5f,  0.5f, 0.f,
		-0.5f, -0.5f, 0.f,    0.5f,  0.5f, 0.f,    0.5f, -0.5f, 0.f,
	};
	glGenBuffers(1, &m_VBOQuad);
	glBindBuffer(GL_ARRAY_BUFFER, m_VBOQuad);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// The shape program gets its own vertex array object. Instance attributes
// carry a divisor, and leaving them on the default VAO would leak into the
// post-processing passes that share it.
void Renderer::CreateShapeVAO()
{
	glGenVertexArrays(1, &m_ShapeVAO);
	glGenBuffers(1, &m_InstanceVBO);

	glBindVertexArray(m_ShapeVAO);

	glBindBuffer(GL_ARRAY_BUFFER, m_VBOQuad);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, 0);
	glVertexAttribDivisor(0, 0);

	for (GLuint loc = 1; loc <= 9; ++loc)
	{
		glEnableVertexAttribArray(loc);
		glVertexAttribDivisor(loc, 1);
	}

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

bool Renderer::CreateTarget(RenderTarget* rt, int w, int h)
{
	rt->w = w;
	rt->h = h;

	glGenTextures(1, &rt->tex);
	glBindTexture(GL_TEXTURE_2D, rt->tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glGenFramebuffers(1, &rt->fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, rt->fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		GL_TEXTURE_2D, rt->tex, 0);

	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);

	return status == GL_FRAMEBUFFER_COMPLETE;
}

void Renderer::DestroyTarget(RenderTarget* rt)
{
	if (rt->fbo) glDeleteFramebuffers(1, &rt->fbo);
	if (rt->tex) glDeleteTextures(1, &rt->tex);
	rt->fbo = 0;
	rt->tex = 0;
}

// -----------------------------------------------------------------------------
// Shader plumbing
// -----------------------------------------------------------------------------

void Renderer::AddShader(GLuint program, const char* text, GLenum type)
{
	GLuint obj = glCreateShader(type);
	if (obj == 0)
	{
		fprintf(stderr, "Error creating shader type %d\n", type);
		return;
	}

	const GLchar* p[1];
	p[0] = text;
	GLint lengths[1];
	lengths[0] = (GLint)strlen(text);

	glShaderSource(obj, 1, p, lengths);
	glCompileShader(obj);

	GLint ok = 0;
	glGetShaderiv(obj, GL_COMPILE_STATUS, &ok);
	if (!ok)
	{
		GLchar log[1024];
		glGetShaderInfoLog(obj, 1024, NULL, log);
		fprintf(stderr, "Shader compile error (type %d): %s\n", type, log);
	}

	glAttachShader(program, obj);
	glDeleteShader(obj);
}

bool Renderer::ReadFile(const char* filename, std::string* target)
{
	std::ifstream file(filename);
	if (file.fail())
	{
		std::cout << filename << " file loading failed.\n";
		return false;
	}

	std::string line;
	while (getline(file, line))
	{
		target->append(line.c_str());
		target->append("\n");
	}
	return true;
}

int Renderer::CompileShaders(const char* filenameVS, const char* filenameFS)
{
	GLuint program = glCreateProgram();
	if (program == 0) return -1;

	std::string vs, fs;
	if (!ReadFile(filenameVS, &vs)) { glDeleteProgram(program); return -1; }
	if (!ReadFile(filenameFS, &fs)) { glDeleteProgram(program); return -1; }

	AddShader(program, vs.c_str(), GL_VERTEX_SHADER);
	AddShader(program, fs.c_str(), GL_FRAGMENT_SHADER);

	GLint ok = 0;
	GLchar log[1024] = { 0 };

	glLinkProgram(program);
	glGetProgramiv(program, GL_LINK_STATUS, &ok);
	if (ok == 0)
	{
		glGetProgramInfoLog(program, sizeof(log), NULL, log);
		std::cout << filenameVS << ", " << filenameFS << " link error\n" << log;
		glDeleteProgram(program);
		return -1;
	}

	std::cout << filenameVS << ", " << filenameFS << " ok\n";
	return (int)program;
}

// -----------------------------------------------------------------------------
// GPU timing
//
// GL_TIME_ELAPSED queries, one per pass, in a ring of GPU_RING frames. A result
// is read only when it is already available, so profiling never makes the CPU
// wait for the GPU. Results arrive a few frames late, which is fine for
// averages.
// -----------------------------------------------------------------------------

void Renderer::BeginGpuPass(int pass)
{
	if (!m_QueriesReady) return;
	if (m_ActivePass >= 0) EndGpuPass();
	glBeginQuery(GL_TIME_ELAPSED, m_Queries[m_QuerySlot][pass]);
	m_QueryIssued[m_QuerySlot][pass] = true;
	m_ActivePass = pass;
}

void Renderer::EndGpuPass()
{
	if (!m_QueriesReady || m_ActivePass < 0) return;
	glEndQuery(GL_TIME_ELAPSED);
	m_ActivePass = -1;
}

void Renderer::CollectGpuTimes()
{
	if (!m_QueriesReady) return;

	// The slot about to be reused was issued GPU_RING frames ago.
	int slot = m_QuerySlot;
	double total = 0.0;
	bool all = true;
	bool any = false;

	static const char* const names[GPU_PASS_COUNT] = { "gpu.scene_ms", "gpu.post_ms", "gpu.ui_ms" };
	static int ids[GPU_PASS_COUNT] = { -1, -1, -1 };
	if (ids[0] < 0)
		for (int p = 0; p < GPU_PASS_COUNT; ++p) ids[p] = Profiler::RegisterSample(names[p]);

	for (int p = 0; p < GPU_PASS_COUNT; ++p)
	{
		if (!m_QueryIssued[slot][p]) continue;
		any = true;

		GLint avail = 0;
		glGetQueryObjectiv(m_Queries[slot][p], GL_QUERY_RESULT_AVAILABLE, &avail);
		if (!avail)
		{
			all = false;
			PROFILE_COUNT("gpu.query_not_ready", 1);
		}
		else
		{
			GLuint64 ns = 0;
			glGetQueryObjectui64v(m_Queries[slot][p], GL_QUERY_RESULT, &ns);
			double ms = (double)ns / 1.0e6;
			total += ms;
			Profiler::AddSample(ids[p], ms);
		}
		m_QueryIssued[slot][p] = false;
	}

	if (any && all) PROFILE_SAMPLE("gpu.frame_ms", total);
}

// -----------------------------------------------------------------------------
// Frame
// -----------------------------------------------------------------------------

void Renderer::BeginFrame(const Col& clearColor)
{
	m_DrawCalls = 0;
	m_InstancesDrawn = 0;
	m_InstancesStatic = 0;
	m_UploadBytes = 0;
	for (int i = 0; i < FLUSH_COUNT; ++i) m_Flushes[i] = 0;
	m_Queue.clear();

	CollectGpuTimes();
	BeginGpuPass(GPU_SCENE);

	if (m_PostReady)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, m_Scene.fbo);
		glViewport(0, 0, m_Scene.w, m_Scene.h);
	}
	else
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, m_WindowSizeX, m_WindowSizeY);
	}

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glClearColor(clearColor.r, clearColor.g, clearColor.b, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::CountDraw(int instances)
{
	++m_DrawCalls;
	m_InstancesDrawn += instances;
}

void Renderer::FullscreenPass(GLuint program)
{
	GLint attrib = glGetAttribLocation(program, "a_Position");
	if (attrib < 0) return;

	glBindVertexArray(0);
	glEnableVertexAttribArray(attrib);
	glBindBuffer(GL_ARRAY_BUFFER, m_VBOQuad);
	glVertexAttribPointer(attrib, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, 0);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	CountDraw(1);
	glDisableVertexAttribArray(attrib);
}

void Renderer::EndFrame()
{
	// Everything queued so far belongs to the scene target.
	Flush(FLUSH_FBO_SWITCH);
	EndGpuPass();

	if (!m_PostReady)
	{
		BeginGpuPass(GPU_UI);
		return;
	}

	PROFILE_SCOPE("render.post");
	BeginGpuPass(GPU_POST);

	glDisable(GL_BLEND);

	// 1. bright pass, straight into quarter resolution
	glBindFramebuffer(GL_FRAMEBUFFER, m_BloomA.fbo);
	glViewport(0, 0, m_BloomA.w, m_BloomA.h);
	glUseProgram(m_BrightProg);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_Scene.tex);
	glUniform1i(m_bTex, 0);
	glUniform1f(m_bThreshold, POST_BRIGHT_THRESHOLD);
	FullscreenPass(m_BrightProg);

	// 2. horizontal blur
	glBindFramebuffer(GL_FRAMEBUFFER, m_BloomB.fbo);
	glViewport(0, 0, m_BloomB.w, m_BloomB.h);
	glUseProgram(m_BlurProg);
	glBindTexture(GL_TEXTURE_2D, m_BloomA.tex);
	glUniform1i(m_lTex, 0);
	glUniform2f(m_lDir, 1.0f / (float)m_BloomA.w, 0.0f);
	FullscreenPass(m_BlurProg);

	// 3. vertical blur
	glBindFramebuffer(GL_FRAMEBUFFER, m_BloomA.fbo);
	glViewport(0, 0, m_BloomA.w, m_BloomA.h);
	glBindTexture(GL_TEXTURE_2D, m_BloomB.tex);
	glUniform1i(m_lTex, 0);
	glUniform2f(m_lDir, 0.0f, 1.0f / (float)m_BloomB.h);
	FullscreenPass(m_BlurProg);

	// 4. composite to the back buffer
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, m_WindowSizeX, m_WindowSizeY);
	glUseProgram(m_CompositeProg);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_Scene.tex);
	glUniform1i(m_cScene, 0);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, m_BloomA.tex);
	glUniform1i(m_cBloom, 1);

	glUniform1f(m_cAmount, POST_BLOOM_AMOUNT);
	glUniform1f(m_cVignette, POST_VIGNETTE);
	glUniform1f(m_cExposure, POST_EXPOSURE);
	glUniform1f(m_cWhite, POST_WHITE);
	glUniform1f(m_cContrast, POST_CONTRAST);
	glUniform1f(m_cPivot, POST_PIVOT);
	glUniform1f(m_cSaturation, POST_SATURATION);
	FullscreenPass(m_CompositeProg);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glEnable(GL_BLEND);

	EndGpuPass();
	BeginGpuPass(GPU_UI);
}

void Renderer::FinishFrame()
{
	Flush(FLUSH_FRAME_END);
	EndGpuPass();
	if (m_QueriesReady) m_QuerySlot = (m_QuerySlot + 1) % GPU_RING;

	// ---- counters for the profiler -------------------------------------------
	PROFILE_COUNT("render.draw_calls", m_DrawCalls);
	PROFILE_COUNT("render.instances", m_InstancesDrawn);
	PROFILE_COUNT("render.instances_static", m_InstancesStatic);
	PROFILE_COUNT("render.upload_bytes", m_UploadBytes);

	static int flushIds[FLUSH_COUNT];
	static bool flushIdsReady = false;
	if (!flushIdsReady)
	{
		flushIdsReady = true;
		for (int i = 0; i < FLUSH_COUNT; ++i)
		{
			std::string n = std::string("render.flush.") + FLUSH_REASON_NAMES[i];
			flushIds[i] = Profiler::RegisterCounter(n.c_str());
		}
	}
	for (int i = 0; i < FLUSH_COUNT; ++i) Profiler::AddCount(flushIds[i], m_Flushes[i]);

	int glyphs = 0;
	for (int i = 0; i < FONT_COUNT; ++i) glyphs += m_Fonts[i].GlyphCount();
	PROFILE_GAUGE("text.glyphs_cached", glyphs);
	PROFILE_GAUGE("text.atlas_used_pct", m_Atlas.UsedPct());
	PROFILE_GAUGE("render.queue_capacity", (double)m_Queue.capacity());
}

// -----------------------------------------------------------------------------
// Queue
// -----------------------------------------------------------------------------

void Renderer::BindShapeState(GLuint instanceVBO, float offX, float offY, float scale)
{
	glUseProgram(m_ShapeProg);

	glUniform2f(m_sScreen, (float)m_WindowSizeX, (float)m_WindowSizeY);
	glUniform2f(m_sOffset, offX, offY);
	glUniform1f(m_sScale, scale);
	glUniform1f(m_sTime, m_FrameTime);
	glUniform4f(m_sHaze, m_Haze[0], m_Haze[1], m_Haze[2], m_Haze[3]);
	glUniform1f(m_sHazeZoom, m_HazeZoom);
	glUniform4f(m_sSeeThru, m_SeeThru[0], m_SeeThru[1], m_SeeThru[2], m_SeeThru[3]);
	glUniform4f(m_sSeeThruShape, m_SeeThruShape[0], m_SeeThruShape[1],
		m_SeeThruShape[2], m_SeeThruShape[3]);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_Atlas.Texture());
	glUniform1i(m_sTex, 0);

	glBindVertexArray(m_ShapeVAO);
	glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);

	const GLsizei stride = (GLsizei)sizeof(ShapeInstance);
	for (GLuint k = 0; k < 9; ++k)
	{
		glVertexAttribPointer(1 + k, 4, GL_FLOAT, GL_FALSE, stride,
			(const void*)(sizeof(float) * 4 * k));
	}
}

void Renderer::UnbindShapeState()
{
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
}

void Renderer::Flush(int reason)
{
	if (m_Queue.empty() || !m_Initialized) return;

	PROFILE_SCOPE("render.flush");

	const size_t n = m_Queue.size();
	const GLsizeiptr bytes = (GLsizeiptr)(n * sizeof(ShapeInstance));

	// Orphan and refill: the driver hands back fresh storage instead of
	// waiting for the GPU to finish reading last frame's data.
	glBindBuffer(GL_ARRAY_BUFFER, m_InstanceVBO);
	glBufferData(GL_ARRAY_BUFFER, bytes, &m_Queue[0], GL_STREAM_DRAW);
	m_UploadBytes += (int)bytes;

	BindShapeState(m_InstanceVBO, 0.0f, 0.0f, 1.0f);
	glDrawArraysInstanced(GL_TRIANGLES, 0, 6, (GLsizei)n);
	CountDraw((int)n);
	UnbindShapeState();

	m_Flushes[reason]++;
	m_Queue.clear();
}

void Renderer::Submit(int shape, float cx, float cy, float w, float h,
	const Col& a, const Col& b, float radius, float soft,
	float time, float ripple, float rim, const float* uv)
{
	if (!m_Initialized) return;

	ShapeInstance si;

	bool recording = (m_Record != 0);

	float swayX = m_Sway[0];
	float swayY = m_Sway[1];

	// 줌은 제출 직전에 한 번만 건다. 호출부 수십 곳에 배율을 곱하고 다니면
	// 하나만 빠져도 그 도형만 크기가 어긋난다. 기록할 때는 걸지 않는다 —
	// 재생하거나 정적 배치를 그릴 때 건다.
	if (!recording && m_WorldScale != 1.0f)
	{
		float ox = (float)m_WindowSizeX * 0.5f;
		float oy = (float)m_WindowSizeY * 0.5f;
		cx = ox + (cx - ox) * m_WorldScale;
		cy = oy + (cy - oy) * m_WorldScale;
		w *= m_WorldScale;
		h *= m_WorldScale;
		if (m_Sway[3] != 0.0f)
		{
			swayX = ox + (swayX - ox) * m_WorldScale;
			swayY = oy + (swayY - oy) * m_WorldScale;
		}
	}

	si.rect[0] = cx;          si.rect[1] = cy;
	si.rect[2] = w * 0.5f;    si.rect[3] = h * 0.5f;
	si.colA[0] = a.r; si.colA[1] = a.g; si.colA[2] = a.b; si.colA[3] = a.a;
	si.colB[0] = b.r; si.colB[1] = b.g; si.colB[2] = b.b; si.colB[3] = b.a;
	si.rot[0] = m_RotCos;     si.rot[1] = m_RotSin;
	si.rot[2] = m_PivotX;     si.rot[3] = m_PivotY;
	si.params[0] = (float)shape;
	si.params[1] = radius;
	si.params[2] = soft;
	si.params[3] = (float)m_AnchorFlags;
	si.extra[0] = (time != 0.0f) ? time - m_FrameTime : 0.0f;
	si.extra[1] = ripple;
	si.extra[2] = rim;
	si.extra[3] = m_SwayFreq;
	si.anchor[0] = m_Anchor[0];
	si.anchor[1] = m_Anchor[1];
	si.anchor[2] = m_Anchor[2];
	si.anchor[3] = 0.0f;
	si.sway[0] = swayX;       si.sway[1] = swayY;
	si.sway[2] = m_Sway[2];   si.sway[3] = m_Sway[3];
	if (uv)
	{
		si.uv[0] = uv[0]; si.uv[1] = uv[1]; si.uv[2] = uv[2]; si.uv[3] = uv[3];
	}
	else
	{
		si.uv[0] = si.uv[1] = si.uv[2] = si.uv[3] = 0.0f;
	}

	if (recording)
	{
		m_Record->push_back(si);
		return;
	}

	if (m_Queue.size() >= QUEUE_CAPACITY) Flush(FLUSH_CAPACITY);
	m_Queue.push_back(si);
}

void Renderer::DrawRect(float cx, float cy, float w, float h,
	const Col& top, const Col& bottom, float radius, float rim)
{
	Submit(0, cx, cy, w, h, top, bottom, radius, 0.0f, 0.0f, 0.0f, rim, 0);
}

void Renderer::DrawRect(float cx, float cy, float w, float h,
	const Col& flat, float radius, float rim)
{
	Submit(0, cx, cy, w, h, flat, flat, radius, 0.0f, 0.0f, 0.0f, rim, 0);
}

void Renderer::DrawEllipse(float cx, float cy, float w, float h,
	const Col& top, const Col& bottom, float rim)
{
	Submit(1, cx, cy, w, h, top, bottom, 0.0f, 0.0f, 0.0f, 0.0f, rim, 0);
}

void Renderer::DrawEllipse(float cx, float cy, float w, float h,
	const Col& flat, float rim)
{
	Submit(1, cx, cy, w, h, flat, flat, 0.0f, 0.0f, 0.0f, 0.0f, rim, 0);
}

void Renderer::DrawDiamond(float cx, float cy, float w, float h,
	const Col& top, const Col& bottom)
{
	Submit(2, cx, cy, w, h, top, bottom, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0);
}

void Renderer::DrawDiamond(float cx, float cy, float w, float h, const Col& flat)
{
	Submit(2, cx, cy, w, h, flat, flat, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0);
}

void Renderer::DrawTriangle(float cx, float cy, float w, float h,
	const Col& top, const Col& bottom, float rim)
{
	Submit(3, cx, cy, w, h, top, bottom, 0.0f, 0.0f, 0.0f, 0.0f, rim, 0);
}

void Renderer::DrawTriangle(float cx, float cy, float w, float h,
	const Col& flat, float rim)
{
	Submit(3, cx, cy, w, h, flat, flat, 0.0f, 0.0f, 0.0f, 0.0f, rim, 0);
}

void Renderer::DrawBowl(float cx, float cy, float w, float h,
	const Col& top, const Col& bottom, float rim)
{
	Submit(7, cx, cy, w, h, top, bottom, 0.0f, 0.0f, 0.0f, 0.0f, rim, 0);
}

void Renderer::DrawShadow(float cx, float cy, float w, float h,
	float alpha, float softness)
{
	float a = alpha * SHADOW_GAIN;
	if (a > SHADOW_MAX) a = SHADOW_MAX;

	// 햇빛이 따뜻하면 그늘은 차다. 흙색 그림자는 더러워 보이고, 푸른 기가
	// 돌아야 "빛이 가려진 자리"로 읽힌다.
	Col c(0.085f, 0.095f, 0.145f, a);
	Submit(1, cx, cy, w, h, c, c, 0.0f, softness, 0.0f, 0.0f, 0.0f, 0);
}

void Renderer::DrawWater(float cx, float cy, float w, float h,
	const Col& top, const Col& bottom, float time, float ripple)
{
	// time 0 would read as "not animated" in Submit, so nudge it.
	Submit(5, cx, cy, w, h, top, bottom, 0.0f, 0.0f,
		time != 0.0f ? time : 1e-6f, ripple, 0.0f, 0);
}

void Renderer::DrawFlame(float cx, float cy, float w, float h,
	float time, float intensity, float alpha)
{
	Col c(1.0f, 1.0f, 1.0f, alpha);
	Submit(6, cx, cy, w, h, c, c, 0.0f, 0.0f,
		time != 0.0f ? time : 1e-6f, intensity, 0.0f, 0);
}

// -----------------------------------------------------------------------------
// State
// -----------------------------------------------------------------------------

void Renderer::SetWorldScale(float scale)
{
	m_WorldScale = (scale > 0.01f) ? scale : 1.0f;
}

void Renderer::ClearWorldScale()
{
	m_WorldScale = 1.0f;
}

void Renderer::SetRotation(float radians, float pivotX, float pivotY)
{
	m_RotCos = cosf(radians);
	m_RotSin = sinf(radians);
	m_PivotX = pivotX;
	m_PivotY = pivotY;
}

void Renderer::ClearRotation()
{
	m_RotCos = 1.0f;
	m_RotSin = 0.0f;
	m_PivotX = 0.0f;
	m_PivotY = 0.0f;
}

void Renderer::SetAnchor(float x, float y, float depth, int flags)
{
	m_Anchor[0] = x;
	m_Anchor[1] = y;
	m_Anchor[2] = depth;
	m_AnchorFlags = flags;
}

void Renderer::ClearAnchor()
{
	m_Anchor[0] = m_Anchor[1] = m_Anchor[2] = 0.0f;
	m_AnchorFlags = 0;
}

void Renderer::SetSway(float pivotX, float pivotY, float phase, float amplitude, float freq)
{
	m_Sway[0] = pivotX;
	m_Sway[1] = pivotY;
	m_Sway[2] = phase;
	m_Sway[3] = amplitude;
	m_SwayFreq = freq;
}

void Renderer::ClearSway()
{
	m_Sway[0] = m_Sway[1] = m_Sway[2] = m_Sway[3] = 0.0f;
	m_SwayFreq = 0.0f;
}

void Renderer::SetHaze(const Col& color, float strength, float worldZoom)
{
	m_Haze[0] = color.r;
	m_Haze[1] = color.g;
	m_Haze[2] = color.b;
	m_Haze[3] = strength;
	m_HazeZoom = worldZoom;
}

void Renderer::SetSeeThrough(bool enabled, float playerDepth, float playerX, float playerY,
	float halfWidth, float up, float below, float alpha)
{
	m_SeeThru[0] = playerDepth;
	m_SeeThru[1] = playerX;
	m_SeeThru[2] = playerY;
	m_SeeThru[3] = enabled ? 1.0f : 0.0f;
	m_SeeThruShape[0] = halfWidth;
	m_SeeThruShape[1] = up;
	m_SeeThruShape[2] = below;
	m_SeeThruShape[3] = alpha;
}

// -----------------------------------------------------------------------------
// Recording / replay / static batches
// -----------------------------------------------------------------------------

void Renderer::BeginRecord(std::vector<ShapeInstance>* out)
{
	m_Record = out;
	m_RecordBatch = 0;
}

void Renderer::BeginRecord(StaticBatch* batch)
{
	batch->m_Instances.clear();
	batch->m_Dirty = true;
	m_Record = &batch->m_Instances;
	m_RecordBatch = batch;
}

void Renderer::EndRecord()
{
	m_Record = 0;
	m_RecordBatch = 0;
}

void Renderer::SubmitCached(const std::vector<ShapeInstance>& recs, float dx, float dy)
{
	if (recs.empty()) return;

	const float s = m_WorldScale;
	const float ox = (float)m_WindowSizeX * 0.5f;
	const float oy = (float)m_WindowSizeY * 0.5f;

	if (m_Queue.size() + recs.size() > QUEUE_CAPACITY) Flush(FLUSH_CAPACITY);

	for (size_t i = 0; i < recs.size(); ++i)
	{
		ShapeInstance si = recs[i];

		float cx = si.rect[0] + dx;
		float cy = si.rect[1] + dy;
		si.rect[0] = ox + (cx - ox) * s;
		si.rect[1] = oy + (cy - oy) * s;
		si.rect[2] *= s;
		si.rect[3] *= s;

		// The anchor stays pre-zoom: haze and see-through are defined in the
		// unzoomed frame, the shader zooms it itself.
		si.anchor[0] += dx;
		si.anchor[1] += dy;

		if (si.sway[3] != 0.0f)
		{
			float px = si.sway[0] + dx;
			float py = si.sway[1] + dy;
			si.sway[0] = ox + (px - ox) * s;
			si.sway[1] = oy + (py - oy) * s;
		}

		m_Queue.push_back(si);
	}
}

// -----------------------------------------------------------------------------
// Terrain
// -----------------------------------------------------------------------------

TerrainLayer::TerrainLayer()
	: m_W(0), m_H(0), m_TypeCount(1), m_WaterType(-1), m_Warp(0.35f), m_Sharp(7.0f),
	m_Tex(0), m_Dirty(true)
{
	for (int i = 0; i < MAX_TYPES * 4; ++i) m_Colors[i] = 1.0f;
}

TerrainLayer::~TerrainLayer()
{
	if (m_Tex && Platform::HasGLContext()) glDeleteTextures(1, &m_Tex);
}

void TerrainLayer::SetTiles(const unsigned char* types, int w, int h)
{
	m_W = w;
	m_H = h;
	m_Types.assign(types, types + (size_t)w * h);
	m_Dirty = true;
}

void TerrainLayer::SetTypeColor(int type, const Col& c)
{
	if (type < 0 || type >= MAX_TYPES) return;
	m_Colors[type * 4 + 0] = c.r;
	m_Colors[type * 4 + 1] = c.g;
	m_Colors[type * 4 + 2] = c.b;
	m_Colors[type * 4 + 3] = c.a;
}

void Renderer::DrawTerrain(TerrainLayer& t, float offX, float offY, float tileW, float tileH)
{
	if (!m_Initialized || m_GroundProg == 0 || t.m_Types.empty()) return;

	Flush(FLUSH_TERRAIN);

	PROFILE_SCOPE("render.terrain");

	if (t.m_Dirty || t.m_Tex == 0)
	{
		if (t.m_Tex == 0) glGenTextures(1, &t.m_Tex);
		glBindTexture(GL_TEXTURE_2D, t.m_Tex);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, t.m_W, t.m_H, 0, GL_RED, GL_UNSIGNED_BYTE, &t.m_Types[0]);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		t.m_Dirty = false;

		m_UploadBytes += t.m_W * t.m_H;
		char buf[64];
		snprintf(buf, sizeof(buf), "\"tiles\":%d", t.m_W * t.m_H);
		Profiler::Event("render.terrain_upload", buf);
	}

	// Render target size, not window size: the scene target when post is on.
	float tw = (float)(m_PostReady ? m_Scene.w : m_WindowSizeX);
	float th = (float)(m_PostReady ? m_Scene.h : m_WindowSizeY);

	glUseProgram(m_GroundProg);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, t.m_Tex);
	glUniform1i(m_gTypes, 0);
	glUniform2i(m_gMapSize, t.m_W, t.m_H);
	glUniform2f(m_gScreen, tw, th);
	glUniform2f(m_gOffset, offX, offY);
	glUniform1f(m_gZoom, m_WorldScale);
	glUniform2f(m_gTile, tileW, tileH);
	glUniform1f(m_gTime, m_FrameTime);
	glUniform4fv(m_gColors, TerrainLayer::MAX_TYPES, t.m_Colors);
	glUniform1i(m_gTypeCount, t.m_TypeCount);
	glUniform1i(m_gWater, t.m_WaterType);
	glUniform1f(m_gWarp, t.m_Warp);
	glUniform1f(m_gSharp, t.m_Sharp);
	glUniform4f(m_gHaze, m_Haze[0], m_Haze[1], m_Haze[2], m_Haze[3]);

	FullscreenPass(m_GroundProg);

	glBindTexture(GL_TEXTURE_2D, 0);
}

void Renderer::DrawStatic(StaticBatch& batch, float dx, float dy)
{
	if (!m_Initialized || batch.m_Instances.empty()) return;

	Flush(FLUSH_STATIC_BATCH);

	if (batch.m_Dirty || batch.m_VBO == 0)
	{
		PROFILE_SCOPE("render.static_upload");
		if (batch.m_VBO == 0) glGenBuffers(1, &batch.m_VBO);
		glBindBuffer(GL_ARRAY_BUFFER, batch.m_VBO);
		GLsizeiptr bytes = (GLsizeiptr)(batch.m_Instances.size() * sizeof(ShapeInstance));
		glBufferData(GL_ARRAY_BUFFER, bytes, &batch.m_Instances[0], GL_STATIC_DRAW);
		batch.m_Uploaded = (int)batch.m_Instances.size();
		batch.m_Dirty = false;
		m_UploadBytes += (int)bytes;

		char buf[96];
		snprintf(buf, sizeof(buf), "\"instances\":%d,\"bytes\":%d",
			batch.m_Uploaded, (int)bytes);
		Profiler::Event("render.static_batch_upload", buf);
	}

	PROFILE_SCOPE("render.static_draw");
	BindShapeState(batch.m_VBO, dx, dy, m_WorldScale);
	glDrawArraysInstanced(GL_TRIANGLES, 0, 6, (GLsizei)batch.m_Uploaded);
	CountDraw(batch.m_Uploaded);
	m_InstancesStatic += batch.m_Uploaded;
	UnbindShapeState();
}

// -----------------------------------------------------------------------------
// Text
// -----------------------------------------------------------------------------

void Renderer::EmitLabel(float x, float y, const wchar_t* text,
	const Col& c, FontSize size)
{
	KoreanFont& f = m_Fonts[size];
	if (!f.IsReady()) return;

	float pen = x;
	for (const wchar_t* p = text; *p; ++p)
	{
		// Copy: building a later glyph may reshuffle the font's map.
		GlyphInfo g = *f.Glyph(*p);
		if (g.u1 > g.u0 && g.w > 0 && g.h > 0)
		{
			float uv[4] = { g.u0, g.v0, g.u1, g.v1 };
			Submit(4, pen + g.w * 0.5f, y + g.h * 0.5f, (float)g.w, (float)g.h,
				c, c, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, uv);
		}
		pen += (float)g.advance;
	}
}

void Renderer::DrawLabel(float x, float y, const wchar_t* text,
	const Col& c, FontSize size)
{
	if (text == 0 || text[0] == L'\0') return;
	if (size < 0 || size >= FONT_COUNT) size = FONT_BODY;
	EmitLabel(x, y, text, c, size);
}

void Renderer::DrawLabelCentered(float cx, float y, const wchar_t* text,
	const Col& c, FontSize size)
{
	if (text == 0 || text[0] == L'\0') return;
	if (size < 0 || size >= FONT_COUNT) size = FONT_BODY;

	int w = m_Fonts[size].Measure(text);
	EmitLabel(cx - w * 0.5f, y, text, c, size);
}

int Renderer::LabelWidth(const wchar_t* text, FontSize size)
{
	if (size < 0 || size >= FONT_COUNT) size = FONT_BODY;
	return m_Fonts[size].Measure(text);
}

int Renderer::LabelHeight(FontSize size)
{
	if (size < 0 || size >= FONT_COUNT) size = FONT_BODY;
	return m_Fonts[size].Height();
}
