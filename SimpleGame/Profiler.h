#pragma once

// =============================================================================
// Profiler
//
// 목적: 지금 느린 곳과 앞으로 느려질 곳을 사람과 AI 가 같은 로그로 찾는다.
//
// 출력
//   Logs/perf_<시각>.jsonl   JSON Lines. 한 줄이 한 레코드. 스키마 "gg-perf/1".
//   Logs/perf_latest.txt     가장 최근 로그 파일 이름
//   콘솔                     5초마다 사람이 읽는 요약 블록 (색, 한국어 경고 설명)
//                            종료 때 세션 요약. FPS 는 게임 화면에 따로 띄운다
//
// 레코드 종류 (type 필드)
//   session_start  빌드, GPU, 해상도, 목표 FPS, 시작 시간
//   window         1초 구간 집계. 프레임 시간, 스코프별 CPU 시간, 카운터,
//                  GPU 시간, 메모리, 경고(warnings)
//   hitch          예산의 2배를 넘긴 프레임 하나. 그 프레임의 상위 스코프 포함
//   event          드문 일 (아틀라스 리셋, 정적 배치 생성, 맵 생성 등)
//   session_end    세션 전체 요약. 백분위, 최악 프레임, 경고 누적
//
// 이름 규칙 (자세한 뜻과 임계값은 PERF_LOG.md)
//   점으로 계층을 나눈다: <영역>.<대상>.<세부>   예) scene.update.monster
//   단위는 접미사로 붙인다: _ms _bytes _mb _pct, 없으면 개수
//   스코프 시간은 "포함 시간"이다. 부모 스코프가 자식 시간을 포함한다.
//
// 사용
//   PROFILE_SCOPE("render.flush");            // 블록이 끝날 때까지의 CPU 시간
//   PROFILE_COUNT("render.draw_calls", 1);     // 프레임 안에서 누적
//   PROFILE_GAUGE("text.atlas_used_pct", p);   // 마지막 값
//   PROFILE_SAMPLE("gpu.scene_ms", ms);        // 들어온 표본의 평균/최대
//   Profiler::Event("scene.rebuild", "\"ms\":12.3");
// =============================================================================

namespace Profiler
{
	struct SessionInfo
	{
		const char* build;        // "Debug" / "Release"
		const char* glRenderer;
		const char* glVersion;
		int         width;
		int         height;
		int         targetFps;    // 0 = 제한 없음
		int         vsync;        // -1 드라이버 기본, 0 끔, 1 켬
		double      startupMs;
	};

	void Init(const char* logDir);
	void WriteSessionStart(const SessionInfo& info);
	void Shutdown();

	bool   Enabled();
	double NowMs();

	int RegisterScope(const char* name);
	int RegisterCounter(const char* name);
	int RegisterGauge(const char* name);
	int RegisterSample(const char* name);

	void AddTime(int id, double ms);
	void AddCount(int id, double v);
	void SetGauge(int id, double v);
	void AddSample(int id, double v);

	// fieldsJson 은 중괄호 없는 JSON 필드 목록 ("\"k\":1,\"s\":\"x\""), NULL 가능
	void Event(const char* name, const char* fieldsJson);

	// 프레임 끝 (스왑 직후). 프레임 경계이자 1초 구간 집계 시점
	void FrameMark();

	// 화면 표시용. 최근 0.5초의 평균이고 0.25초마다만 바뀌어 숫자가 떨리지 않는다
	double DisplayFps();
	double DisplayFrameMs();
	int    TargetFps();

	class ScopeTimer
	{
	public:
		explicit ScopeTimer(int id) : m_Id(id), m_T0(Enabled() ? NowMs() : -1.0) {}
		~ScopeTimer() { if (m_T0 >= 0.0) AddTime(m_Id, NowMs() - m_T0); }
	private:
		int    m_Id;
		double m_T0;
	};
}

#define PROF_CAT2(a, b) a##b
#define PROF_CAT(a, b) PROF_CAT2(a, b)

#define PROFILE_SCOPE(name) \
	static const int PROF_CAT(prof_id_, __LINE__) = Profiler::RegisterScope(name); \
	Profiler::ScopeTimer PROF_CAT(prof_t_, __LINE__)(PROF_CAT(prof_id_, __LINE__))

#define PROFILE_COUNT(name, v) \
	do { static const int prof_id_ = Profiler::RegisterCounter(name); \
		Profiler::AddCount(prof_id_, (double)(v)); } while (0)

#define PROFILE_GAUGE(name, v) \
	do { static const int prof_id_ = Profiler::RegisterGauge(name); \
		Profiler::SetGauge(prof_id_, (double)(v)); } while (0)

#define PROFILE_SAMPLE(name, v) \
	do { static const int prof_id_ = Profiler::RegisterSample(name); \
		Profiler::AddSample(prof_id_, (double)(v)); } while (0)
