#include "stdafx.h"
#include "Renderer.h"

#include <cstring>
#include <vector>

#include "Dependencies\freeglut.h"

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

Renderer::Renderer(int windowSizeX, int windowSizeY)
{
	Initialize(windowSizeX, windowSizeY);
}

Renderer::~Renderer()
{
	for (int i = 0; i < FONT_COUNT; ++i) m_Fonts[i].Destroy();

	DestroyTarget(&m_Scene);
	DestroyTarget(&m_BloomA);
	DestroyTarget(&m_BloomB);

	if (m_VBOQuad)       glDeleteBuffers(1, &m_VBOQuad);
	if (m_ShapeProg)     glDeleteProgram(m_ShapeProg);
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

	m_sRect      = glGetUniformLocation(m_ShapeProg, "u_Rect");
	m_sScreen    = glGetUniformLocation(m_ShapeProg, "u_Screen");
	m_sShape     = glGetUniformLocation(m_ShapeProg, "u_Shape");
	m_sColorA    = glGetUniformLocation(m_ShapeProg, "u_ColorA");
	m_sColorB    = glGetUniformLocation(m_ShapeProg, "u_ColorB");
	m_sRadius    = glGetUniformLocation(m_ShapeProg, "u_Radius");
	m_sSoft      = glGetUniformLocation(m_ShapeProg, "u_Soft");
	m_sExtra     = glGetUniformLocation(m_ShapeProg, "u_Extra");
	m_sTex       = glGetUniformLocation(m_ShapeProg, "u_Tex");
	m_sAttribPos = glGetAttribLocation(m_ShapeProg, "a_Position");

	CreateVertexBufferObject();

	if (m_VBOQuad > 0 && m_sRect >= 0 && m_sScreen >= 0 && m_sAttribPos >= 0)
	{
		m_Initialized = true;
	}
	else
	{
		std::cout << "Renderer: shape uniform lookup failed.\n";
		return;
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

	// ---- fonts --------------------------------------------------------------
	m_Fonts[FONT_SMALL].Create(15, false);
	m_Fonts[FONT_BODY].Create(19, false);
	m_Fonts[FONT_TITLE].Create(30, true);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_DEPTH_TEST);
}

bool Renderer::IsInitialized() const
{
	return m_Initialized;
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
// Frame
// -----------------------------------------------------------------------------

void Renderer::BeginFrame(const Col& clearColor)
{
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

void Renderer::FullscreenPass(GLuint program)
{
	GLint attrib = glGetAttribLocation(program, "a_Position");
	if (attrib < 0) return;

	glEnableVertexAttribArray(attrib);
	glBindBuffer(GL_ARRAY_BUFFER, m_VBOQuad);
	glVertexAttribPointer(attrib, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, 0);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glDisableVertexAttribArray(attrib);
}

void Renderer::EndFrame()
{
	if (!m_PostReady) return;

	glDisable(GL_BLEND);

	// 1. bright pass, straight into quarter resolution
	glBindFramebuffer(GL_FRAMEBUFFER, m_BloomA.fbo);
	glViewport(0, 0, m_BloomA.w, m_BloomA.h);
	glUseProgram(m_BrightProg);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_Scene.tex);
	glUniform1i(m_bTex, 0);
	glUniform1f(m_bThreshold, 0.60f);
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

	glUniform1f(m_cAmount, 0.55f);
	glUniform1f(m_cVignette, 0.85f);
	FullscreenPass(m_CompositeProg);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glEnable(GL_BLEND);
}

// -----------------------------------------------------------------------------
// Shape submission
// -----------------------------------------------------------------------------

void Renderer::Submit(int shape, float cx, float cy, float w, float h,
	const Col& a, const Col& b, float radius, float soft,
	float time, float ripple, float rim, GLuint tex)
{
	if (!m_Initialized) return;

	glUseProgram(m_ShapeProg);

	glUniform4f(m_sRect, cx, cy, w * 0.5f, h * 0.5f);
	glUniform2f(m_sScreen, (float)m_WindowSizeX, (float)m_WindowSizeY);
	glUniform1i(m_sShape, shape);
	glUniform4f(m_sColorA, a.r, a.g, a.b, a.a);
	glUniform4f(m_sColorB, b.r, b.g, b.b, b.a);
	glUniform1f(m_sRadius, radius);
	glUniform1f(m_sSoft, soft);
	glUniform4f(m_sExtra, time, ripple, rim, 0.0f);

	if (shape == 4)
	{
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, tex);
		glUniform1i(m_sTex, 0);
	}

	glEnableVertexAttribArray(m_sAttribPos);
	glBindBuffer(GL_ARRAY_BUFFER, m_VBOQuad);
	glVertexAttribPointer(m_sAttribPos, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, 0);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glDisableVertexAttribArray(m_sAttribPos);
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

void Renderer::DrawShadow(float cx, float cy, float w, float h,
	float alpha, float softness)
{
	Col c(0.13f, 0.10f, 0.08f, alpha);
	Submit(1, cx, cy, w, h, c, c, 0.0f, softness, 0.0f, 0.0f, 0.0f, 0);
}

void Renderer::DrawWater(float cx, float cy, float w, float h,
	const Col& top, const Col& bottom, float time, float ripple)
{
	Submit(5, cx, cy, w, h, top, bottom, 0.0f, 0.0f, time, ripple, 0.0f, 0);
}

void Renderer::DrawTexture(float cx, float cy, float w, float h,
	GLuint tex, const Col& tint)
{
	if (tex == 0) return;
	Submit(4, cx, cy, w, h, tint, tint, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, tex);
}

// -----------------------------------------------------------------------------
// Text
// -----------------------------------------------------------------------------

void Renderer::DrawLabel(float x, float y, const wchar_t* text,
	const Col& c, FontSize size)
{
	if (text == 0 || text[0] == L'\0') return;
	if (size < 0 || size >= FONT_COUNT) size = FONT_BODY;

	int w = 0, h = 0;
	GLuint tex = m_Fonts[size].GetTexture(text, &w, &h);
	if (tex == 0 || w == 0 || h == 0) return;

	DrawTexture(x + w * 0.5f, y + h * 0.5f, (float)w, (float)h, tex, c);
}

void Renderer::DrawLabelCentered(float cx, float y, const wchar_t* text,
	const Col& c, FontSize size)
{
	if (text == 0 || text[0] == L'\0') return;
	if (size < 0 || size >= FONT_COUNT) size = FONT_BODY;

	int w = 0, h = 0;
	GLuint tex = m_Fonts[size].GetTexture(text, &w, &h);
	if (tex == 0 || w == 0 || h == 0) return;

	DrawTexture(cx, y + h * 0.5f, (float)w, (float)h, tex, c);
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
