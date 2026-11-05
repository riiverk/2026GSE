#pragma once

// -----------------------------------------------------------------------------
// Platform
//
// windows.h 가 필요한 것만 여기로 모은다. windows.h 는 DrawText, GetObject
// 같은 이름을 매크로로 바꿔 버려서, 렌더러나 게임 코드가 직접 include 하면
// 멤버 함수가 사라진다. 이 헤더는 windows.h 를 끌어오지 않는다.
// -----------------------------------------------------------------------------
namespace Platform
{
	// 콘솔 출력 코드페이지를 UTF-8 로. 소스가 /utf-8 로 컴파일되므로 한글
	// 로그가 CP949 콘솔에서 깨지지 않게 한다.
	void SetConsoleUtf8();

	// 콘솔 색 (ANSI 이스케이프). Windows 10 이상 콘솔에서 켜진다.
	// 켜지지 않으면 false — 그때는 색 코드를 출력하면 안 된다.
	bool EnableConsoleColors();

	// Windows 기본 타이머 해상도는 약 15.6ms 라 16ms 타이머가 들쭉날쭉해진다.
	// 1ms 로 올리고, 종료 때 되돌린다.
	void BeginHighResTimer();
	void EndHighResTimer();

	// 프로세스 메모리 (MB). 실패하면 음수.
	double WorkingSetMB();
	double PrivateBytesMB();

	// "2026-09-23T15:04:05" (로컬 시각)
	void LocalTimestamp(char* out, int cap);
	// "20260923_150405" (파일 이름용)
	void LocalTimestampCompact(char* out, int cap);

	// 폴더가 없으면 만든다
	void EnsureDirectory(const char* path);

	// 수직 동기화. 0 끔, 1 켬. 현재 GL 컨텍스트에 건다. 드라이버가
	// WGL_EXT_swap_control 을 지원하지 않으면 false.
	bool SetSwapInterval(int interval);

	// 현재 스레드에 GL 컨텍스트가 붙어 있는가. 종료 중 소멸자가 컨텍스트
	// 없이 glDelete* 를 부르지 않게 할 때 쓴다.
	bool HasGLContext();

	// UTF-8 -> UTF-16. 데이터 파일(*.ggd)의 문자열을 KoreanFont 에 넘길 때 쓴다.
	// 넘치면 잘라 내고 항상 0 으로 끝낸다. 반환값은 쓴 글자 수.
	int Utf8ToWide(const char* utf8, wchar_t* out, int cap);
}
