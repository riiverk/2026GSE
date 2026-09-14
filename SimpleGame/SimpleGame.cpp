/*
	Gran Gusto - tutorial level prototype
	Game Software Engineering, Tech University of Korea

	Base template (c) 2022 Lee Taek Hee.

	This file owns the window, the input plumbing and the frame timer. Every
	piece of world state and drawing lives in Game.cpp.
*/

#include "stdafx.h"

#include <iostream>

#include "Dependencies\glew.h"
#include "Dependencies\freeglut.h"

#include "Renderer.h"
#include "Game.h"

static const int WIN_W = 1280;
static const int WIN_H = 720;

static Renderer* g_Renderer = NULL;
static int       g_LastMs = 0;

static void RenderScene(void)
{
	Game::Render();
	glutSwapBuffers();
}

static void Timer(int value)
{
	int now = glutGet(GLUT_ELAPSED_TIME);
	float dt = (now - g_LastMs) / 1000.0f;
	g_LastMs = now;

	// A hitch must not become a step big enough to tunnel through geometry.
	if (dt > 0.1f) dt = 0.1f;
	if (dt < 0.0f) dt = 0.0f;

	Game::Update(dt);

	glutPostRedisplay();
	glutTimerFunc(16, Timer, 0);
}

static void KeyDown(unsigned char key, int x, int y)
{
	Game::SetKey(key, true);

	if (key == 27) { glutLeaveMainLoop(); return; }   // ESC
	if (key == 'e' || key == 'E') Game::Interact();
	if (key == 'r' || key == 'R') Game::Reset();
}

static void KeyUp(unsigned char key, int x, int y)
{
	Game::SetKey(key, false);
}

static void MouseInput(int button, int state, int x, int y)
{
}

static void SpecialKeyInput(int key, int x, int y)
{
}

int main(int argc, char** argv)
{
	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_DEPTH | GLUT_DOUBLE | GLUT_RGBA);
	glutInitWindowPosition(40, 30);
	glutInitWindowSize(WIN_W, WIN_H);
	glutCreateWindow("Gran Gusto - Tutorial Prototype");

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

	Game::Init(g_Renderer, WIN_W, WIN_H);

	// Without this, holding a key produces repeated down/up pairs from the OS
	// and movement stutters.
	glutIgnoreKeyRepeat(1);

	glutDisplayFunc(RenderScene);
	glutKeyboardFunc(KeyDown);
	glutKeyboardUpFunc(KeyUp);
	glutMouseFunc(MouseInput);
	glutSpecialFunc(SpecialKeyInput);

	g_LastMs = glutGet(GLUT_ELAPSED_TIME);
	glutTimerFunc(16, Timer, 0);

	glutMainLoop();

	delete g_Renderer;
	return 0;
}
