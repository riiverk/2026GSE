#pragma once

class Renderer;
class JsonWriter;

// -----------------------------------------------------------------------------
// 레벨 1 — 사냥터
//
// 목적은 단순하다. 때리고, 줍고, 경험치를 쌓고, 오른 레벨만큼 스탯을 찍는다.
// RPG 의 기본 루프가 한 바퀴 도는지 확인하는 레벨이다.
//
// 맵은 매 실행마다 무작위로 생성되며, BSP 로 나눈 공터를 부모가 자식끼리
// 이어 주는 방식이라 구조적으로 고립 영역이 생기지 않는다. 생성 후에도
// 플러드 필로 한 번 더 검증한다.
// -----------------------------------------------------------------------------
namespace Level1
{
	void Init(Renderer* renderer, int screenW, int screenH);
	void Reset(unsigned int seed);      // seed 0 이면 시간 기반

	void Update(float dt);
	void Render();

	void SetKey(unsigned char key, bool down);
	void KeyPressed(unsigned char key);

	// 모니터 (Tools/monitor.html) — 열린 "maps" 배열에 이 맵 객체를 붙인다
	void WriteMonitorMap(JsonWriter& w, bool active);
}
