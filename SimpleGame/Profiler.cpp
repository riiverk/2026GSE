#include "stdafx.h"

#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdarg>

#include "Profiler.h"
#include "Platform.h"

// =============================================================================
// Profiler
//
// 이 파일은 콘솔 로그에 한글이 있어 UTF-8 BOM 으로 저장한다.
// =============================================================================

namespace Profiler
{

namespace
{
	enum Kind { K_SCOPE = 0, K_COUNTER, K_GAUGE, K_SAMPLE };

	const char* const SCHEMA = "gg-perf/1";

	// 경고 임계값. 값의 근거와 대응은 PERF_LOG.md 에 있다
	const double LIMIT_DRAW_CALLS      = 100.0;
	const double LIMIT_INSTANCES       = 20000.0;
	const double LIMIT_UPLOAD_BYTES    = 2.0 * 1024.0 * 1024.0;
	const double LIMIT_ATLAS_PCT       = 85.0;
	const double LIMIT_SEP_PAIRS       = 20000.0;
	const double LIMIT_ACTOR_CLASS_MS  = 2.0;
	const double LIMIT_MEM_GROWTH_MB   = 64.0;
	const double WARMUP_MS             = 3000.0;   // 이 시간 안의 글리프 생성·히치는 시작 비용으로 본다
	const int    MAX_HITCH_RECORDS     = 20;       // 구간당

	struct Metric
	{
		std::string name;
		int    kind;

		double frameSum;
		int    frameCalls;
		double lastFrame;     // 직전 프레임 값 (콘솔 표시용)

		double winSum;
		double winMax;        // 스코프/카운터: 프레임 합의 최대, 표본: 표본 최대
		int    winCalls;

		double sesSum;
		double sesMax;
		long long sesCalls;

		double gauge;
		bool   gaugeSet;
		double gaugeFirst;
		bool   gaugeFirstSet;
		double gaugeMax;
	};

	struct Worst
	{
		long long frame;
		double    ms;
		double    t;
	};

	std::vector<Metric> g_M;

	bool  g_On = false;
	FILE* g_File = 0;
	std::string g_FilePath;

	std::chrono::steady_clock::time_point g_T0 = std::chrono::steady_clock::now();

	double    g_FirstMark = -1.0;
	double    g_LastMark = -1.0;
	double    g_WinStart = 0.0;
	long long g_Frame = 0;
	int       g_WinFrames = 0;

	std::vector<float> g_WinInterval;
	std::vector<float> g_WinWork;
	std::vector<float> g_SesInterval;

	int  g_WinHitches = 0;
	int  g_WinHitchRecords = 0;
	long long g_SesHitches = 0;

	std::vector<Worst> g_Worst;
	std::map<std::string, int> g_WarnSeen;

	int    g_TargetFps = 60;
	double g_Budget = 1000.0 / 60.0;

	// 경고 규칙과 프레임 계산에 쓰는 이름들
	int ID_UPDATE, ID_RENDER, ID_SWAP;
	int ID_DRAW_CALLS, ID_INSTANCES, ID_UPLOAD, ID_GLYPH_BUILDS, ID_ATLAS_PCT,
		ID_ATLAS_RESETS, ID_GPU_FRAME, ID_SCENE_NODES, ID_SCENE_CULLED,
		ID_SEP_PAIRS, ID_MEM_WS, ID_MEM_PRIVATE;

	// ---- 유틸 -------------------------------------------------------------

	int Register(const char* name, int kind)
	{
		for (size_t i = 0; i < g_M.size(); ++i)
		{
			if (g_M[i].name == name) return (int)i;
		}
		Metric m;
		m.name = name;
		m.kind = kind;
		m.frameSum = 0.0; m.frameCalls = 0; m.lastFrame = 0.0;
		m.winSum = 0.0; m.winMax = 0.0; m.winCalls = 0;
		m.sesSum = 0.0; m.sesMax = 0.0; m.sesCalls = 0;
		m.gauge = 0.0; m.gaugeSet = false;
		m.gaugeFirst = 0.0; m.gaugeFirstSet = false; m.gaugeMax = 0.0;
		g_M.push_back(m);
		return (int)g_M.size() - 1;
	}

	void Append(std::string& s, const char* fmt, ...)
	{
		char buf[1024];
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);
		s += buf;
	}

	double Percentile(std::vector<float> v, double p)
	{
		if (v.empty()) return 0.0;
		std::sort(v.begin(), v.end());
		size_t idx = (size_t)(p * (double)(v.size() - 1) + 0.5);
		if (idx >= v.size()) idx = v.size() - 1;
		return v[idx];
	}

	double Avg(const std::vector<float>& v)
	{
		if (v.empty()) return 0.0;
		double s = 0.0;
		for (size_t i = 0; i < v.size(); ++i) s += v[i];
		return s / (double)v.size();
	}

	double Max(const std::vector<float>& v)
	{
		double m = 0.0;
		for (size_t i = 0; i < v.size(); ++i) if (v[i] > m) m = v[i];
		return m;
	}

	void WriteLine(const std::string& line)
	{
		if (!g_File) return;
		fputs(line.c_str(), g_File);
		fputc('\n', g_File);
		fflush(g_File);   // 크래시가 나도 직전 구간까지는 남긴다
	}

	double WinAvgPerFrame(int id)
	{
		if (id < 0 || g_WinFrames == 0) return 0.0;
		return g_M[id].winSum / (double)g_WinFrames;
	}

	double WinSampleAvg(int id)
	{
		if (id < 0 || g_M[id].winCalls == 0) return -1.0;
		return g_M[id].winSum / (double)g_M[id].winCalls;
	}

	struct Warn
	{
		std::string code;
		double value;
		double limit;
		std::string subject;   // 경고 대상 (스코프 이름 등). 없으면 빈 문자열
	};

	void AddWarn(std::vector<Warn>& w, const char* code, double value, double limit,
		const char* subject = "")
	{
		Warn x;
		x.code = code;
		x.value = value;
		x.limit = limit;
		x.subject = subject;
		w.push_back(x);
		g_WarnSeen[code]++;
	}

	// ---- 콘솔 요약 --------------------------------------------------------
	//
	// 사람이 읽는 출력. JSON 은 AI 와 도구용으로 그대로 두고, 콘솔은
	//   - 5초에 한 번, 몇 줄짜리 블록으로
	//   - 상태를 한 단어와 색으로 (정상 / 주의 / 경고)
	//   - 경고는 코드 대신 한국어 문장과 숫자로
	// 보여 준다.

	const double CONSOLE_PERIOD_MS = 5000.0;

	bool g_Color = false;

	const char* C_RESET  = "\x1b[0m";
	const char* C_DIM    = "\x1b[90m";
	const char* C_GREEN  = "\x1b[92m";
	const char* C_YELLOW = "\x1b[93m";
	const char* C_RED    = "\x1b[91m";
	const char* C_CYAN   = "\x1b[96m";

	const char* Clr(const char* c) { return g_Color ? c : ""; }

	struct PeriodWarn
	{
		std::string code;
		std::string subject;
		double value;
		double limit;
	};

	struct Period
	{
		double start;
		double spanMs;
		int    frames;
		double fpsMin;
		double workSum;       // 프레임 가중 합
		double updSum, rendSum, swapSum;
		double gpuSum;
		int    gpuN;
		double dcSum, instSum;
		int    hitches;
		double worstMs;
		std::string worstCause;
		double worstCauseMs;
		bool   warm;
		std::vector<PeriodWarn> warns;
		std::map<std::string, double> actorMs;   // 액터 클래스 스코프, 프레임 합
	};

	Period g_Period;

	std::vector<float> g_Recent;
	double g_DisplayAt = -1e9;
	double g_DisplayFps = 0.0;
	double g_DisplayMs = 0.0;

	void ResetPeriod(double now)
	{
		g_Period.start = now;
		g_Period.spanMs = 0.0;
		g_Period.frames = 0;
		g_Period.fpsMin = 1e9;
		g_Period.workSum = 0.0;
		g_Period.updSum = g_Period.rendSum = g_Period.swapSum = 0.0;
		g_Period.gpuSum = 0.0;
		g_Period.gpuN = 0;
		g_Period.dcSum = g_Period.instSum = 0.0;
		g_Period.hitches = 0;
		g_Period.worstMs = 0.0;
		g_Period.worstCause.clear();
		g_Period.worstCauseMs = 0.0;
		g_Period.warm = false;
		g_Period.warns.clear();
		g_Period.actorMs.clear();
	}

	// 스코프 이름 -> 사람이 읽는 이름. 모르는 이름은 그대로 둔다.
	std::string ClassKo(const std::string& c)
	{
		if (c == "monster")    return "몬스터";
		if (c == "prop")       return "나무·바위";
		if (c == "player")     return "플레이어";
		if (c == "weapon")     return "무기";
		if (c == "drop")       return "드롭";
		if (c == "float_text") return "떠오르는 글자";
		if (c == "ground")     return "지면";
		return c;
	}

	std::string FriendlyName(const std::string& n)
	{
		if (n.compare(0, 19, "scene.update.actor.") == 0) return ClassKo(n.substr(19)) + " 갱신";
		if (n.compare(0, 19, "scene.render.actor.") == 0) return ClassKo(n.substr(19)) + " 그리기";

		static const char* const table[][2] =
		{
			{ "frame.update",         "게임 갱신" },
			{ "frame.render",         "그리기" },
			{ "frame.swap",           "화면 교체 대기" },
			{ "level1.update",        "레벨 갱신" },
			{ "level1.render",        "레벨 그리기" },
			{ "level1.ui",            "UI" },
			{ "tutorial.update",      "튜토리얼 갱신" },
			{ "tutorial.render",      "튜토리얼 그리기" },
			{ "scene.update",         "씬 갱신" },
			{ "scene.flush",          "생성·삭제 반영" },
			{ "scene.render.bounds",  "경계 계산" },
			{ "scene.render.collect", "화면 밖 제외(컬링)" },
			{ "scene.render.sort",    "깊이 정렬" },
			{ "scene.render.emit",    "액터 그리기" },
			{ "render.flush",         "도형 제출" },
			{ "render.post",          "후처리" },
			{ "render.terrain",       "지형" },
			{ "render.static_draw",   "지면 장식" },
			{ "render.static_upload", "정적 배치 업로드" },
			{ "text.glyph_build",     "새 글자 생성" },
		};
		for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); ++i)
			if (n == table[i][0]) return table[i][1];
		return n;
	}

	// 다른 스코프를 감싸기만 하는 이름. "원인" 을 짚을 때는 건너뛴다.
	bool IsUmbrella(const std::string& n)
	{
		return n.compare(0, 6, "frame.") == 0 || n == "level1.update" || n == "level1.render" ||
			n == "tutorial.update" || n == "tutorial.render" || n == "scene.update" ||
			n == "scene.render.emit";
	}

	// 이번 프레임에서 가장 무거운 (감싸는 스코프가 아닌) 스코프
	void FrameCulprit(std::string* name, double* ms)
	{
		int best = -1;
		for (size_t i = 0; i < g_M.size(); ++i)
		{
			const Metric& m = g_M[i];
			if (m.kind != K_SCOPE || m.frameCalls == 0 || IsUmbrella(m.name)) continue;
			if (best < 0 || m.frameSum > g_M[best].frameSum) best = (int)i;
		}
		if (best < 0) { name->clear(); *ms = 0.0; return; }
		*name = g_M[best].name;
		*ms = g_M[best].frameSum;
	}

	std::string WarnText(const PeriodWarn& w)
	{
		std::string s;
		const std::string& c = w.code;
		if (c == "LOW_FPS")
			Append(s, "FPS 낮음 — 최저 %.1f (목표 %d)", w.value, g_TargetFps);
		else if (c == "FRAME_BUDGET_EXCEEDED")
			Append(s, "CPU 예산 초과 — 느린 프레임 %.1fms (예산 %.1fms)", w.value, w.limit);
		else if (c == "GPU_BOUND")
			Append(s, "GPU 부담 큼 — %.1fms (기준 %.1fms)", w.value, w.limit);
		else if (c == "DRAW_CALLS_HIGH")
			Append(s, "DrawCall 많음 — %.0f회 (기준 %.0f)", w.value, w.limit);
		else if (c == "INSTANCES_HIGH")
			Append(s, "도형 많음 — %.0f개 (기준 %.0f)", w.value, w.limit);
		else if (c == "UPLOAD_HIGH")
			Append(s, "GPU 업로드 많음 — %.1fMB/프레임", w.value / (1024.0 * 1024.0));
		else if (c == "GLYPH_BUILD_RUNTIME")
			Append(s, "새 글자 %.0f개 생성 — 처음 나온 글자에서 잠깐 멈출 수 있음", w.value);
		else if (c == "ATLAS_NEAR_FULL")
			Append(s, "글자 텍스처 %.0f%% 참 — 가득 차면 한 번 비운다", w.value);
		else if (c == "ATLAS_RESET")
			Append(s, "글자 텍스처를 비웠음 %.0f회 — 그 프레임 글자가 깨질 수 있음", w.value);
		else if (c == "CULLING_INEFFECTIVE")
			Append(s, "화면 밖 제외(컬링)가 안 됨 — 경계 없는 액터가 있는지 확인");
		else if (c == "QUADRATIC_GROWTH")
			Append(s, "몬스터 밀어내기 비교 %.0f회/프레임 — 수가 늘면 급격히 느려짐", w.value);
		else if (c == "MEMORY_GROWTH")
			Append(s, "메모리 +%.0fMB 증가 — 누수 의심", w.value);
		else if (c == "ACTOR_UPDATE_SLOW" || c == "ACTOR_RENDER_SLOW")
			Append(s, "%s 느림 — %.1fms/프레임 (기준 %.1fms)",
				FriendlyName(w.subject).c_str(), w.value, w.limit);
		else
			Append(s, "%s %.2f / %.2f", c.c_str(), w.value, w.limit);
		return s;
	}

	// 세션 요약용 짧은 이름
	std::string ShortKo(const std::string& c)
	{
		if (c == "LOW_FPS")               return "FPS낮음";
		if (c == "FRAME_BUDGET_EXCEEDED") return "CPU예산초과";
		if (c == "HITCH")                 return "끊김";
		if (c == "GPU_BOUND")             return "GPU부담";
		if (c == "DRAW_CALLS_HIGH")       return "DrawCall많음";
		if (c == "INSTANCES_HIGH")        return "도형많음";
		if (c == "UPLOAD_HIGH")           return "업로드많음";
		if (c == "GLYPH_BUILD_RUNTIME")   return "새글자생성";
		if (c == "ATLAS_NEAR_FULL")       return "글자텍스처거의참";
		if (c == "ATLAS_RESET")           return "글자텍스처초기화";
		if (c == "CULLING_INEFFECTIVE")   return "컬링안됨";
		if (c == "QUADRATIC_GROWTH")      return "몬스터비교과다";
		if (c == "MEMORY_GROWTH")         return "메모리증가";
		if (c == "ACTOR_UPDATE_SLOW")     return "액터갱신느림";
		if (c == "ACTOR_RENDER_SLOW")     return "액터그리기느림";
		return c;
	}

	bool IsSevere(const std::string& code)
	{
		return code == "LOW_FPS" || code == "FRAME_BUDGET_EXCEEDED" ||
			code == "GPU_BOUND" || code == "HITCH";
	}

	std::string Thousands(double v)
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "%.0f", v);
		std::string d(buf), out;
		int n = (int)d.size();
		for (int i = 0; i < n; ++i)
		{
			out += d[i];
			int rest = n - 1 - i;
			if (rest > 0 && rest % 3 == 0 && d[i] != '-') out += ',';
		}
		return out;
	}

	std::string Clock(double ms)
	{
		int s = (int)(ms / 1000.0);
		char buf[32];
		if (s >= 3600) snprintf(buf, sizeof(buf), "%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
		else           snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
		return buf;
	}

	void AccumulatePeriod(double spanMs, double fps, double workAvg, bool warm,
		const std::vector<Warn>& warns);
	void PrintPeriod(double now);

	// ---- 구간 집계 --------------------------------------------------------

	void FlushWindow(double now)
	{
		double span = now - g_WinStart;
		if (span <= 0.0 || g_WinFrames == 0) return;

		double tSec = (now - g_FirstMark) / 1000.0;
		bool warm = (now - g_FirstMark) < WARMUP_MS;

		double fps = g_WinFrames * 1000.0 / span;
		double workAvg = Avg(g_WinWork);
		double workP95 = Percentile(g_WinWork, 0.95);

		// ---- 경고 -----------------------------------------------------------
		std::vector<Warn> warns;
		if (!warm)
		{
			if (g_TargetFps > 0 && fps < g_TargetFps * 0.9)
				AddWarn(warns, "LOW_FPS", fps, g_TargetFps * 0.9);
			if (workP95 > g_Budget)
				AddWarn(warns, "FRAME_BUDGET_EXCEEDED", workP95, g_Budget);
			if (g_WinHitches > 0)
				AddWarn(warns, "HITCH", (double)g_WinHitches, 0.0);

			double gpu = WinSampleAvg(ID_GPU_FRAME);
			if (gpu > g_Budget * 0.8)
				AddWarn(warns, "GPU_BOUND", gpu, g_Budget * 0.8);

			double dc = WinAvgPerFrame(ID_DRAW_CALLS);
			if (dc > LIMIT_DRAW_CALLS) AddWarn(warns, "DRAW_CALLS_HIGH", dc, LIMIT_DRAW_CALLS);

			double inst = WinAvgPerFrame(ID_INSTANCES);
			if (inst > LIMIT_INSTANCES) AddWarn(warns, "INSTANCES_HIGH", inst, LIMIT_INSTANCES);

			double up = WinAvgPerFrame(ID_UPLOAD);
			if (up > LIMIT_UPLOAD_BYTES) AddWarn(warns, "UPLOAD_HIGH", up, LIMIT_UPLOAD_BYTES);

			if (g_M[ID_GLYPH_BUILDS].winSum > 0.0)
				AddWarn(warns, "GLYPH_BUILD_RUNTIME", g_M[ID_GLYPH_BUILDS].winSum, 0.0);
			if (g_M[ID_ATLAS_RESETS].winSum > 0.0)
				AddWarn(warns, "ATLAS_RESET", g_M[ID_ATLAS_RESETS].winSum, 0.0);
			if (g_M[ID_ATLAS_PCT].gaugeSet && g_M[ID_ATLAS_PCT].gauge > LIMIT_ATLAS_PCT)
				AddWarn(warns, "ATLAS_NEAR_FULL", g_M[ID_ATLAS_PCT].gauge, LIMIT_ATLAS_PCT);

			double nodes = WinAvgPerFrame(ID_SCENE_NODES);
			double culled = WinAvgPerFrame(ID_SCENE_CULLED);
			if (nodes > 500.0 && culled < 1.0)
				AddWarn(warns, "CULLING_INEFFECTIVE", culled, 1.0);

			double sep = WinAvgPerFrame(ID_SEP_PAIRS);
			if (sep > LIMIT_SEP_PAIRS) AddWarn(warns, "QUADRATIC_GROWTH", sep, LIMIT_SEP_PAIRS);

			const Metric& ws = g_M[ID_MEM_WS];
			if (ws.gaugeSet && ws.gaugeFirstSet && ws.gauge - ws.gaugeFirst > LIMIT_MEM_GROWTH_MB)
				AddWarn(warns, "MEMORY_GROWTH", ws.gauge - ws.gaugeFirst, LIMIT_MEM_GROWTH_MB);

			for (size_t i = 0; i < g_M.size(); ++i)
			{
				const Metric& m = g_M[i];
				if (m.kind != K_SCOPE) continue;
				double a = m.winSum / (double)g_WinFrames;
				if (a <= LIMIT_ACTOR_CLASS_MS) continue;
				if (m.name.compare(0, 19, "scene.update.actor.") == 0)
					AddWarn(warns, "ACTOR_UPDATE_SLOW", a, LIMIT_ACTOR_CLASS_MS, m.name.c_str());
				else if (m.name.compare(0, 19, "scene.render.actor.") == 0)
					AddWarn(warns, "ACTOR_RENDER_SLOW", a, LIMIT_ACTOR_CLASS_MS, m.name.c_str());
			}
		}

		// ---- JSON -----------------------------------------------------------
		std::string s;
		Append(s, "{\"type\":\"window\",\"schema\":\"%s\",\"t_s\":%.3f,\"frame_index\":%lld,"
			"\"warmup\":%s,\"frames\":%d,\"fps\":%.2f,\"target_fps\":%d,\"budget_ms\":%.3f,",
			SCHEMA, tSec, g_Frame, warm ? "true" : "false", g_WinFrames, fps, g_TargetFps, g_Budget);

		Append(s, "\"frame\":{\"interval_ms\":{\"avg\":%.3f,\"p95\":%.3f,\"max\":%.3f},"
			"\"cpu_work_ms\":{\"avg\":%.3f,\"p95\":%.3f,\"max\":%.3f},"
			"\"headroom_ms\":%.3f,\"hitches\":%d},",
			Avg(g_WinInterval), Percentile(g_WinInterval, 0.95), Max(g_WinInterval),
			workAvg, workP95, Max(g_WinWork), g_Budget - workAvg, g_WinHitches);

		// 스코프 — 평균 시간 내림차순. AI 가 앞에서부터 읽으면 된다
		std::vector<int> order;
		for (size_t i = 0; i < g_M.size(); ++i)
			if (g_M[i].kind == K_SCOPE && g_M[i].winCalls > 0) order.push_back((int)i);
		struct ByAvg
		{
			bool operator()(int a, int b) const { return g_M[a].winSum > g_M[b].winSum; }
		};
		std::sort(order.begin(), order.end(), ByAvg());

		s += "\"cpu_ms\":{";
		for (size_t k = 0; k < order.size(); ++k)
		{
			const Metric& m = g_M[order[k]];
			Append(s, "%s\"%s\":{\"avg\":%.4f,\"max\":%.4f,\"calls_per_frame\":%.2f}",
				k ? "," : "", m.name.c_str(), m.winSum / g_WinFrames, m.winMax,
				(double)m.winCalls / g_WinFrames);
		}
		s += "},\"counters\":{";
		bool first = true;
		for (size_t i = 0; i < g_M.size(); ++i)
		{
			const Metric& m = g_M[i];
			if (m.kind != K_COUNTER) continue;
			Append(s, "%s\"%s\":{\"avg\":%.2f,\"max\":%.2f,\"sum\":%.0f}",
				first ? "" : ",", m.name.c_str(), m.winSum / g_WinFrames, m.winMax, m.winSum);
			first = false;
		}
		s += "},\"samples\":{";
		first = true;
		for (size_t i = 0; i < g_M.size(); ++i)
		{
			const Metric& m = g_M[i];
			if (m.kind != K_SAMPLE || m.winCalls == 0) continue;
			Append(s, "%s\"%s\":{\"avg\":%.4f,\"max\":%.4f,\"n\":%d}",
				first ? "" : ",", m.name.c_str(), m.winSum / m.winCalls, m.winMax, m.winCalls);
			first = false;
		}
		s += "},\"gauges\":{";
		first = true;
		for (size_t i = 0; i < g_M.size(); ++i)
		{
			const Metric& m = g_M[i];
			if (m.kind != K_GAUGE || !m.gaugeSet) continue;
			Append(s, "%s\"%s\":%.3f", first ? "" : ",", m.name.c_str(), m.gauge);
			first = false;
		}
		s += "},\"warnings\":[";
		for (size_t i = 0; i < warns.size(); ++i)
		{
			Append(s, "%s{\"code\":\"%s\",\"value\":%.3f,\"limit\":%.3f",
				i ? "," : "", warns[i].code.c_str(), warns[i].value, warns[i].limit);
			if (!warns[i].subject.empty())
				Append(s, ",\"subject\":\"%s\"", warns[i].subject.c_str());
			s += "}";
		}
		s += "]}";
		WriteLine(s);

		// ---- 콘솔 -----------------------------------------------------------
		// 1초마다 찍으면 읽기 전에 밀려 올라간다. 구간을 모아 5초마다 한 번 낸다.
		AccumulatePeriod(span, fps, workAvg, warm, warns);
		if (now - g_Period.start >= CONSOLE_PERIOD_MS) PrintPeriod(now);

		// ---- 리셋 -----------------------------------------------------------
		for (size_t i = 0; i < g_M.size(); ++i)
		{
			g_M[i].winSum = 0.0;
			g_M[i].winMax = 0.0;
			g_M[i].winCalls = 0;
		}
		g_WinStart = now;
		g_WinFrames = 0;
		g_WinInterval.clear();
		g_WinWork.clear();
		g_WinHitches = 0;
		g_WinHitchRecords = 0;
	}

	void WriteHitch(double now, double interval, double work)
	{
		std::vector<int> order;
		for (size_t i = 0; i < g_M.size(); ++i)
			if (g_M[i].kind == K_SCOPE && g_M[i].frameCalls > 0) order.push_back((int)i);
		struct ByFrame
		{
			bool operator()(int a, int b) const { return g_M[a].frameSum > g_M[b].frameSum; }
		};
		std::sort(order.begin(), order.end(), ByFrame());

		std::string s;
		Append(s, "{\"type\":\"hitch\",\"schema\":\"%s\",\"t_s\":%.3f,\"frame_index\":%lld,"
			"\"warmup\":%s,\"interval_ms\":%.3f,\"cpu_work_ms\":%.3f,\"budget_ms\":%.3f,\"top_cpu_ms\":[",
			SCHEMA, (now - g_FirstMark) / 1000.0, g_Frame,
			(now - g_FirstMark) < WARMUP_MS ? "true" : "false", interval, work, g_Budget);
		for (size_t k = 0; k < order.size() && k < 8; ++k)
		{
			Append(s, "%s{\"name\":\"%s\",\"ms\":%.4f}", k ? "," : "",
				g_M[order[k]].name.c_str(), g_M[order[k]].frameSum);
		}
		s += "],\"counters\":{";
		bool first = true;
		for (size_t i = 0; i < g_M.size(); ++i)
		{
			const Metric& m = g_M[i];
			if (m.kind != K_COUNTER || m.frameSum == 0.0) continue;
			Append(s, "%s\"%s\":%.0f", first ? "" : ",", m.name.c_str(), m.frameSum);
			first = false;
		}
		s += "}}";
		WriteLine(s);
	}

	void AccumulatePeriod(double spanMs, double fps, double workAvg, bool warm,
		const std::vector<Warn>& warns)
	{
		Period& p = g_Period;
		p.spanMs += spanMs;
		p.frames += g_WinFrames;
		if (fps < p.fpsMin) p.fpsMin = fps;
		p.workSum += workAvg * g_WinFrames;
		p.updSum  += g_M[ID_UPDATE].winSum;
		p.rendSum += g_M[ID_RENDER].winSum;
		p.swapSum += g_M[ID_SWAP].winSum;
		double gpu = WinSampleAvg(ID_GPU_FRAME);
		// 시작 직후 GPU 타이머는 셰이더 컴파일 등이 섞여 수 초로 튄다. 평균을 망치므로 뺀다
		if (gpu >= 0.0 && !warm) { p.gpuSum += gpu; p.gpuN++; }
		p.dcSum   += g_M[ID_DRAW_CALLS].winSum;
		p.instSum += g_M[ID_INSTANCES].winSum;
		p.hitches += g_WinHitches;
		if (warm) p.warm = true;

		for (size_t i = 0; i < g_M.size(); ++i)
		{
			const Metric& m = g_M[i];
			if (m.kind != K_SCOPE || m.winCalls == 0) continue;
			if (m.name.compare(0, 19, "scene.update.actor.") == 0 ||
				m.name.compare(0, 19, "scene.render.actor.") == 0)
				p.actorMs[m.name] += m.winSum;
		}

		// 같은 경고는 구간 안에서 한 줄로 합치고 가장 나쁜 값을 남긴다
		for (size_t i = 0; i < warns.size(); ++i)
		{
			if (warns[i].code == "HITCH") continue;   // 끊김은 따로 센다
			bool merged = false;
			for (size_t k = 0; k < p.warns.size(); ++k)
			{
				PeriodWarn& pw = p.warns[k];
				if (pw.code != warns[i].code || pw.subject != warns[i].subject) continue;
				bool lowerIsWorse = (pw.code == "LOW_FPS");
				if (lowerIsWorse ? warns[i].value < pw.value : warns[i].value > pw.value)
					pw.value = warns[i].value;
				merged = true;
				break;
			}
			if (!merged)
			{
				PeriodWarn pw;
				pw.code = warns[i].code;
				pw.subject = warns[i].subject;
				pw.value = warns[i].value;
				pw.limit = warns[i].limit;
				p.warns.push_back(pw);
			}
		}
	}

	void PrintPeriod(double now)
	{
		Period& p = g_Period;
		if (p.frames == 0) { ResetPeriod(now); return; }

		double fps  = p.frames * 1000.0 / p.spanMs;
		double work = p.workSum / p.frames;
		double upd  = p.updSum / p.frames;
		double rnd  = p.rendSum / p.frames;
		double swp  = p.swapSum / p.frames;
		double dc   = p.dcSum / p.frames;
		double inst = p.instSum / p.frames;

		// ---- 상태 한 단어 -------------------------------------------------------
		bool severe = p.hitches > 0;
		for (size_t i = 0; i < p.warns.size(); ++i) if (IsSevere(p.warns[i].code)) severe = true;
		const char* state;
		const char* stateClr;
		if (p.warm)                 { state = "시작 중"; stateClr = C_DIM; }
		else if (severe)            { state = "경고";    stateClr = C_RED; }
		else if (!p.warns.empty())  { state = "주의";    stateClr = C_YELLOW; }
		else                        { state = "정상";    stateClr = C_GREEN; }

		const char* fpsClr = C_GREEN;
		if (g_TargetFps > 0)
		{
			if (fps < g_TargetFps * 0.75) fpsClr = C_RED;
			else if (fps < g_TargetFps * 0.95) fpsClr = C_YELLOW;
		}

		std::string out;
		Append(out, "\n%s── 성능 %s ──────────────────────────────%s %s%s%s\n",
			Clr(C_CYAN), Clock(now - g_FirstMark).c_str(), Clr(C_RESET),
			Clr(stateClr), state, Clr(C_RESET));

		Append(out, "  FPS  %s%5.1f%s  %s(최저 %.1f, 목표 %d)%s\n",
			Clr(fpsClr), fps, Clr(C_RESET), Clr(C_DIM), p.fpsMin, g_TargetFps, Clr(C_RESET));

		Append(out, "  시간  CPU %.2fms %s(갱신 %.2f + 그리기 %.2f)%s",
			work, Clr(C_DIM), upd, rnd, Clr(C_RESET));
		if (p.gpuN > 0) Append(out, "  ·  GPU %.2fms", p.gpuSum / p.gpuN);
		Append(out, "  ·  교체 대기 %.2fms  %s/ 예산 %.1fms%s\n",
			swp, Clr(C_DIM), g_Budget, Clr(C_RESET));

		Append(out, "  그리기  DrawCall %.0f  ·  도형 %s", dc, Thousands(inst).c_str());

		// 가장 무거운 액터 클래스
		std::string heavy;
		double heavyMs = 0.0;
		for (std::map<std::string, double>::iterator it = p.actorMs.begin(); it != p.actorMs.end(); ++it)
		{
			if (it->second > heavyMs) { heavyMs = it->second; heavy = it->first; }
		}
		if (!heavy.empty())
			Append(out, "  ·  가장 무거운 액터: %s %.2fms", FriendlyName(heavy).c_str(), heavyMs / p.frames);
		out += "\n";

		// ---- 경고 --------------------------------------------------------------
		if (p.hitches > 0)
		{
			Append(out, "  %s! 끊김 %d회%s — 가장 긴 프레임 %.0fms",
				Clr(C_RED), p.hitches, Clr(C_RESET), p.worstMs);
			if (!p.worstCause.empty())
				Append(out, " (원인: %s %.1fms)", FriendlyName(p.worstCause).c_str(), p.worstCauseMs);
			if (p.warm) Append(out, " %s— 시작 비용, 무시해도 됨%s", Clr(C_DIM), Clr(C_RESET));
			out += "\n";
		}
		for (size_t i = 0; i < p.warns.size(); ++i)
		{
			const char* c = IsSevere(p.warns[i].code) ? C_RED : C_YELLOW;
			Append(out, "  %s!%s %s\n", Clr(c), Clr(C_RESET), WarnText(p.warns[i]).c_str());
		}

		fputs(out.c_str(), stdout);
		fflush(stdout);

		ResetPeriod(now);
	}
}

// =============================================================================
// 공개 API
// =============================================================================

void Init(const char* logDir)
{
	g_T0 = std::chrono::steady_clock::now();

	Platform::EnsureDirectory(logDir);

	char stamp[32];
	Platform::LocalTimestampCompact(stamp, sizeof(stamp));
	char name[64];
	snprintf(name, sizeof(name), "perf_%s.jsonl", stamp);

	g_FilePath = std::string(logDir) + "/" + name;
	if (fopen_s(&g_File, g_FilePath.c_str(), "w") != 0) g_File = 0;

	std::string latest = std::string(logDir) + "/perf_latest.txt";
	FILE* lf = 0;
	if (fopen_s(&lf, latest.c_str(), "w") == 0 && lf)
	{
		fprintf(lf, "%s\n", name);
		fclose(lf);
	}

	ID_UPDATE       = RegisterScope("frame.update");
	ID_RENDER       = RegisterScope("frame.render");
	ID_SWAP         = RegisterScope("frame.swap");
	ID_DRAW_CALLS   = RegisterCounter("render.draw_calls");
	ID_INSTANCES    = RegisterCounter("render.instances");
	ID_UPLOAD       = RegisterCounter("render.upload_bytes");
	ID_GLYPH_BUILDS = RegisterCounter("text.glyph_builds");
	ID_ATLAS_RESETS = RegisterCounter("text.atlas_resets");
	ID_ATLAS_PCT    = RegisterGauge("text.atlas_used_pct");
	ID_GPU_FRAME    = RegisterSample("gpu.frame_ms");
	ID_SCENE_NODES  = RegisterCounter("scene.nodes");
	ID_SCENE_CULLED = RegisterCounter("scene.culled_subtrees");
	ID_SEP_PAIRS    = RegisterCounter("level1.monster_sep_pairs");
	ID_MEM_WS       = RegisterGauge("mem.working_set_mb");
	ID_MEM_PRIVATE  = RegisterGauge("mem.private_mb");

	g_On = true;
	g_Color = Platform::EnableConsoleColors();
	ResetPeriod(0.0);

	printf("%s[성능]%s 5초마다 요약을 출력합니다. 게임 화면 FPS 는 F3 으로 켜고 끕니다.\n"
		"       자세한 기록: %s\n",
		Clr(C_CYAN), Clr(C_RESET), g_FilePath.c_str());
}

void WriteSessionStart(const SessionInfo& info)
{
	g_TargetFps = info.targetFps;
	g_Budget = 1000.0 / (info.targetFps > 0 ? info.targetFps : 60);

	char when[32];
	Platform::LocalTimestamp(when, sizeof(when));

	std::string s;
	Append(s, "{\"type\":\"session_start\",\"schema\":\"%s\",\"local_time\":\"%s\","
		"\"build\":\"%s\",\"gl_renderer\":\"%s\",\"gl_version\":\"%s\","
		"\"resolution\":[%d,%d],\"target_fps\":%d,\"vsync\":%d,\"budget_ms\":%.3f,"
		"\"startup_ms\":%.2f,\"warmup_ms\":%.0f}",
		SCHEMA, when, info.build,
		info.glRenderer ? info.glRenderer : "", info.glVersion ? info.glVersion : "",
		info.width, info.height, info.targetFps, info.vsync, g_Budget, info.startupMs,
		WARMUP_MS);
	WriteLine(s);
}

void Shutdown()
{
	if (!g_On) return;

	double now = NowMs();
	if (g_WinFrames > 0) FlushWindow(now);
	if (g_Period.frames > 0) PrintPeriod(now);

	std::string s;
	double seconds = (g_FirstMark >= 0.0) ? (now - g_FirstMark) / 1000.0 : 0.0;
	Append(s, "{\"type\":\"session_end\",\"schema\":\"%s\",\"t_s\":%.3f,\"frames\":%lld,"
		"\"fps_avg\":%.2f,\"frame_interval_ms\":{\"p50\":%.3f,\"p95\":%.3f,\"p99\":%.3f,\"max\":%.3f},"
		"\"hitches\":%lld,\"worst_frames\":[",
		SCHEMA, seconds, g_Frame, seconds > 0.0 ? g_Frame / seconds : 0.0,
		Percentile(g_SesInterval, 0.50), Percentile(g_SesInterval, 0.95),
		Percentile(g_SesInterval, 0.99), Max(g_SesInterval), g_SesHitches);
	for (size_t i = 0; i < g_Worst.size(); ++i)
	{
		Append(s, "%s{\"frame_index\":%lld,\"t_s\":%.3f,\"interval_ms\":%.3f}",
			i ? "," : "", g_Worst[i].frame, g_Worst[i].t, g_Worst[i].ms);
	}
	s += "],\"cpu_ms\":{";
	bool first = true;
	for (size_t i = 0; i < g_M.size(); ++i)
	{
		const Metric& m = g_M[i];
		if (m.kind != K_SCOPE || m.sesCalls == 0 || g_Frame == 0) continue;
		Append(s, "%s\"%s\":{\"avg\":%.4f,\"max\":%.4f}", first ? "" : ",",
			m.name.c_str(), m.sesSum / (double)g_Frame, m.sesMax);
		first = false;
	}
	s += "},\"counters\":{";
	first = true;
	for (size_t i = 0; i < g_M.size(); ++i)
	{
		const Metric& m = g_M[i];
		if (m.kind != K_COUNTER || g_Frame == 0) continue;
		Append(s, "%s\"%s\":{\"avg\":%.2f,\"max\":%.2f,\"sum\":%.0f}", first ? "" : ",",
			m.name.c_str(), m.sesSum / (double)g_Frame, m.sesMax, m.sesSum);
		first = false;
	}
	s += "},\"gauges_max\":{";
	first = true;
	for (size_t i = 0; i < g_M.size(); ++i)
	{
		const Metric& m = g_M[i];
		if (m.kind != K_GAUGE || !m.gaugeSet) continue;
		Append(s, "%s\"%s\":%.3f", first ? "" : ",", m.name.c_str(), m.gaugeMax);
		first = false;
	}
	s += "},\"warnings_seen\":{";
	first = true;
	for (std::map<std::string, int>::iterator it = g_WarnSeen.begin(); it != g_WarnSeen.end(); ++it)
	{
		Append(s, "%s\"%s\":%d", first ? "" : ",", it->first.c_str(), it->second);
		first = false;
	}
	s += "}}";
	WriteLine(s);

	// ---- 콘솔 세션 요약 --------------------------------------------------------
	{
		std::string out;
		Append(out, "\n%s══ 성능 요약 ══%s  %s 동안 %lld 프레임\n",
			Clr(C_CYAN), Clr(C_RESET), Clock(now - (g_FirstMark >= 0.0 ? g_FirstMark : now)).c_str(), g_Frame);
		Append(out, "  평균 FPS %.1f  ·  프레임 시간 보통 %.1fms / 느린 5%% %.1fms / 최악 %.0fms\n",
			seconds > 0.0 ? g_Frame / seconds : 0.0,
			Percentile(g_SesInterval, 0.50), Percentile(g_SesInterval, 0.95), Max(g_SesInterval));
		Append(out, "  끊김 %lld회", g_SesHitches);
		if (!g_WarnSeen.empty())
		{
			out += "  ·  경고가 났던 구간:";
			for (std::map<std::string, int>::iterator it = g_WarnSeen.begin(); it != g_WarnSeen.end(); ++it)
				Append(out, " %s %d초", ShortKo(it->first).c_str(), it->second);
		}
		Append(out, "\n  자세한 기록: %s\n", g_FilePath.c_str());
		fputs(out.c_str(), stdout);
		fflush(stdout);
	}

	if (g_File) fclose(g_File);
	g_File = 0;
	g_On = false;
}

bool Enabled()
{
	return g_On;
}

double DisplayFps()     { return g_DisplayFps; }
double DisplayFrameMs() { return g_DisplayMs; }
int    TargetFps()      { return g_TargetFps; }

double NowMs()
{
	std::chrono::duration<double, std::milli> d = std::chrono::steady_clock::now() - g_T0;
	return d.count();
}

int RegisterScope(const char* name)   { return Register(name, K_SCOPE); }
int RegisterCounter(const char* name) { return Register(name, K_COUNTER); }
int RegisterGauge(const char* name)   { return Register(name, K_GAUGE); }
int RegisterSample(const char* name)  { return Register(name, K_SAMPLE); }

void AddTime(int id, double ms)
{
	if (!g_On || id < 0 || id >= (int)g_M.size()) return;
	g_M[id].frameSum += ms;
	g_M[id].frameCalls++;
}

void AddCount(int id, double v)
{
	if (!g_On || id < 0 || id >= (int)g_M.size()) return;
	g_M[id].frameSum += v;
	g_M[id].frameCalls++;
}

void SetGauge(int id, double v)
{
	if (!g_On || id < 0 || id >= (int)g_M.size()) return;
	Metric& m = g_M[id];
	m.gauge = v;
	m.gaugeSet = true;
	if (!m.gaugeFirstSet && g_FirstMark >= 0.0 && NowMs() - g_FirstMark > WARMUP_MS)
	{
		// 메모리 증가 기준점은 시작 비용이 끝난 뒤의 값으로 잡는다
		m.gaugeFirst = v;
		m.gaugeFirstSet = true;
	}
	if (v > m.gaugeMax) m.gaugeMax = v;
}

void AddSample(int id, double v)
{
	if (!g_On || id < 0 || id >= (int)g_M.size()) return;
	Metric& m = g_M[id];
	m.winSum += v;
	m.winCalls++;
	if (v > m.winMax) m.winMax = v;
	m.sesSum += v;
	m.sesCalls++;
	if (v > m.sesMax) m.sesMax = v;
}

void Event(const char* name, const char* fieldsJson)
{
	if (!g_On) return;
	double now = NowMs();
	std::string s;
	Append(s, "{\"type\":\"event\",\"schema\":\"%s\",\"t_s\":%.3f,\"frame_index\":%lld,\"name\":\"%s\"",
		SCHEMA, g_FirstMark >= 0.0 ? (now - g_FirstMark) / 1000.0 : 0.0, g_Frame, name);
	if (fieldsJson && fieldsJson[0])
	{
		s += ",";
		s += fieldsJson;
	}
	s += "}";
	WriteLine(s);
}

void FrameMark()
{
	if (!g_On) return;

	double now = NowMs();

	if (g_FirstMark < 0.0)
	{
		// 첫 프레임 이전(시작 비용)은 session_start 의 startup_ms 로 따로 남겼다
		g_FirstMark = now;
		g_LastMark = now;
		g_WinStart = now;
		ResetPeriod(now);
		for (size_t i = 0; i < g_M.size(); ++i)
		{
			g_M[i].frameSum = 0.0;
			g_M[i].frameCalls = 0;
		}
		return;
	}

	double interval = now - g_LastMark;
	g_LastMark = now;
	++g_Frame;

	double work = g_M[ID_UPDATE].frameSum + g_M[ID_RENDER].frameSum;

	// 메모리는 싸지 않으니 구간당 한 번쯤이면 충분하다
	if ((g_Frame % 30) == 0)
	{
		SetGauge(ID_MEM_WS, Platform::WorkingSetMB());
		SetGauge(ID_MEM_PRIVATE, Platform::PrivateBytesMB());
	}

	// 화면 표시용 FPS: 최근 0.5초 평균을 0.25초마다 갱신
	g_Recent.push_back((float)interval);
	{
		double sum = 0.0;
		int n = 0;
		for (int i = (int)g_Recent.size() - 1; i >= 0 && sum < 500.0; --i) { sum += g_Recent[i]; ++n; }
		if ((int)g_Recent.size() > n + 8) g_Recent.erase(g_Recent.begin(), g_Recent.end() - n);
		if (now - g_DisplayAt >= 250.0 && n > 0 && sum > 0.0)
		{
			g_DisplayAt = now;
			g_DisplayFps = n * 1000.0 / sum;
			g_DisplayMs = sum / n;
		}
	}

	if (interval > g_Budget * 2.0)
	{
		++g_WinHitches;
		++g_SesHitches;
		if (interval > g_Period.worstMs)
		{
			g_Period.worstMs = interval;
			FrameCulprit(&g_Period.worstCause, &g_Period.worstCauseMs);
		}
		if (g_WinHitchRecords < MAX_HITCH_RECORDS)
		{
			++g_WinHitchRecords;
			WriteHitch(now, interval, work);
		}
	}

	// 최악 프레임 상위 10
	Worst w;
	w.frame = g_Frame;
	w.ms = interval;
	w.t = (now - g_FirstMark) / 1000.0;
	if (g_Worst.size() < 10 || interval > g_Worst.back().ms)
	{
		g_Worst.push_back(w);
		struct ByMs
		{
			bool operator()(const Worst& a, const Worst& b) const { return a.ms > b.ms; }
		};
		std::sort(g_Worst.begin(), g_Worst.end(), ByMs());
		if (g_Worst.size() > 10) g_Worst.resize(10);
	}

	for (size_t i = 0; i < g_M.size(); ++i)
	{
		Metric& m = g_M[i];
		if (m.kind == K_SCOPE || m.kind == K_COUNTER)
		{
			m.winSum += m.frameSum;
			if (m.frameSum > m.winMax) m.winMax = m.frameSum;
			m.winCalls += m.frameCalls;
			m.sesSum += m.frameSum;
			if (m.frameSum > m.sesMax) m.sesMax = m.frameSum;
			m.sesCalls += m.frameCalls;
			m.lastFrame = m.frameSum;
		}
		m.frameSum = 0.0;
		m.frameCalls = 0;
	}

	g_WinInterval.push_back((float)interval);
	g_WinWork.push_back((float)work);
	g_SesInterval.push_back((float)interval);
	++g_WinFrames;

	if (now - g_WinStart >= 1000.0) FlushWindow(now);
}

} // namespace Profiler
