#include "stdafx.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

// PSAPI_VERSION 2 는 GetProcessMemoryInfo 를 kernel32 의 K32 버전으로 연결해
// psapi.lib 없이 쓸 수 있게 한다.
#ifndef PSAPI_VERSION
#define PSAPI_VERSION 2
#endif
#include <psapi.h>

#include <direct.h>
#include <cstdio>
#include <ctime>

#include "Platform.h"

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "opengl32.lib")

namespace Platform
{

void SetConsoleUtf8()
{
	SetConsoleOutputCP(CP_UTF8);
}

bool EnableConsoleColors()
{
	HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
	if (h == INVALID_HANDLE_VALUE || h == NULL) return false;
	DWORD mode = 0;
	if (!GetConsoleMode(h, &mode)) return false;   // 파일로 리다이렉트된 경우
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
	return SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != FALSE;
}

void BeginHighResTimer()
{
	timeBeginPeriod(1);
}

void EndHighResTimer()
{
	timeEndPeriod(1);
}

double WorkingSetMB()
{
	PROCESS_MEMORY_COUNTERS_EX pmc;
	pmc.cb = sizeof(pmc);
	if (!GetProcessMemoryInfo(GetCurrentProcess(),
		(PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)))
		return -1.0;
	return (double)pmc.WorkingSetSize / (1024.0 * 1024.0);
}

double PrivateBytesMB()
{
	PROCESS_MEMORY_COUNTERS_EX pmc;
	pmc.cb = sizeof(pmc);
	if (!GetProcessMemoryInfo(GetCurrentProcess(),
		(PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)))
		return -1.0;
	return (double)pmc.PrivateUsage / (1024.0 * 1024.0);
}

void LocalTimestamp(char* out, int cap)
{
	time_t t = time(NULL);
	struct tm lt;
	localtime_s(&lt, &t);
	strftime(out, (size_t)cap, "%Y-%m-%dT%H:%M:%S", &lt);
}

void LocalTimestampCompact(char* out, int cap)
{
	time_t t = time(NULL);
	struct tm lt;
	localtime_s(&lt, &t);
	strftime(out, (size_t)cap, "%Y%m%d_%H%M%S", &lt);
}

void EnsureDirectory(const char* path)
{
	_mkdir(path);   // 이미 있으면 실패하지만 상관없다
}

bool SetSwapInterval(int interval)
{
	typedef BOOL (WINAPI *SwapIntervalFn)(int);
	SwapIntervalFn fn = (SwapIntervalFn)wglGetProcAddress("wglSwapIntervalEXT");
	if (fn == NULL) return false;
	return fn(interval) != FALSE;
}

bool HasGLContext()
{
	return wglGetCurrentContext() != NULL;
}

int Utf8ToWide(const char* utf8, wchar_t* out, int cap)
{
	if (cap <= 0) return 0;
	out[0] = 0;
	if (!utf8 || !utf8[0]) return 0;

	int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, cap);
	if (n == 0)
	{
		// 버퍼가 모자라면 실패한다. 필요한 길이를 받아 잘라서 다시 쓴다
		int need = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
		if (need <= 0) return 0;
		wchar_t* tmp = new wchar_t[need];
		MultiByteToWideChar(CP_UTF8, 0, utf8, -1, tmp, need);
		wcsncpy_s(out, cap, tmp, _TRUNCATE);
		delete[] tmp;
		return (int)wcslen(out);
	}
	return n - 1;
}

} // namespace Platform
