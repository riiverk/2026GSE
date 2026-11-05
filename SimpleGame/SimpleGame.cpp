/*
	Gran Gusto
	Game Software Engineering, Tech University of Korea

	Base template (c) 2022 Lee Taek Hee.

	This file owns the window, the input plumbing and the frame loop. All world
	state lives in the active scene: Game (tutorial) or Level1 (hunting ground).
*/

#include "stdafx.h"

#include <iostream>
#include <cstdio>
#include <string>

#include "Dependencies\glew.h"
#include "Dependencies\freeglut.h"

#include "Renderer.h"
#include "Model.h"
#include "Game.h"
#include "Level1.h"
#include "Platform.h"
#include "Profiler.h"

static const int WIN_W = 1280;
static const int WIN_H = 720;

// ---- 프레임 루프 설정 ---------------------------------------------------------
// TARGET_FPS  0 이면 제한 없이 idle 루프로 돈다 (그때는 VSYNC 가 상한이 된다).
// VSYNC      -1 드라이버 기본값을 건드리지 않음, 0 끔, 1 켬.
//
// 기본값은 예전과 같은 60 이다. 60 을 넘겨 여유를 재 보고 싶으면 TARGET_FPS
// 를 0, VSYNC 를 0 으로 둔다. 그 상태의 FPS 가 곧 실제 처리 능력이다.
static const int TARGET_FPS = 60;
static const int VSYNC      = -1;

static const char* const LOG_DIR = "Logs";

enum Scene
{
	SCENE_TUTORIAL = 0,
	SCENE_LEVEL1
};

static Renderer* g_Renderer = NULL;
static double    g_LastTickMs = 0.0;
static double    g_NextTickMs = 0.0;
// 마을 테스트 중이라 고향 마을(튜토리얼)에서 시작한다. F2 로 사냥터.
static Scene     g_Scene = SCENE_TUTORIAL;
static bool      g_ShowFps = true;   // F3 로 켜고 끈다

// ---- 화면 FPS ----------------------------------------------------------------
// 오른쪽 위, 두 씬의 오른쪽 상단 패널 바로 아래. 목표 대비 색으로 상태를 바로
// 읽을 수 있게 한다 (초록 95% 이상, 노랑 75% 이상, 빨강 그 아래).
static void DrawFpsOverlay()
{
	if (!g_ShowFps) return;

	double fps = Profiler::DisplayFps();
	double ms  = Profiler::DisplayFrameMs();
	if (fps <= 0.0) return;   // 첫 0.25초

	int target = Profiler::TargetFps();
	Col c(0.55f, 0.90f, 0.55f);
	if (target > 0)
	{
		if (fps < target * 0.75)      c = Col(1.00f, 0.45f, 0.40f);
		else if (fps < target * 0.95) c = Col(1.00f, 0.85f, 0.40f);
	}

	wchar_t buf[48];
	swprintf_s(buf, 48, L"FPS %.0f   %.1f ms", fps, ms);

	const float W = (float)WIN_W;
	int tw = g_Renderer->LabelWidth(buf, FONT_SMALL);
	int th = g_Renderer->LabelHeight(FONT_SMALL);
	float cx = W - 30.0f - tw * 0.5f;
	float cy = 84.0f;
	g_Renderer->DrawRect(cx, cy, tw + 22.0f, th + 12.0f,
		Col(0.08f, 0.07f, 0.07f, 0.55f), 0.45f);
	g_Renderer->DrawLabel(W - 30.0f - tw, cy - th * 0.5f - 2.0f, buf, c, FONT_SMALL);
}

static void RenderScene(void)
{
	{
		PROFILE_SCOPE("frame.render");

		if (g_Scene == SCENE_LEVEL1) Level1::Render();
		else
		{
			PROFILE_SCOPE("tutorial.render");
			Game::Render();
		}

		DrawFpsOverlay();

		// UI 까지 그린 큐를 내보내고 GPU 타이머를 닫는다
		g_Renderer->FinishFrame();
	}
	{
		// vsync 나 GPU 가 밀려 있으면 여기서 기다린다. frame.swap 이 길면
		// CPU 가 아니라 GPU/주사율 쪽을 봐야 한다는 뜻이다.
		PROFILE_SCOPE("frame.swap");
		glutSwapBuffers();
	}

	Profiler::FrameMark();
}

// 한 번의 게임 갱신. 타이머 루프와 idle 루프가 같이 쓴다.
static void Tick()
{
	PROFILE_SCOPE("frame.update");

	double now = Profiler::NowMs();
	float dt = (float)((now - g_LastTickMs) / 1000.0);
	g_LastTickMs = now;

	// A hitch must not become a step big enough to tunnel through geometry.
	if (dt > 0.1f) dt = 0.1f;
	if (dt < 0.0f) dt = 0.0f;

	if (g_Scene == SCENE_LEVEL1) Level1::Update(dt);
	else
	{
		PROFILE_SCOPE("tutorial.update");
		Game::Update(dt);
	}
}

static void Timer(int value)
{
	Tick();
	glutPostRedisplay();

	// 다음 틱은 "지금 + 16ms" 가 아니라 정해진 박자에 맞춘다. 예전 방식은
	// 갱신에 걸린 시간만큼 매 프레임 밀려서 60 을 못 채우고 간격도 흔들렸다.
	const double period = 1000.0 / (double)(TARGET_FPS > 0 ? TARGET_FPS : 60);
	double now = Profiler::NowMs();
	g_NextTickMs += period;
	if (g_NextTickMs < now - period * 2.0) g_NextTickMs = now;   // 크게 밀리면 따라잡지 않는다

	int delay = (int)(g_NextTickMs - now);
	if (delay < 0) delay = 0;
	glutTimerFunc((unsigned int)delay, Timer, 0);
}

static void Idle()
{
	Tick();
	glutPostRedisplay();
}

static void KeyDown(unsigned char key, int x, int y)
{
	if (key == 27) { glutLeaveMainLoop(); return; }   // ESC

	if (g_Scene == SCENE_LEVEL1)
	{
		Level1::SetKey(key, true);
		Level1::KeyPressed(key);
	}
	else
	{
		Game::SetKey(key, true);
		if (key == 'e' || key == 'E') Game::Interact();
		if (key == 'g' || key == 'G') Game::Gratitude();
		if (key == 'r' || key == 'R') Game::Reset();
	}
}

static void KeyUp(unsigned char key, int x, int y)
{
	if (g_Scene == SCENE_LEVEL1) Level1::SetKey(key, false);
	else                         Game::SetKey(key, false);
}

static void SpecialKeyInput(int key, int x, int y)
{
	// F1 tutorial, F2 level 1, F3 화면 FPS 표시.
	// 마을(튜토리얼)에서는 F5 주민 현황, F6 다음 날, F7 상태 덤프, F8 다음 구간.
	if (key == GLUT_KEY_F3)
	{
		g_ShowFps = !g_ShowFps;
		return;
	}
	if (g_Scene == SCENE_TUTORIAL && key >= GLUT_KEY_F5 && key <= GLUT_KEY_F8)
	{
		Game::SpecialKey(key - GLUT_KEY_F1 + 1);
		return;
	}
	if (key == GLUT_KEY_F1)
	{
		g_Scene = SCENE_TUTORIAL;
		std::cout << "Scene: tutorial\n";
		Profiler::Event("scene.switch", "\"to\":\"tutorial\"");
	}
	else if (key == GLUT_KEY_F2)
	{
		g_Scene = SCENE_LEVEL1;
		std::cout << "Scene: level 1\n";
		Profiler::Event("scene.switch", "\"to\":\"level1\"");
	}
}

static void MouseInput(int button, int state, int x, int y)
{
}

// JSON 문자열에 넣기 전에 따옴표와 역슬래시를 뺀다 (GPU 이름 등)
static std::string JsonSafe(const GLubyte* s)
{
	std::string out;
	if (!s) return out;
	for (const char* p = (const char*)s; *p; ++p)
	{
		if (*p == '"' || *p == '\\' || (unsigned char)*p < 0x20) out += ' ';
		else out += *p;
	}
	return out;
}

int main(int argc, char** argv)
{
	Platform::SetConsoleUtf8();
	Profiler::Init(LOG_DIR);
	double startMs = Profiler::NowMs();

	glutInit(&argc, argv);
	// 깊이 버퍼는 쓰지 않는다 (깊이는 painter 정렬). 만들지 않으면 그만큼
	// 메모리와 클리어 대역폭이 준다.
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
	glutInitWindowPosition(40, 30);
	glutInitWindowSize(WIN_W, WIN_H);
	glutCreateWindow("Gran Gusto");

	// 창을 X 로 닫아도 glutMainLoop 가 돌아오게 해야 세션 요약이 남는다
	glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_GLUTMAINLOOP_RETURNS);

	glewExperimental = GL_TRUE;
	GLenum err = glewInit();
	if (err != GLEW_OK)
	{
		std::cout << "GLEW init failed.\n";
	}
	if (!glewIsSupported("GL_VERSION_3_3"))
	{
		std::cout << "OpenGL 3.3 is not supported on this device.\n";
	}

	g_Renderer = new Renderer(WIN_W, WIN_H);
	if (!g_Renderer->IsInitialized())
	{
		std::cout << "Renderer could not be initialized.\n";
	}
	else if (!g_Renderer->HasPostProcessing())
	{
		std::cout << "Post-processing unavailable; drawing straight to the back buffer.\n";
	}
	g_Renderer->SetVSync(VSYNC);

	// session_start 는 로그의 첫 줄이어야 하므로 레벨 초기화(맵 생성 이벤트)
	// 보다 먼저 쓴다. 전체 시작 시간은 startup.complete 이벤트로 따로 남긴다.
	{
		std::string gpu = JsonSafe(glGetString(GL_RENDERER));
		std::string ver = JsonSafe(glGetString(GL_VERSION));

		Profiler::SessionInfo info;
#ifdef _DEBUG
		info.build = "Debug";
#else
		info.build = "Release";
#endif
		info.glRenderer = gpu.c_str();
		info.glVersion = ver.c_str();
		info.width = WIN_W;
		info.height = WIN_H;
		info.targetFps = TARGET_FPS;
		info.vsync = VSYNC;
		info.startupMs = Profiler::NowMs() - startMs;
		Profiler::WriteSessionStart(info);
	}

	// Model definitions are built once and cached to Data/models.bin.
	{
		double t0 = Profiler::NowMs();
		Models::Load();
		char buf[64];
		snprintf(buf, sizeof(buf), "\"ms\":%.3f", Profiler::NowMs() - t0);
		Profiler::Event("startup.models_load", buf);
	}

	Game::Init(g_Renderer, WIN_W, WIN_H);
	Level1::Init(g_Renderer, WIN_W, WIN_H);

	{
		char buf[64];
		snprintf(buf, sizeof(buf), "\"ms\":%.3f", Profiler::NowMs() - startMs);
		Profiler::Event("startup.complete", buf);
	}

	// Without this, holding a key produces repeated down/up pairs from the OS
	// and movement stutters.
	glutIgnoreKeyRepeat(1);

	glutDisplayFunc(RenderScene);
	glutKeyboardFunc(KeyDown);
	glutKeyboardUpFunc(KeyUp);
	glutMouseFunc(MouseInput);
	glutSpecialFunc(SpecialKeyInput);

	// Windows 기본 타이머 해상도(약 15.6ms)로는 16ms 타이머가 들쭉날쭉하다
	Platform::BeginHighResTimer();

	g_LastTickMs = Profiler::NowMs();
	g_NextTickMs = g_LastTickMs;
	if (TARGET_FPS > 0) glutTimerFunc(0, Timer, 0);
	else                glutIdleFunc(Idle);

	glutMainLoop();

	Platform::EndHighResTimer();
	Profiler::Shutdown();

	// glutMainLoop 가 돌아올 때 freeglut 은 이미 창과 GL 컨텍스트를 없앤
	// 뒤다. 여기서 렌더러를 지우면 컨텍스트 없이 glDelete* 를 부르게 되므로
	// 지우지 않고 프로세스 종료에 맡긴다.
	return 0;
}
