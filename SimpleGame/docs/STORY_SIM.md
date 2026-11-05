# STORY_SIM.md — 그랑 구스토 스토리 & 오픈월드 시뮬레이션 명세

**버전** v0.2 (2026-09-29) — 스토리를 소년만화(토리코 계열) 방향으로 전면 교체
**대상** Claude Code — `CLAUDE.md`(= 프로젝트 문서 `claude/game-dev-guidelines.md`)와 함께 읽는다
**관련 문서** `기획서.md`, `튜토리얼_레벨설계.md`, `PERF_LOG.md`

> 이 문서는 **확정된 줄거리가 없는 스토리**를 코드로 돌리기 위한 명세다.
> 고정되는 것은 과거, 시작 상태, 인물 설정뿐이다. 그 이후의 사건은 NPC의 독자 행동,
> 월드 시간, 플레이어 개입이 만든다. 수치와 데이터는 전부 초기값이며 튜닝 대상이다.
>
> v0.1 대비: 보호·폭로 중심의 이야기를 **전설 식재료 레이스**로 교체. 이야기(2장),
> 월드 상태·세력·계획·사실·결말·데이터(5~15장)가 바뀌었고, 시스템 규칙(4, 7~11, 16~18장)은
> 거의 그대로다.

---

## 0. Claude Code 작업 규칙 (이 명세 한정)

1. **시뮬레이션은 순수 로직이다.** `namespace Sim` 안의 코드는 GL, Renderer, Actor,
   `windows.h`를 참조하지 않는다. 화면 쪽(Actor, UI)이 Sim 상태를 **읽기만** 한다.
   Sim 상태 변경은 Sim이 공개한 함수(§16.2)로만 한다.
2. **콘텐츠는 데이터 파일에 둔다.** NPC, 세력, 전설 식재료, 계획, 행동, 사실, 에필로그는
   `Data/Sim/*.ggd`에 둔다. 코드에 NPC 이름·수치·대사를 하드코딩하지 않는다.
3. **한글 문자열 위치 규칙의 예외**: `CLAUDE.md`는 한글 문자열을 `Game.cpp`에 모으도록
   정한다. 이 명세에서 도입하는 **데이터 파일(`*.ggd`)은 예외**로, UTF-8 BOM으로 저장한다.
   Sim 코드(`*.cpp`)에는 한글 문자열을 넣지 않는다. 이 예외를 `CLAUDE.md` 2장에 추가한다.
4. **결정론.** 같은 시드와 같은 입력이면 같은 결과가 나와야 한다. 난수는 Sim 전용
   RNG(§4.3) 하나만 쓰고, `rand()`나 시각 기반 난수를 쓰지 않는다. `std::unordered_map`
   순회로 결과를 내지 않는다. 정의 순서를 보존한 `std::vector` 기준으로 순회한다.
5. **계측.** `PROFILE_SCOPE("sim.tick")` 등을 단다(§17.4). 지표는 `PERF_LOG.md`에 추가한다.
6. **빌드와 실행은 사용자가 한다.** 각 단계의 완료 판정(§18)은 사용자가 실행해서
   확인할 수 있는 형태(디버그 오버레이, 로그, `--sim-test`)로 제공한다.
7. 코드 컨벤션은 `CLAUDE.md` 2.1을 따른다 (탭, Allman, `m_`/`g_` 접두어, PascalCase 함수).

---

## 1. 설계 원칙

```
스토리 = 고정된 과거 + 고정된 시작 상태 + NPC 독자 행동 + 월드 시간 + 플레이어 개입
```

| 원칙 | 코드상 의미 |
|---|---|
| 세계 전체가 레이스다 | 경쟁자들이 각자의 일정으로 전설 식재료를 사냥한다(계획). 먼저 잡은 쪽이 가져간다 |
| NPC는 플레이어를 기다리지 않는다 | 계획은 매일 틱마다 스스로 진행한다. 플레이어 트리거가 필요 없다 |
| 주인공은 혼자다 | 동행 NPC·파티가 없다. 동맹 NPC는 **자기 자리에서 독자적으로** 돕는다. 동행은 타르투(짐승)뿐 |
| 개입하지 않는 것도 선택이다 | 아무것도 안 해도 월드는 결말까지 굴러간다 (`--sim-test`로 검증) |
| 핵심 사실은 경로가 둘 이상 | 로드 시 모든 `fact`의 `source`가 2개 이상인지 검사 |
| 결말은 판정한다 | 엔딩 스크립트가 없다. 미식 일식 날의 상태로 에필로그 조각을 고른다 |
| 예외: 튜토리얼 | 1일차 튜토리얼은 기존 `Game.cpp` 스크립트 그대로. 끝날 때까지 시계 정지 |

---

## 2. 스토리 설정

### 2.1 미식의 황금시대

미식종을 먹으면 그 힘이 몸에 남는 세계다. 사람들은 **전설 식재료**(미식 등급 ★★★)를 쫓는다.
포획 등급과 미식 등급은 별개이므로, 전설 식재료 중에는 섬만 한 괴수도 있고 손바닥만 한 물고기도 있다.

- 요리사의 격은 **별**로 매긴다. 무자격 → 견습 → 일성 → 이성 → 삼성.
- 세계에서 가장 강한 요리사 일곱을 **칠성(七星)**이라 부른다. 한 명 한 명이 재해급 전력이다.
  칠성만이 대륙 끝 **그랑 테라**의 통행증을 가진다.
- **식혼(食魂)**: 전설 식재료를 제대로 조리해 먹으면 그 짐승의 힘이 기술로 남는다
  (먹물 연막, 돌진, 야간 투시 등). 이 게임의 레벨업이다.

### 2.2 그랑 메뉴

모든 요리사의 꿈은 전설 식재료로 채운 인생 코스, **그랑 메뉴**를 완성하는 것이다.

| 칸 id | 이름 | 뜻 |
|---|---|---|
| `antipasto` | 안티파스토 | 전채 |
| `primo` | 프리모 | 파스타·수프 |
| `secondo_fish` | 세콘도 (생선) | 생선 요리 |
| `secondo_meat` | 세콘도 (고기) | 고기 요리 |
| `dolce` | 돌체 | 디저트 |
| `bevanda` | 음료 | 음료 |
| `gran` | (일곱 번째 칸) | 그랑 구스토만 들어갈 수 있다 |

- 기획서의 "성장 = 메뉴판" 장치가 게임 전체의 목표가 된다. 가게 메뉴판 UI가 곧 진행도다.
- 칸마다 후보 식재료가 여럿이라 **플레이어마다 메뉴판이 달라진다.**

### 2.3 그랑 구스토와 미식 일식

**한 그릇에 세계의 모든 맛이 담긴다**는 전설의 식재료.

- 30년마다 오는 **미식 일식** 날에만 그랑 테라에서 모습을 드러낸다. 이번 일식은 **게임 시작 후 111일째**다.
- 그랑 구스토는 **"열쇠의 맛"을 가져온 자에게만** 모습을 보인다. 열쇠는 별비늘 수프다(비밀, §3).
- 칠성, 라이벌, 「검은 식탁」이 모두 이 날을 향해 달린다.

### 2.4 주인공 — 리노

시골 식당집 아이. **혼자서 사냥도 하고 요리도 하는 요리사.** 이 세계에서는 사냥꾼과 요리사가
짝을 이루는 것이 관행이라, 혼자 다니는 리노는 비웃음과 놀라움을 동시에 산다.

- 어머니 **엘레나**는 칠성이었고, 10년 전 그랑 테라 원정에서 **실종**됐다.
- 떠나기 전날 밤 어머니가 끓여 준 수프 한 그릇을 리노는 잊지 못한다.
  *"돌아오면 진짜를 먹여 줄게."*
- 목표: 그랑 메뉴를 채우고, 미식 일식 날 그랑 테라에 가서 그랑 구스토를 먹고, 어머니를 찾는 것.

### 2.5 타르투

리노의 유일한 동행. 손바닥만 한 송로 강아지로, 냄새로 식재료를 찾는다.

- **리노가 전설 식재료를 먹을 때마다 함께 자란다.** 크기 1~4단계. 4단계에서는 리노를 태운다.
- 크기가 곧 탐지 범위다. 몬테 투베로처럼 **타르투가 일정 크기 이상이어야만 찾을 수 있는** 식재료가 있다.
- NPC가 아니다. 시뮬레이션상으로는 `var:tartu_size` 하나로 표현한다.

### 2.6 인물

| 인물 | 역할 | 설정 |
|---|---|---|
| **미라** | 고향 방앗간 주인 | 엘레나의 친구. 엘레나가 남긴 마지막 편지를 보관 |
| **마테오** | 은퇴한 칠성 | 항구의 생선 장수로 위장. 리노의 첫 스승 후보. 옛 통행증을 아직 갖고 있다 |
| **발렌틴** | 라이벌 | 명문가 출신 최연소 이성 요리사. 오만하고 빠르다. 늘 한 발 앞서 전설 식재료를 채 간다. 리노를 **인정**하게 되면 협력자, 무너지면 검은 식탁으로 넘어간다. 어느 쪽이든 동행하지 않는다 |
| **오르소** | 협회 심사관 | 원칙주의자. 협회 안의 검은 식탁 내통자를 의심한다 |
| **세라핀** | 협회 부회장 | **검은 식탁의 내통자.** 칠성 결원 심사를 조작하려 한다 |
| **루치아** | 항구 어민조합장 | 검은 식탁 선단과 바다를 두고 다툰다 |
| **도나토** | 검은 식탁 선단장 | 항구에서 폴포 레를 노린다 |
| **앙셀므** | 구릉 수도원 양조 수도사 | 바카나 축제를 지킨다 |
| **코르비** | 구릉 대지주 | 바카나를 잡아 검은 식탁에 팔려 한다 |
| **그레타** | 산악 사냥꾼 길드장 | 몬테 투베로를 지키는 산의 수호자 |
| **하겐** | 칠성, 북부 숲 은둔자 | 엘레나와 함께 그랑 테라에 갔던 유일한 생환자. **열쇠의 비밀**과 엘레나의 행방을 안다 |
| **세베로** | 「검은 식탁」 수장 | "먹는 자가 세계를 가진다." 전설 식재료를 사육·강제 진화시켜 군대를 만든다. 그랑 구스토를 먹어 세계의 정점에 서려 한다 |

### 2.7 지역과 전설 식재료

| 지역 | 전설 식재료 | 포획 | 조리의 벽 | 칸 | 경쟁자 |
|---|---|---|---|---|---|
| 고향 호수 | **별비늘** | 2 | 봄 산란기에만 나타남 | (열쇠) | 검은 식탁 (열쇠를 알게 되면) |
| 항구 도시 | **리모네 드라고** (레몬 숲의 작은 용) | 4 | 한낮에만 과즙이 오름 | 안티파스토 | — |
| 항구 도시 | **폴포 레** (섬만 한 왕문어) | 7 | 다리 8개의 맛이 전부 달라 다리마다 다른 손질 필요 | 세콘도 (생선) | 발렌틴, 검은 식탁 선단 |
| 구릉 농경지 | **바카나** (걸어 다니는 포도원) | 7 | 열매가 와인으로 익어 있음. 가을 축제 때만 멈춤 | 음료 | 대지주 코르비 |
| 산악 지대 | **페로 토로** (강철 뿔 들소) | 6 | 뿔을 먼저 꺾어야 살이 풀림 | 세콘도 (고기) | — |
| 산악 지대 | **몬테 투베로** (등에 송로 숲이 자라는 산만 한 멧돼지) | 8 | 송로가 떨어지면 1분 안에 썩음. 타르투 3단계 이상 필요 | 프리모 | 검은 식탁 (생포·사육) |
| 북부 숲 | **오로라 사슴** | 9 | 뿔의 오로라는 달빛 아래 훈연해야 굳음. 겨울 밤만 | 돌체 | 발렌틴 |
| 그랑 테라 | **그랑 구스토** | ?? | 열쇠의 맛 | 일곱 번째 | 전원 |

### 2.8 톤 규칙

- 유혈 없음. 쓰러진 식재료는 빛이 되어 재료로 변한다.
- 먹기 전 **감사 의례**를 크게 외친다. 문구: *"네 힘, 한 톨도 남기지 않고 받겠습니다!"*
- 가장 큰 연출은 **먹는 순간**이다. 식혼을 얻는 장면이 곧 레벨업 컷신이다.

---

## 3. 고정된 과거 (연표)

| 시점 | 사건 |
|---|---|
| 40년 전 | 지난 미식 일식. 당시 칠성 누구도 그랑 구스토를 찾지 못함 |
| 20년 전 | 세베로가 협회에서 추방됨(금지된 강제 진화 요리). 「검은 식탁」 결성 |
| 12년 전 | 엘레나가 고향 호수의 별비늘로 수프를 만들어 봄. 그랑 테라 고문서의 "귀향의 맛"이 이것임을 깨달음 |
| 10년 전 | 엘레나와 하겐이 그랑 테라 원정. 떠나기 전날 엘레나가 리노에게 그 수프를 끓여 줌. 원정 중 검은 식탁의 습격, 엘레나 실종, 하겐만 생환해 북부 숲에 은둔 |
| 5년 전 | 세라핀이 검은 식탁과 내통 시작 |
| 현재 | 엘레나의 칠성 자리가 공석. 가을에 결원 심사. 미식 일식까지 111일 |

**비밀**: 그랑 구스토는 "귀향의 맛"(별비늘 수프)을 가져온 자에게만 나타난다. 엘레나는 그랑 테라에서
살아 있으나 검은 식탁에 붙잡혀 있다(열쇠를 끝내 말하지 않았다). 이 사실을 아는 것은 하겐뿐이다.

---

## 4. 월드 시계

### 4.1 시간 단위

| 항목 | 값 | 상수 |
|---|---|---|
| 하루 | 실시간 1200초 (20분) | `SIM_DAY_SECONDS` |
| 하루 구간 | 4개: 새벽 / 낮 / 저녁 / 밤 (각 300초) | `SIM_SEGMENTS` |
| 계절 | 28일 | `SIM_SEASON_DAYS` |
| 계절 순서 | 여름(0) → 가을(1) → 겨울(2) → 봄(3) | `enum Season` |
| 시작 | 절대일 0 = 1년차 여름 1일 | |
| 미식 일식 (결말 판정) | 절대일 111 = 2년차 봄 28일 | `SIM_ECLIPSE_DAY` |

- 전체 112일, 실시간 약 37시간. 수치는 튜닝 대상이다.
- 절대일(`day`, 0부터)만 저장하고 계절·연차·일자는 계산으로 구한다.

```cpp
season      = (day / SIM_SEASON_DAYS) % 4;   // 0 여름
dayOfSeason = day % SIM_SEASON_DAYS + 1;     // 1~28
year        = day / (SIM_SEASON_DAYS * 4) + 1;
```

### 4.2 두 층의 시뮬레이션

| 층 | 대상 | 주기 | 하는 일 |
|---|---|---|---|
| **전략 층** | 전 지역의 모든 NPC·세력·계획 | 하루 1회, 새벽 구간 시작 시 | 계획 진행, NPC 행동, 소문 전파, 시한 플래그 만료 |
| **일과 층** | 플레이어가 있는 지역의 NPC | 매 프레임 | 일과표 위치로 이동, 대화, 플레이어 행동 목격 |

- 화면 밖 지역은 전략 층 수치만 돈다. **맵이 없는 지역도 전략 층에서는 완전히 시뮬레이션된다.**
  버티컬 슬라이스에서 맵이 2개뿐이어도 월드 전체가 레이스를 벌인다.
- 전략 층이 정한 NPC의 현재 지역이 플레이어의 현재 맵 지역과 같을 때만 NPC Actor를 스폰한다.

### 4.3 틱 순서 (전략 층, 하루 1회)

순서를 바꾸면 결과가 달라지므로 고정한다.

```
DailyTick(day):
  1. 시한 플래그 만료 처리 (전설 식재료 재출현 포함)
  2. 계획 진행 (plans.ggd 정의 순서대로)          §9
  3. NPC 행동 선택·실행 (npcs.ggd 정의 순서대로)  §10
  4. 소문 전파                                     §11
  5. 판정일 검사 (day == SIM_ECLIPSE_DAY → 결말)   §14
  6. 틱 로그 기록 (Logs/world_*.jsonl)             §17
```

**RNG**: `Sim::Rng` (xorshift64*, 시드 64비트). 세이브에 상태를 저장한다.
`Rng::Chance(pct)`, `Rng::Range(lo, hi)`, `Rng::Jitter(value, pct)`만 공개한다.

### 4.4 시간 흐름 제어

| 상황 | 시계 |
|---|---|
| 튜토리얼 (`Q_DONE` 전) | 정지 |
| 대화창, 조리 화면, 메뉴 | 정지 |
| 가게에서 "잠자기" | 다음 새벽까지 건너뜀 (그 사이 틱 1회) |
| 디버그 `F6` | 하루 건너뜀 (디버그 빌드만) |

---

## 5. 월드 상태

### 5.1 전역 변수 (`var:`)

| id | 범위 | 초기값 | 의미 |
|---|---|---|---|
| `player_power` | 0~999 | 0 | 주인공의 전력. 전설 식재료를 먹을 때 오름 |
| `player_grade` | 0~5 | 0 | 요리사 등급 (0 무자격, 1 견습, 2 일성, 3 이성, 4 삼성, 5 칠성) |
| `menu_count` | 0~7 | 0 | 그랑 메뉴에 채운 칸 수 |
| `legends_eaten` | 0~ | 0 | 먹은 전설 식재료 수 |
| `tartu_size` | 1~4 | 1 | 타르투 크기 |
| `black_army` | 0~100 | 0 | 검은 식탁이 사육한 괴수 전력 |
| `fame` | 0~100 | 0 | 주인공 명성. 소문 전파 속도·첫인상 보정 |

### 5.2 지역 (`region:<id>.<field>`)

| id | 이름 | prosperity | black | unrest | security | 맵 |
|---|---|---|---|---|---|---|
| `home` | 고향 마을 | 30 | 0 | 20 | 70 | tutorial |
| `port` | 항구 도시 | 70 | 40 | 50 | 50 | port |
| `hills` | 구릉 농경지 | 60 | 20 | 40 | 60 | — |
| `alps` | 산악 지대 | 50 | 30 | 40 | 40 | — |
| `north` | 북부 숲 | 20 | 10 | 10 | 30 | — |
| `terra` | 그랑 테라 | — | 100 | — | 0 | — (일식 날만 개방) |

- `black`: 검은 식탁 영향력. 나머지는 번영, 불만, 치안. 모두 0~100 클램프.

### 5.3 전설 식재료 상태 (`legend:<id>.<field>`)

| 필드 | 의미 |
|---|---|
| `owner` | 마지막으로 잡은 쪽. 0 없음, 1 플레이어, 2 NPC(`owner_npc`로 구분), 3 검은 식탁 |
| `gone` | 1이면 지금 없음 (누군가 잡아 간 뒤 재출현 대기) |
| `available` | 계산값: `gone == 0` 이고 출현 조건(`window`)이 참 |

- 잡히면 `respawn`일 동안 `gone = 1` (시한 플래그 `<id>_gone`으로 구현).
- 잡은 쪽 판정은 `take` 효과(§15.3)로 처리한다.

### 5.4 플래그 (`flag:`)

- 이름이 곧 id. 기본값 false.
- **시한 플래그**: `flag <name> <days>`로 켜면 그 날수 뒤 틱 1단계에서 자동으로 꺼진다.

---

## 6. 세력

| id | 이름 | 장기 목표 | 초기 평판(rep) |
|---|---|---|---|
| `guild` | 미식가 협회 | 질서 유지, 칠성 결원 충원 | 0 |
| `black_table` | 검은 식탁 | 그랑 구스토 독점 | −20 |
| `villagers` | 고향 주민 | 마을 유지 | +20 |
| `fishers` | 항구 어민조합 | 바다를 검은 식탁에서 지킴 | +10 |
| `monastery` | 구릉 수도원 | 바카나 축제 수호 | 0 |
| `landlord` | 구릉 대지주 | 돈 | 0 |
| `foragers` | 산악 사냥꾼 길드 | 산의 괴수 보호 | −10 |

- 세력 필드: `rep`(플레이어 평판, −100~100), `treasury`(자금 0~100).
- 세력 간 관계 `frel:<a>.<b>` (−100~100, 대칭). 초기값은 `factions.ggd`.
- 소속 없는 NPC는 `faction = none`.

---

## 7. NPC

### 7.1 데이터 구조

```cpp
namespace Sim
{
	struct Traits          // 각 −2 ~ +2
	{
		int greed;         // 욕망: − 절제 / + 탐욕
		int principle;     // 규범: − 실리 / + 원칙
		int bold;          // 기질: − 신중 / + 대담
	};

	struct NpcDef          // 고정 (데이터 파일)
	{
		std::string id;
		std::string name;          // UTF-8
		std::string title;
		int factionIdx;            // 초기 소속 (−1 = none)
		Traits traits;
		std::string goal;          // 장기 목표 태그 (행동 점수 보정용)
		int homeRegion;
		std::vector<std::string> likes, dislikes;   // 재료 id
		std::vector<int> actions;
		std::vector<Schedule> schedule;
		std::vector<std::pair<int,int>> relInit;
		int initAffinity, initTrust, initPower, initMeter;
	};

	struct NpcState        // 가변 (세이브 대상)
	{
		int affinity;      // −100 ~ 100, 플레이어에 대한 개인 호감
		int trust;         // 0 ~ 100
		int power;         // 전력. 결말 판정과 동맹 지원에 사용
		int meter;         // 범용 수치 (발렌틴의 "인정" 등)
		int mark;          // 범용 카운터
		int factionIdx;    // 현재 소속. join 효과로 바뀜
		bool alive;
		bool captured;     // 검은 식탁에 붙잡힘
		int region;
		int lastAction;
		int lastServedDay;
		std::vector<int> rel;
	};
}
```

### 7.2 친화도와 관계 단계

```
effAffinity = clamp(affinity + faction.rep / 2, −100, 100)
```

| 유효 친화도 | 단계 | enum | 행동 |
|---|---|---|---|
| 60 이상 | 동맹 | `BAND_ALLY` | **동행하지 않는다.** 전용 의뢰, 정보·통로 제공, 자기 계획을 바꿔 플레이어를 도움(경쟁 사냥에서 물러남, 검은 식탁을 따로 막음), 일식 날 그랑 테라에 **각자** 도착해 지원(§14) |
| 20 ~ 59 | 우호 | `BAND_FRIENDLY` | 거래 10% 할인, 소문 제공 |
| −19 ~ 19 | 중립 | `BAND_NEUTRAL` | 기본 거래 |
| −59 ~ −20 | 경계 | `BAND_WARY` | 거래 거부, 플레이어 행적을 소속 세력에 보고 |
| −60 이하 | 적대 | `BAND_HOSTILE` | 방해, 조우 시 `OnHostileEncounter`. 탐욕 성향이면 **검은 식탁의 포섭 대상** |

- `OnHostileEncounter`는 전투 방식 확정 전까지 스텁(대사 + 거래 차단).
- 검은 식탁 소속 NPC는 소속 세력 평판(−20 시작) 때문에 기본적으로 경계 쪽에서 시작한다.

### 7.3 친화도·신뢰도 변화

| 원인 | 친화도 | 신뢰도 |
|---|---|---|
| 요리 대접 (§7.4) | 재료 선호에 따라 | +2 |
| 의뢰 완수 / 실패 | 의뢰 값 (기본 +10 / −5) | +10 / −10 |
| 목격한 이벤트 (§8) | 이벤트 공식 | 이벤트 값 |
| 들은 소문 (§11) | 이벤트 공식 × 감쇠 | 없음 |

### 7.4 요리 대접

```
base    = (likes 포함) ? 12 : (dislikes 포함) ? −10 : 4
legend  = 전설 식재료 요리면 ×2
quality = 조리 완성도 0.5 ~ 1.5
delta   = round(base * legend * quality)
```

- 하루 1회 제한. 대접 시 NPC별 `on_served` 스크립트가 있으면 실행.

---

## 8. 이벤트 (목격과 반응)

### 8.1 적용 공식

```
delta(npc) = event.delta + event.wGreed * greed + event.wPrinciple * principle + event.wBold * bold
```

- `faction_delta`가 있으면 해당 세력 `rep`에 1회 더한다.
- NPC별 `react.<eventId>` 스크립트가 있으면 추가 실행.

### 8.2 목격 판정

| 층 | 목격자 |
|---|---|
| 일과 층 | 반경 `WITNESS_RADIUS` = 8타일 안의 생존 NPC (씬 그래프 셀 단위 탐색) |
| 전략 층 | 같은 지역의 NPC 전원 |

### 8.3 플레이어 행동 → 이벤트

| 플레이어 행동 | 이벤트 id |
|---|---|
| 전설 식재료 포획 | `legend_catch` |
| 다른 NPC의 계획이 노리던 전설 식재료를 먼저 포획 | `legend_catch` + `stole_legend` (대상 계획 주인에게) |
| 포획 등급 5 이상 포획 | `big_catch` |
| 포획 후 감사 의례 | `gratitude` |
| 검은 식탁 소속 NPC·괴수 격퇴 | `beat_black` |
| NPC 공격 | `attack_npc` |

---

## 9. 계획 (Plan)

### 9.1 구조

```
Plan
 ├─ owner        : npc id / 세력 id / "nature"
 ├─ start_cond   : 참이 되는 첫 틱에 시작 (없으면 day 0)
 ├─ stages[]
 │    ├─ start_cond   : 이전 단계 완료 후 이 조건이 참일 때까지 대기
 │    ├─ duration     : 필요한 진행도 (0 = 연속형, complete_cond로 끝남)
 │    ├─ on_enter / daily
 │    ├─ block_cond   : 참이면 오늘 진행 안 함
 │    ├─ rush_cond    : 참이면 오늘 진행도 +2
 │    ├─ fail_cond    : 참이면 계획 전체 실패
 │    ├─ complete_cond
 │    └─ outcome[]    : "조건 => 효과", 첫 매칭만 실행
 └─ on_fail
```

상태: `PLAN_WAITING`(0) → `PLAN_ACTIVE`(1) → `PLAN_DONE`(2) / `PLAN_FAILED`(3).

### 9.2 진행 알고리즘

```
for plan in plans (정의 순서):
	if plan.state == WAITING and eval(plan.start_cond): ACTIVE, stage 0 진입
	if plan.state != ACTIVE: continue
	s = plan.stages[plan.stage]
	if s.start_cond and not s.started:
		if not eval(s.start_cond): continue
		s.started = true; run(s.on_enter)
	if eval(s.fail_cond): FAILED; run(plan.on_fail); continue
	if eval(s.block_cond): continue
	plan.progress += eval(s.rush_cond) ? 2 : 1
	run(s.daily)
	finished = (s.duration > 0) ? progress >= duration : eval(s.complete_cond)
	if finished:
		run(첫 매칭 outcome)
		다음 단계 (progress = 0) 또는 DONE
```

- `progress`는 외부 효과로 줄어들 수 있고 0 아래로 내려가지 않는다.

### 9.3 계획 목록 (기본 일정 = 아무도 개입하지 않았을 때)

| id | 주체 | 기본 일정 | 끝까지 가면 |
|---|---|---|---|
| `val_polpo` | 발렌틴 | 14일 출발 → 20일 포획 | 폴포 레 선점, 발렌틴 전력 +15 |
| `bt_polpo` | 검은 식탁 선단 | 35일 출항. 폴포 레가 없으면 재출현(76일)까지 대기 → 86일경 포획 | 세베로 전력 +15 |
| `bt_monte` | 검은 식탁 | 28일 추적 → 62일 생포 → 90일 사육 완료 | `black_army +30` |
| `hagen_hunted` | 검은 식탁 | 30일 수색 (하겐이 열흘 버팀) → 80일 하겐 생포 | 세베로가 **열쇠의 비밀**을 앎 |
| `chilsung_exam` | 협회 | 40일 칠성 결원 심사 | 조작 시 검은 식탁 추천인 당선 / 공정하면 실력순 |
| `baccana_harvest` | 대지주 코르비 | 가을 축제(28~55일) 중 36일경 포획 | 검은 식탁에 판매, 세베로 전력 +10 |
| `valentin_fall` | 발렌틴 | 심사에서 지고 인정 < 30이면 시작 | 발렌틴이 검은 식탁 합류 |
| `valentin_ally` | 발렌틴 | 인정 ≥ 70이면 시작 | 협력자. 오로라 사슴을 양보 |
| `val_aurora` | 발렌틴 | 겨울(56~83일) 60일 출발 → 66일 포획 | 오로라 사슴 선점, 전력 +25 |
| `bt_starscale` | 검은 식탁 | 열쇠를 알면 봄 88일 고향 호수 급습 → 92일 | **세베로가 열쇠를 가짐** |
| `starscale_season` | nature | 봄(84~111일) | 별비늘 출현 기간 |

**계획 간 연쇄**가 스토리를 만든다.

- 하겐이 붙잡히지 않으면 세베로는 열쇠를 모른다 → `bt_starscale`이 시작되지 않는다.
- 세라핀의 내통이 폭로되면 심사가 공정해진다 → 발렌틴이나 리노가 칠성이 된다 → 발렌틴이 무너지지 않는다.
- 발렌틴이 검은 식탁에 넘어가면 그가 잡은 전설 식재료의 전력도 검은 식탁 쪽으로 합산된다.
- 그레타가 동맹이면 산을 지켜 `bt_monte`가 막힌다 → 검은 식탁 군대가 생기지 않는다.

**아무도 개입하지 않은 월드**: 발렌틴이 폴포 레를 가져가고, 심사가 조작되고, 발렌틴이 검은 식탁에 합류하고,
하겐이 붙잡히고, 세베로가 봄에 별비늘을 가져가 일식 날 **세베로가 그랑 구스토를 먹는다.**

---

## 10. NPC 행동 선택 (전략 층)

매일 틱마다 생존하고 붙잡히지 않은 각 NPC가 자기 `actions`에서 하나를 고른다.

```
score(a) = a.base
         + a.wGreed * greed + a.wPrinciple * principle + a.wBold * bold
         + (a.goal == npc.goal ? 20 : 0)
score(a) = Rng::Jitter(score(a), 10%)
```

- `a.cond`가 거짓이면 제외. 최고 점수 < `IDLE_THRESHOLD`(5)면 `idle`. 동점은 정의 순서.
- 선택된 행동의 `effect` 실행, `event`가 있으면 같은 지역 목격자에게 적용.

| 자리표시자 | 뜻 |
|---|---|
| `@self` | 행동하는 NPC id |
| `@region` | 그 NPC의 현재 지역 |
| `@faction` | 그 NPC의 현재 소속 세력 |
| `@victim_faction` | 이벤트 한정. 피해 NPC의 세력 |

**예: 그레타** — 평소에는 사냥. `bt_monte`가 진행 중이고 플레이어와 동맹이면 `defend_mountain`
점수가 치솟아 검은 식탁의 추적 진행도를 매일 깎는다. 플레이어가 산에 없어도 일어나는 일이다.

---

## 11. 소문 전파

```cpp
struct Rumor
{
	int eventIdx;
	int subject;        // −1 = 플레이어, 그 외 NPC 인덱스
	int magnitude;      // 100 = 원래 크기
	int originDay;
	int hops;
	std::vector<uint8_t> heard;
};
```

```
for rumor in 활성 소문:
	if day - originDay > RUMOR_LIFETIME (14): 삭제
	for 들은 NPC a (정의 순서):
		for 안 들은 NPC b, rel(a,b) >= 0:
			p = 30 + rel(a,b) / 2 + fame / 5       (%)
			if b가 a와 다른 지역: p /= 2
			if Rng::Chance(p): b.heard = true; 적용 delta(b) * magnitude / 100
	magnitude /= 2 ; hops += 1
	if hops > RUMOR_MAX_HOPS (3) 또는 magnitude < 10: 삭제
```

- 전설 식재료 포획(`legend_catch`)은 `fame`을 올리고 소문으로 퍼진다. 리노가 "혼자서 폴포 레를 잡은 요리사"로 알려지는 경로다.
- 동시 활성 소문 상한 `RUMOR_MAX` = 64.

---

## 12. 사실(Fact)과 도달 경로

어떤 순서로, 어디까지 알게 될지는 정하지 않는다. 진실을 모른 채 일식 날을 맞을 수도 있다.

| source 형식 | 공개 조건 |
|---|---|
| `npc:<id> trust>=<n>` / `npc:<id> affinity>=<n>` | 살아 있고 붙잡히지 않았으며 수치 이상일 때 대화에 "묻는다" 선택지 |
| `item:<id>` | 문서 아이템 획득 즉시 |
| `cond:<조건식>` | 조건이 참이 되는 틱에 |

- 각 사실은 source 2개 이상. 부족하면 로드 시 경고 `[Sim] fact '<id>' has <n> source(s)`.
- 공개된 사실은 가게의 기록장 UI에 표시. 조건식에서 `fact:<id>`로 참조.

| id | 내용 | 경로 |
|---|---|---|
| `mother_was_chilsung` | 어머니는 칠성이었다 | 미라(신뢰 20), 마테오(신뢰 30), 오르소(신뢰 30) |
| `eight_cut` | 폴포 레의 다리별 손질법 | 마테오(신뢰 40), 엄마의 요리 노트 |
| `seraphine_traitor` | 세라핀은 검은 식탁 내통자 | 오르소(신뢰 60), 검은 식탁 지령서, 발렌틴(협력자) |
| `hagen_survivor` | 하겐은 원정의 생환자 | 마테오(신뢰 60), 그레타(신뢰 40) |
| `key_taste` | 그랑 구스토의 열쇠는 귀향의 맛 | 하겐(신뢰 70), 엄마의 마지막 편지 |
| `starscale_recipe` | 별비늘 수프 레시피 | 엄마의 요리 노트 + 마지막 편지, 하겐(신뢰 80) |
| `mother_alive` | 어머니는 그랑 테라에 살아 있다 | 하겐(신뢰 80), 검은 식탁 지령서 |
| `monte_trail` | 몬테 투베로 추적법 | 그레타(신뢰 30), 타르투 3단계 |

---

## 13. 주인공 성장 인터페이스 (그랑 메뉴·식혼·타르투)

게임 쪽 조리·사냥 시스템과 Sim의 접점이다.

```
OnPlayerCatch(legendId)
  → take <legend> player (§15.3)
  → 이벤트 legend_catch, 다른 계획의 목표였으면 stole_legend

OnDishEaten(legendId, quality)          // 조리 성공 후 시식
  → var:player_power += legend.power * quality
  → var:legends_eaten += 1
  → var:tartu_size = 1 + min(3, (legends_eaten + 1) / 2)
  → 그 칸이 비었거나 플레이어가 교체를 선택하면 메뉴 칸 채움, menu_count 갱신
  → 식혼 id(legend.soul)를 게임 쪽에 돌려줌 (기술 해금은 게임 쪽)
```

- 조리 가능 조건: `legend.cook_req`(사실 id)를 알아야 한다. 모르면 "이 재료는 아직 다룰 수 없다".
- **열쇠**: 별비늘을 잡고 `starscale_recipe`를 알고 조리하면 `flag:player_has_key`. 메뉴 칸에는 들어가지 않는다.
- 타르투 크기별 탐지 반경: 1단계 6타일, 2단계 10, 3단계 16, 4단계 24. `legend.need_tartu` 미만이면 그 식재료가 맵에 표시되지 않는다.
- 요리사 등급: `menu_count` 1 → 일성, 3 → 이성, 5 → 삼성(`player_grade` 자동 갱신). 칠성은 심사로만.

---

## 14. 결말 판정 — 미식 일식

### 14.1 그랑 테라 도착

- 절대일 105부터 가게에서 **"그랑 테라로 출발"** 선택지가 열린다. 조건: `flag:player_pass`.
- 통행증 경로(둘 이상): 칠성 심사 합격 / 마테오 동맹(옛 통행증) / 하겐 동맹이며 붙잡히지 않음.
- 출발하면 `flag:player_at_terra`, 이후 시계는 일식 날까지 건너뛴다.

### 14.2 대결 판정 (`Ending.cpp`)

일식 날(111) 틱 5단계에서 계산한다. 그랑 구스토는 **열쇠를 가진 쪽만** 다툴 수 있다.

```
playerSide = player_at_terra ? var:player_power + Σ(동맹 NPC.power / 2) : 0
             동맹 NPC = band ALLY, alive, !captured, 소속 != black_table

blackSide  = npc:severo.power + var:black_army + Σ(black_table 소속 NPC.power / 2)
             (severo 제외, alive, !captured)

playerKey  = flag:player_has_key && flag:player_at_terra
blackKey   = flag:severo_has_key && npc:severo.alive

if playerKey && blackKey : winner = playerSide >= blackSide ? player : black
elif playerKey           : winner = player
elif blackKey            : winner = black
else                     : winner = none        // 아무도 그랑 구스토를 보지 못함
```

결과를 플래그로 남긴다: `gg_player` / `gg_black` / `gg_none`.

### 14.3 판정 축

| 축 | 상태 | 판정 |
|---|---|---|
| 그랑 구스토 | 리노 / 검은 식탁 / 없음 | 14.2 |
| 그랑 메뉴 | 채운 칸 수, 구성 | `menu_count`, 칸별 식재료 |
| 엘레나 | 구출 / 흔적 발견 / 미확인 | `gg_player` → 구출, `player_at_terra` → 흔적, 그 외 미확인 |
| 발렌틴 | 협력자 / 라이벌 / 검은 식탁 / 칠성 | 플래그 |
| 일식 날의 판도 | 그랑 테라에 모인 동맹과 적 | 14.2의 집계 목록 |

### 14.4 에필로그

- `epilogue.ggd`의 각 항목은 `slot`, `cond`, `priority`, `text`. 슬롯별로 조건이 참인 항목 중 최고 priority 하나.
- 슬롯 순서: `opening` → `gran_gusto` → `mother` → `menu` → `valentin` → `world` → `closing`.
- **진엔딩 후보** (`gg_player`): 그랑 구스토에서 어머니의 수프 맛이 난다. 엘레나는 10년 전 이 맛을 알고
  집에서 흉내 냈던 것이다. 리노는 그 한 그릇을 자기 요리로 완성해 일곱 번째 칸에 적는다.
- 판정 후에도 월드는 계속 돈다 (`SIM_CONTINUE_AFTER_JUDGE`, 계획은 전부 종료).

---

## 15. 데이터 파일

### 15.1 GGD 형식

외부 라이브러리 없이 파싱하는 줄 단위 형식. 인코딩 **UTF-8 BOM**.

```
# 주석 (줄 전체)
[<type> <id>]              # 블록 시작
key = value
key += value               # 리스트에 추가
```

- 값 앞뒤 공백은 자른다. 값 안의 `#`은 주석이 아니다.
- 리스트 값은 `|` 구분: `likes = olive|lemon`. 정수 쌍: `rel = mateo:40|lucia:20`.
- 블록 id의 `.`은 하위 블록: `[stage val_polpo.hunt]`
- 빈 값은 빈 리스트/빈 문자열. 알 수 없는 키는 경고 후 무시. 필수 키 누락은 로드 실패(파일명·줄 번호).

### 15.2 조건식 문법

```
cond    := or
or      := and ( '||' and )*
and     := term ( '&&' term )*
term    := 'true' | 'false' | '!' flagref | flagref | factref | cmp
cmp     := ref op ( number | ref )
op      := '<' | '<=' | '>' | '>=' | '==' | '!='
flagref := 'flag:' name
factref := 'fact:' name
ref     := 'day' | 'season' | 'var:' id
         | 'region:' id '.' field
         | 'npc:' id '.' field      # affinity(유효) raw_affinity trust power meter mark alive captured band black(소속이 black_table이면 1)
         | 'faction:' id '.' field  # rep treasury
         | 'frel:' id '.' id | 'rel:' id '.' id
         | 'plan:' id '.' field     # stage progress state
         | 'legend:' id '.' field   # owner gone available
```

- 괄호 없음. `&&`가 `||`보다 먼저 묶인다. 빈 조건은 참.
- `npc:<id>.band`는 −2(적대) ~ 2(동맹).
- 로드 시 **AST로 미리 컴파일**. 알 수 없는 id는 로드 에러.

### 15.3 효과 문법 (`;` 구분)

| 명령 | 예 |
|---|---|
| `add <ref> <n>` | `add npc:severo.power 15` |
| `set <ref> <n>` | `set var:black_army 0` |
| `flag <name> [days]` / `unflag <name>` | `flag hagen_protected` |
| `take <legend> <taker>` | `take polpo_re valentin` — taker: `player` / NPC id / `black_table` |
| `kill <npc>` / `capture <npc>` / `free <npc>` | `capture hagen` |
| `join <npc> <faction>` | `join valentin black_table` |
| `move <npc> <region>` | `move hagen terra` |
| `plan <id> start\|fail` | `plan bt_starscale start` |
| `event <eventId> [subject]` | |
| `reveal <factId>` | |
| `chance <pct> { <효과들> }` | `chance 50 { kill hagen }` |
| `notify <noticeId>` | 플레이어가 그 지역에 있거나 소문을 들었을 때 알림 |
| `log <text>` | 월드 로그에만 |

**`take` 처리**

```
legend.owner = taker 종류, owner_npc = taker가 NPC면 그 인덱스
flag <legend>_gone <legend.respawn>
taker == player      : 전력 증가 없음 (먹을 때 OnDishEaten에서)
taker == NPC         : npc.power += legend.power
taker == black_table : npc:severo.power += legend.power
```

쓰기 가능 ref: `var`, `region`, `npc`(affinity trust power meter mark), `faction`(rep treasury), `frel`, `rel`, `plan`(progress).

### 15.4 파일 목록

```
Data/Sim/
 ├─ world.ggd      전역 변수, 지역, 알림 문구
 ├─ factions.ggd   세력, 세력 관계
 ├─ legends.ggd    전설 식재료
 ├─ npcs.ggd       NPC 정의, 일과, 대사, 선택지
 ├─ actions.ggd    NPC 행동
 ├─ events.ggd     이벤트
 ├─ plans.ggd      계획과 단계
 ├─ facts.ggd      사실과 경로
 ├─ items.ggd      문서 아이템
 └─ epilogue.ggd   에필로그 조각
```

아래는 각 파일의 **초기 데이터 전문**이다. Claude Code는 이 내용을 그대로 파일로 만든다.

### 15.5 `world.ggd`

```
# ===== 전역 =====
[var player_power]
init = 0
max = 999

[var player_grade]
init = 0
max = 5

[var menu_count]
init = 0
max = 7

[var legends_eaten]
init = 0

[var tartu_size]
init = 1
min = 1
max = 4

[var black_army]
init = 0

[var fame]
init = 0

# ===== 지역 =====
[region home]
name = 고향 마을
prosperity = 30
black = 0
unrest = 20
security = 70
map = tutorial

[region port]
name = 항구 도시
prosperity = 70
black = 40
unrest = 50
security = 50
map = port

[region hills]
name = 구릉 농경지
prosperity = 60
black = 20
unrest = 40
security = 60

[region alps]
name = 산악 지대
prosperity = 50
black = 30
unrest = 40
security = 40

[region north]
name = 북부 숲
prosperity = 20
black = 10
unrest = 10
security = 30

[region terra]
name = 그랑 테라
black = 100
security = 0
closed = true

# ===== 알림 문구 =====
[notice val_polpo]
text = 발렌틴이 폴포 레를 잡았다는 소문이 항구를 휩쓸었다.

[notice bt_polpo]
text = 검은 돛의 선단이 거대한 무언가를 끌고 항구를 떠났다.

[notice bt_monte]
text = 산에서 몬테 투베로의 울음이 끊겼다고 한다.

[notice bt_army]
text = 산악 너머에서 쇠사슬 소리가 들린다. 무언가를 기르고 있다.

[notice hagen_captured]
text = 북쪽 숲의 오두막이 비어 있다고 한다.

[notice exam_black]
text = 칠성 결원 심사 결과가 나왔다. 아무도 모르는 이름이다.

[notice exam_valentin]
text = 발렌틴이 역대 최연소 칠성이 되었다.

[notice exam_player]
text = 새 칠성의 이름이 발표되었다. 리노.

[notice valentin_fall]
text = 발렌틴이 협회를 떠났다. 검은 옷을 입은 그를 봤다는 사람이 있다.

[notice baccana]
text = 올해 축제에서 바카나가 사라졌다. 대지주의 짐마차가 북쪽으로 갔다.

[notice val_aurora]
text = 발렌틴이 오로라 사슴을 잡았다. 북쪽 하늘이 하룻밤 꺼졌다고 한다.

[notice bt_starscale]
text = 고향 호수에 검은 배가 떴다.

[notice eclipse_soon]
text = 해가 이상하게 붉다. 일식이 가깝다.
```

### 15.6 `factions.ggd`

```
[faction guild]
name = 미식가 협회
rep = 0

[faction black_table]
name = 검은 식탁
rep = -20
treasury = 70

[faction villagers]
name = 고향 주민
rep = 20

[faction fishers]
name = 항구 어민조합
rep = 10
treasury = 20

[faction monastery]
name = 구릉 수도원
rep = 0

[faction landlord]
name = 구릉 대지주
rep = 0
treasury = 40

[faction foragers]
name = 산악 사냥꾼 길드
rep = -10
treasury = 30

# 세력 관계 (대칭, 미기재 0)
[frel init]
pair += guild.black_table:-60
pair += black_table.fishers:-50
pair += black_table.foragers:-40
pair += black_table.landlord:40
pair += monastery.landlord:-40
pair += guild.fishers:20
```

### 15.7 `legends.ggd`

```
[legend starscale]
name = 별비늘
region = home
grade = 2
slot = key
power = 5
respawn = 999
window = season == 3
cook_req = starscale_recipe
need_tartu = 1
soul = homecoming

[legend limone_drago]
name = 리모네 드라고
region = port
grade = 4
slot = antipasto
power = 5
respawn = 28
window = true
cook_req =
need_tartu = 1
soul = citrus_spark

[legend polpo_re]
name = 폴포 레
region = port
grade = 7
slot = secondo_fish
power = 15
respawn = 56
window = true
cook_req = eight_cut
need_tartu = 1
soul = ink_smoke

[legend baccana]
name = 바카나
region = hills
grade = 7
slot = bevanda
power = 10
respawn = 999
window = season == 1
cook_req =
need_tartu = 2
soul = drunken_step

[legend ferro_toro]
name = 페로 토로
region = alps
grade = 6
slot = secondo_meat
power = 10
respawn = 28
window = true
cook_req =
need_tartu = 1
soul = iron_charge

[legend monte_tubero]
name = 몬테 투베로
region = alps
grade = 8
slot = primo
power = 20
respawn = 999
window = true
cook_req = monte_trail
need_tartu = 3
soul = mountain_rush

[legend aurora_deer]
name = 오로라 사슴
region = north
grade = 9
slot = dolce
power = 25
respawn = 999
window = season == 2
cook_req =
need_tartu = 2
soul = night_sight
```

- `respawn = 999`는 이번 게임 안에 다시 나오지 않는다는 뜻.
- 확장 시 칸마다 후보를 추가한다 (예: 다른 지역의 `antipasto` 후보).

### 15.8 `npcs.ggd`

일과 `schedule`은 구간(새벽|낮|저녁|밤)별 **맵 위치 id**. 맵이 없는 지역 NPC는 일과가 없다.
대사 키: `greet.<ally|friendly|neutral|wary|hostile>`, `ask.<factId>`. 없는 단계는 `greet.neutral`로 대체.
선택지: `choice.<id>.text / .cond / .effect`. `traits`는 욕망·규범·기질 순서.
미기재 기본값: `init_affinity 0`, `init_trust 0`, `power 0`, `meter 0`, `actions idle`.

```
# ===== 고향 =====
[npc mira]
name = 미라
title = 방앗간 주인
faction = villagers
region = home
traits = -1 1 -1
goal = keep_promise
likes = grain_bread
actions = idle|tend_village
init_affinity = 30
init_trust = 30
power = 5
schedule = mill|square|mill|mill_house
greet.ally = 네 엄마도 떠나기 전날 딱 그 얼굴이었어.
greet.neutral = 방아 돌리는 소리 시끄럽지? 금방 끝나.
ask.mother_was_chilsung = 네 엄마? 칠성이었어.\n마을 사람들한테는 그냥 식당 아줌마였지만.
choice.seal_lake.text = 호수를 지켜 주세요. 봄에 검은 배가 올 거예요.
choice.seal_lake.cond = npc:mira.band >= 2 && faction:villagers.rep >= 40 && season == 3
choice.seal_lake.effect = flag home_sealed

[npc bruno]
name = 브루노
title = 사냥꾼
faction = villagers
region = home
traits = 0 0 1
goal = hunt
likes = lake_deer
actions = idle|hunt
init_affinity = 10
power = 15
schedule = house_2|forest_edge|square|house_2

[npc tomas]
name = 토마스
title = 어부
faction = villagers
region = home
traits = -1 1 0
goal = protect_lake
likes = lake_fish
actions = idle|guard_lake
init_affinity = 10
power = 10
schedule = house_5|lakeside|lakeside|house_5

# (튜토리얼의 다른 주민도 같은 형식으로 추가한다)

# ===== 항구 =====
[npc mateo]
name = 마테오
title = 생선 장수 (은퇴한 칠성)
faction = none
region = port
traits = -1 1 -2
goal = quiet_life
likes = olive_pickle
actions = idle|sell
power = 70
rel = orso:30|lucia:40
schedule = fish_stall|fish_stall|fish_stall|mateo_house
greet.neutral = 생선 살 거 아니면 비켜.
greet.ally = …엘레나 손놀림이 보이는구나.
ask.eight_cut = 폴포 레는 다리마다 다른 짐승이다.\n여덟 번 칼을 바꿔 잡아라.
choice.old_pass.text = 그랑 테라에 데려가 주세요.
choice.old_pass.cond = npc:mateo.band >= 2 && day >= 90
choice.old_pass.effect = flag player_pass

[npc orso]
name = 오르소
title = 협회 심사관
faction = guild
region = port
traits = -1 2 -1
goal = find_traitor
likes = aged_cheese
actions = idle|investigate
init_trust = 10
power = 30
rel = seraphine:-30|mateo:30
schedule = guild_office|guild_office|tavern|inn
greet.neutral = 견습이군. 그 등급으로 할 수 있는 일은 많지 않아.
choice.expose.text = 세라핀이 검은 식탁과 내통하고 있습니다.
choice.expose.cond = fact:seraphine_traitor && npc:orso.trust >= 40 && plan:chilsung_exam.state <= 1
choice.expose.effect = flag seraphine_exposed; capture seraphine
choice.apply_exam.text = 칠성 결원 심사에 지원하겠습니다.
choice.apply_exam.cond = var:player_grade >= 2 && plan:chilsung_exam.state <= 1
choice.apply_exam.effect = flag player_applied

[npc seraphine]
name = 세라핀
title = 협회 부회장
faction = guild
region = port
traits = 1 -2 -1
goal = rig_exam
likes = rare_fish
actions = idle|rig_exam|recruit
power = 25
rel = orso:-30|valentin:20
schedule = guild_office|guild_office|guild_office|mansion

[npc valentin]
name = 발렌틴
title = 이성 요리사
faction = guild
region = port
traits = 1 0 2
goal = chilsung
likes = polpo_re|aurora_deer
actions = idle|train|show_off
init_affinity = -10
power = 40
meter = 10
rel = seraphine:20
schedule = guild_kitchen|harbor|tavern|guild_kitchen
on_served = add npc:valentin.meter 8
react.legend_catch = add npc:valentin.meter 10
react.stole_legend = add npc:valentin.meter 5
greet.hostile = 짝도 없는 촌뜨기가 여기까지 오다니.
greet.neutral = 또 너냐. 이번 사냥감도 내 거다.
greet.friendly = …혼자서 그걸 잡았다고? 거짓말은 아니겠지.
greet.ally = 먼저 가라. 오로라 사슴은 네 몫이다.

[npc lucia]
name = 루치아
title = 어민조합장
faction = fishers
region = port
traits = 0 2 2
goal = guard_sea
likes = limone_drago
actions = idle|patrol_sea
init_affinity = 10
power = 25
rel = mateo:40
schedule = pier|pier|union_hall|union_hall

[npc donato]
name = 도나토
title = 검은 식탁 선단장
faction = black_table
region = port
traits = 2 -1 1
goal = fleet
actions = idle|smuggle
power = 30
schedule = harbor|harbor|tavern|black_ship

# ===== 맵 없는 지역 (전략 층 전용) =====
[npc anselme]
name = 앙셀므
title = 수도원 양조 수도사
faction = monastery
region = hills
traits = -2 1 -1
goal = guard_festival
actions = idle|guard_festival
power = 20

[npc corvi]
name = 코르비
title = 대지주
faction = landlord
region = hills
traits = 2 -1 0
goal = profit
actions = idle|hire_hunters
power = 10

[npc greta]
name = 그레타
title = 산악 사냥꾼 길드장
faction = foragers
region = alps
traits = 0 1 2
goal = guard_mountain
likes = ferro_toro
actions = idle|hunt|defend_mountain
init_affinity = -10
power = 45

[npc hagen]
name = 하겐
title = 칠성, 북부 숲 은둔자
faction = none
region = north
traits = -2 2 -2
goal = wait_heir
likes = aurora_deer|smoked_berry
actions = idle|hide
power = 60
choice.pass.text = 저를 그랑 테라로 데려가 주세요.
choice.pass.cond = npc:hagen.band >= 2 && npc:hagen.captured == 0
choice.pass.effect = flag player_pass

[npc severo]
name = 세베로
title = 검은 식탁 수장
faction = black_table
region = terra
traits = 2 -2 1
goal = gran_gusto
actions = idle|recruit
power = 60
```

### 15.9 `actions.ggd`

```
[action idle]
base = 5

[action hunt]
base = 8
wBold = 2

[action sell]
base = 6

[action tend_village]
base = 6
effect = add region:home.unrest -1

[action guard_lake]
base = 6
wPrinciple = 2
# 주민 평판이 쌓였을 때만 호수를 지킨다 (플레이어가 마을을 움직인 결과)
cond = plan:bt_starscale.state == 1 && faction:villagers.rep >= 40
effect = add plan:bt_starscale.progress -1

[action investigate]
base = 6
wPrinciple = 3
goal = find_traitor
cond = !flag:seraphine_exposed
effect = add npc:@self.mark 1

[action rig_exam]
base = 8
wGreed = 2
wPrinciple = -3
goal = rig_exam
cond = plan:chilsung_exam.state == 1 && !flag:seraphine_exposed
effect = flag exam_rigged 3

[action recruit]
base = 6
wGreed = 2
cond = faction:black_table.treasury >= 5
# 탐욕 +1 이상이고 플레이어와 적대인 NPC 하나를 포섭 (Sim 내장 효과)
effect = recruit_hostile; add faction:black_table.treasury -1

[action train]
base = 8
goal = chilsung
effect = log valentin_train

[action show_off]
base = 5
wBold = 2
cond = npc:@self.band <= 0
effect = add npc:@self.meter -1

[action patrol_sea]
base = 6
wBold = 2
# 플레이어와 동맹일 때만 선단을 막으러 나간다
cond = plan:bt_polpo.state == 1 && npc:@self.band >= 2
effect = add plan:bt_polpo.progress -1

[action smuggle]
base = 7
wGreed = 3
effect = add faction:black_table.treasury 1

[action guard_festival]
base = 6
cond = plan:baccana_harvest.state == 1 && npc:@self.band >= 1
effect = add plan:baccana_harvest.progress -1

[action hire_hunters]
base = 8
wGreed = 3
cond = plan:baccana_harvest.state == 1 && faction:landlord.treasury >= 5
effect = add faction:landlord.treasury -1; add plan:baccana_harvest.progress 1

[action defend_mountain]
base = 10
wBold = 2
goal = guard_mountain
cond = plan:bt_monte.state == 1 && npc:@self.band >= 2
effect = add plan:bt_monte.progress -2

[action hide]
base = 7
goal = wait_heir
# 혼자서는 열흘까지만 버틴다
cond = plan:hagen_hunted.state == 1 && npc:@self.mark < 10
effect = add plan:hagen_hunted.progress -1; add npc:@self.mark 1
```

- `recruit_hostile`은 효과 문법의 내장 명령으로 추가한다: 소속이 `black_table`이 아니고, 살아 있고,
  `band <= -2`이며 `greed >= 1`인 NPC 중 정의 순서 첫 번째를 `join <npc> black_table`.

### 15.10 `events.ggd`

```
[event legend_catch]
delta = 4
wBold = 3
trust = 2
fame = 5
faction_delta = guild:3

[event stole_legend]
delta = -10
wBold = 4

[event big_catch]
delta = 3
wBold = 3

[event gratitude]
delta = 2
wPrinciple = 2
trust = 2

[event beat_black]
delta = 6
wPrinciple = 2
fame = 3
faction_delta = black_table:-10|fishers:5|foragers:5|villagers:5

[event attack_npc]
delta = -40
trust = -40
faction_delta = @victim_faction:-20

[event reported]
delta = -5
wPrinciple = -2
```

- `fame` 키: 이벤트 발생 시 `var:fame`에 1회 더한다.
- `stole_legend`는 목격자가 아니라 **빼앗긴 계획의 주인**에게 직접 적용하고, 그 주인에서 소문이 시작된다.

### 15.11 `plans.ggd`

```
# ===== 발렌틴: 폴포 레 =====
[plan val_polpo]
owner = valentin
start_cond = day >= 14
stages = hunt

[stage val_polpo.hunt]
duration = 7
fail_cond = legend:polpo_re.gone == 1 || npc:valentin.alive == 0 || flag:valentin_ally
outcome = true => take polpo_re valentin; notify val_polpo

# ===== 검은 식탁 선단: 폴포 레 =====
[plan bt_polpo]
owner = black_table
start_cond = day >= 35
stages = sail

[stage bt_polpo.sail]
duration = 10
block_cond = legend:polpo_re.gone == 1
fail_cond = npc:donato.alive == 0 || npc:donato.captured == 1 || day >= 105
outcome = true => take polpo_re black_table; notify bt_polpo

# ===== 검은 식탁: 몬테 투베로 생포·사육 =====
[plan bt_monte]
owner = black_table
start_cond = day >= 28
stages = track|capture|breed

[stage bt_monte.track]
duration = 28
fail_cond = legend:monte_tubero.owner == 1

[stage bt_monte.capture]
duration = 7
fail_cond = legend:monte_tubero.owner == 1
outcome = true => take monte_tubero black_table; notify bt_monte

[stage bt_monte.breed]
duration = 28
fail_cond = npc:severo.alive == 0
outcome = true => add var:black_army 30; notify bt_army

# ===== 검은 식탁: 하겐 추적 =====
[plan hagen_hunted]
owner = black_table
start_cond = day >= 30
stages = search|capture

[stage hagen_hunted.search]
duration = 35
fail_cond = flag:hagen_protected

[stage hagen_hunted.capture]
duration = 5
fail_cond = flag:hagen_protected
outcome = true => capture hagen; move hagen terra; flag severo_knows_key; notify hagen_captured

# ===== 칠성 결원 심사 =====
[plan chilsung_exam]
owner = guild
stages = exam

[stage chilsung_exam.exam]
duration = 41
outcome = flag:exam_rigged && !flag:seraphine_exposed => flag black_chilsung; add npc:severo.power 10; notify exam_black
outcome = flag:player_applied && var:player_power >= npc:valentin.power => flag player_chilsung; flag player_pass; set var:player_grade 5; notify exam_player
outcome = true => flag valentin_chilsung; add npc:valentin.power 10; notify exam_valentin

# ===== 대지주: 바카나 =====
[plan baccana_harvest]
owner = corvi
start_cond = season == 1
stages = hunt

[stage baccana_harvest.hunt]
duration = 17
fail_cond = legend:baccana.gone == 1 || npc:corvi.alive == 0 || season != 1
outcome = true => take baccana black_table; notify baccana

# ===== 발렌틴의 몰락 / 인정 =====
[plan valentin_fall]
owner = valentin
start_cond = npc:valentin.meter < 30 && plan:chilsung_exam.state == 2 && !flag:valentin_chilsung && !flag:valentin_ally
stages = fall

[stage valentin_fall.fall]
duration = 1
outcome = true => join valentin black_table; notify valentin_fall

[plan valentin_ally]
owner = valentin
start_cond = npc:valentin.meter >= 70 && npc:valentin.black == 0
stages = accept

[stage valentin_ally.accept]
duration = 1
outcome = true => flag valentin_ally; reveal seraphine_traitor

# ===== 발렌틴: 오로라 사슴 =====
[plan val_aurora]
owner = valentin
start_cond = day >= 60
stages = hunt

[stage val_aurora.hunt]
duration = 7
fail_cond = legend:aurora_deer.gone == 1 || npc:valentin.alive == 0 || flag:valentin_ally || season != 2
outcome = true => take aurora_deer valentin; notify val_aurora

# ===== 검은 식탁: 별비늘 (열쇠) =====
[plan bt_starscale]
owner = black_table
start_cond = flag:severo_knows_key && day >= 88
stages = raid

[stage bt_starscale.raid]
duration = 5
block_cond = flag:starscale_hidden
fail_cond = flag:home_sealed || legend:starscale.owner == 1
outcome = true => take starscale black_table; flag severo_has_key; notify bt_starscale

# ===== 자연: 일식 예고 =====
[plan eclipse_omen]
owner = nature
start_cond = day >= 100
stages = omen

[stage eclipse_omen.omen]
duration = 1
outcome = true => notify eclipse_soon
```

- `hagen_protected`: 하겐 동맹 + 북부 숲 의뢰 "오두막 지키기" 완수, 또는 그레타 동맹 선택지.
- `starscale_hidden`: 토마스 동맹 선택지로 3일간 켬.
- `home_sealed`: 미라 선택지(`seal_lake`).

### 15.12 `facts.ggd`

```
[fact mother_was_chilsung]
title = 어머니의 과거
text = 어머니는 칠성이었다.
source += npc:mira trust>=20
source += npc:mateo trust>=30
source += npc:orso trust>=30

[fact eight_cut]
title = 여덟 번의 칼
text = 폴포 레는 다리마다 손질법이 다르다.
source += npc:mateo trust>=40
source += item:mothers_notes

[fact seraphine_traitor]
title = 협회의 배신자
text = 세라핀 부회장은 검은 식탁과 내통하고 있다.
source += npc:orso trust>=60
source += item:black_orders
source += cond:flag:valentin_ally

[fact hagen_survivor]
title = 유일한 생환자
text = 북쪽 숲의 하겐은 어머니와 함께 그랑 테라에 갔다가 혼자 돌아왔다.
source += npc:mateo trust>=60
source += npc:greta trust>=40

[fact key_taste]
title = 귀향의 맛
text = 그랑 구스토는 귀향의 맛을 가져온 자에게만 모습을 보인다.
source += npc:hagen trust>=70
source += item:mothers_letter

[fact starscale_recipe]
title = 별비늘 수프
text = 떠나기 전날 밤의 그 수프. 고향 호수의 별비늘로 끓인다.
source += npc:hagen trust>=80
source += cond:fact:key_taste && flag:has_mothers_notes

[fact mother_alive]
title = 어머니는 살아 있다
text = 어머니는 그랑 테라에서 검은 식탁에 붙잡혀 있다.
source += npc:hagen trust>=80
source += item:black_orders

[fact monte_trail]
title = 산의 발자국
text = 몬테 투베로는 송로 냄새로만 쫓을 수 있다.
source += npc:greta trust>=30
source += cond:var:tartu_size >= 3
```

### 15.13 `items.ggd`

```
[item mothers_notes]
name = 엄마의 요리 노트
where = home.restaurant
cond = true
on_get = flag has_mothers_notes

[item mothers_letter]
name = 엄마의 마지막 편지
where = give:mira
cond = npc:mira.trust >= 70

[item black_orders]
name = 검은 식탁 지령서
where = drop:donato
cond = npc:donato.captured == 1 || npc:donato.alive == 0
```

`where`: `<region>.<장소>`(줍기), `give:<npc>`(대화로 받음), `drop:<npc>`(격퇴·체포 시).
도나토를 격퇴하면 게임 쪽이 `capture donato`에 해당하는 `OnPlayerChoice("beat_donato")`를 부른다.

### 15.14 `epilogue.ggd`

```
[epi gg_player]
slot = gran_gusto
priority = 30
cond = flag:gg_player
text = 일식의 그림자 아래, 리노는 세계의 모든 맛을 한 숟갈에 담았다.

[epi gg_black]
slot = gran_gusto
priority = 30
cond = flag:gg_black
text = 그랑 구스토를 삼킨 세베로의 웃음이 대륙 끝까지 울렸다.

[epi gg_none]
slot = gran_gusto
priority = 30
cond = flag:gg_none
text = 해가 다시 떴을 때, 그랑 테라에는 아무것도 남아 있지 않았다. 다음 일식은 30년 뒤다.

[epi mother_rescued]
slot = mother
priority = 20
cond = flag:gg_player
text = "…많이 컸네." 10년 만에 들은 목소리였다.

[epi mother_trace]
slot = mother
priority = 10
cond = flag:player_at_terra
text = 무너진 원정 캠프에서 리노는 엄마의 칼을 찾았다. 아직 날이 서 있었다.

[epi mother_unknown]
slot = mother
priority = 1
cond = true
text = 엄마가 어디에 있는지, 리노는 아직 모른다.

[epi menu_full]
slot = menu
priority = 20
cond = var:menu_count >= 7
text = 가게 메뉴판에 일곱 번째 줄이 적혔다. 그 요리의 이름은 리노만 안다.

[epi menu_some]
slot = menu
priority = 10
cond = var:menu_count >= 3
text = 메뉴판에는 아직 빈칸이 남아 있다. 리노는 그게 싫지 않다.

[epi val_ally]
slot = valentin
priority = 20
cond = flag:valentin_ally
text = 발렌틴은 끝까지 짝이 되지 않았다. 대신 같은 날 같은 산을 올랐다.

[epi val_black]
slot = valentin
priority = 20
cond = npc:valentin.black == 1
text = 검은 식탁의 칼잡이가 된 발렌틴. 그의 요리에서는 더 이상 아무 맛도 나지 않는다.

[epi val_rival]
slot = valentin
priority = 5
cond = npc:valentin.alive == 1
text = "다음 일식 땐 내가 먼저다." 발렌틴은 그렇게 말하고 떠났다.

[epi closing_player]
slot = closing
priority = 20
cond = flag:gg_player
text = 그랑 구스토에서는 떠나기 전날 밤의 그 수프 맛이 났다.

[epi closing_other]
slot = closing
priority = 1
cond = true
text = 리노는 식당 문을 열었다. 오늘도 누군가는 배가 고프다.
```

---

## 16. 코드 구조

### 16.1 파일

| 파일 | 역할 | 인코딩 |
|---|---|---|
| `Sim/SimTypes.h` | 공통 구조체, enum, 상수 | ASCII |
| `Sim/GgdParser.h/cpp` | GGD 토크나이저·블록 파서 | ASCII |
| `Sim/Expr.h/cpp` | 조건식·효과 AST 컴파일과 평가 | ASCII |
| `Sim/SimData.h/cpp` | 데이터 로드, id → 인덱스 해석, 검증 | ASCII |
| `Sim/World.h/cpp` | 월드 상태, 시계, `DailyTick`, 공개 API | ASCII |
| `Sim/Plans.cpp` | 계획 진행 | ASCII |
| `Sim/NpcBrain.cpp` | 행동 선택 | ASCII |
| `Sim/Rumors.cpp` | 소문 전파 | ASCII |
| `Sim/Legends.cpp` | 전설 식재료 상태, `take`, 그랑 메뉴, 타르투 | ASCII |
| `Sim/Ending.cpp` | 일식 판정, 에필로그 조합 | ASCII |
| `Sim/SimSave.cpp` | 세이브/로드 | ASCII |
| `Sim/SimLog.cpp` | `Logs/world_*.jsonl` | ASCII |
| `SimDebug.cpp` | 디버그 오버레이 (Renderer 사용) | BOM |
| `Data/Sim/*.ggd` | 콘텐츠 | **UTF-8 BOM** |

- 문자열은 내부적으로 UTF-8 `std::string`. **구현 전에 `KoreanFont.h`의 입력 타입을 확인하고**,
  `wchar_t`가 필요하면 `Platform.cpp`에 `Utf8ToWide`를 추가한다.
- `.vcxproj`에 새 파일 등록. `Data/Sim`은 기존 `Shaders`와 같은 방식으로 출력 폴더에 복사.

### 16.2 공개 API (`namespace Sim`)

```cpp
class World
{
public:
	bool Load(const char* dataDir, uint64_t seed);
	void Update(float dt);
	void SetPaused(bool paused);
	void SkipToNextDawn();
	void DebugAdvanceDays(int days);       // 디버그 빌드만

	// ---- 읽기 ----
	int GetDay() const;
	int GetSegment() const;
	Season GetSeason() const;
	int GetNpcCount() const;
	const NpcDef& GetNpcDef(int idx) const;
	const NpcState& GetNpcState(int idx) const;
	int GetEffAffinity(int npcIdx) const;
	Band GetBand(int npcIdx) const;
	const char* GetGreeting(int npcIdx) const;
	std::vector<int> GetAvailableChoices(int npcIdx) const;
	std::vector<int> GetAskableFacts(int npcIdx) const;
	bool IsFactKnown(int factIdx) const;
	bool IsLegendAvailable(int legendIdx) const;
	const MenuState& GetMenu() const;               // 칸별 식재료
	int GetVar(const char* id) const;
	bool GetFlag(const char* id) const;
	const std::vector<Notice>& GetNotices() const;

	// ---- 플레이어 행동 보고 ----
	void OnPlayerEvent(const char* eventId, float wx, float wy, int victimNpc = -1);
	void OnDishServed(int npcIdx, const std::vector<std::string>& ingredients,
		float quality);
	void OnPlayerCatch(const char* legendId);
	const char* OnDishEaten(const char* legendId, float quality);    // 반환: 식혼 id
	void OnQuestResult(const char* questId, bool success);
	void OnAskFact(int npcIdx, int factIdx);
	void OnItemAcquired(const char* itemId);
	void OnPlayerChoice(int npcIdx, const char* choiceId);
	void OnPlayerChoice(const char* globalChoiceId);   // beat_donato, depart_terra 등

	// ---- 판정 ----
	bool IsJudged() const;
	std::vector<std::string> BuildEpilogue() const;

	bool Save(const char* path) const;
	bool LoadSave(const char* path);
};
```

- `g_Sim`은 `SimpleGame.cpp`가 소유하고 씬 전환 후에도 유지한다.
- 목격자 판정은 게임 쪽이 등록한 **NPC 위치 제공 콜백**(`SetNpcLocator`)으로 한다. Sim이 Actor를 모르게 하기 위해서다.
- 전역 선택지(`depart_terra`, `beat_donato`)는 `world.ggd`에 `[choice <id>]` 블록(text/cond/effect)으로 둔다:

```
[choice depart_terra]
text = 그랑 테라로 출발한다.
cond = flag:player_pass && day >= 105 && !flag:player_at_terra
effect = flag player_at_terra; move_player terra

[choice beat_donato]
cond = npc:donato.alive == 1 && npc:donato.captured == 0
effect = capture donato; event beat_black
```

(`move_player`는 효과 내장 명령. 게임 쪽에 지역 이동 요청을 보낸다.)

### 16.3 화면 쪽 연결

| 항목 | 위치 | 내용 |
|---|---|---|
| NPC Actor | `NpcActor` (신규, Actor 파생) | `npcIdx`만 가짐. 대사·단계는 매번 Sim에서 읽음 |
| 스폰/디스폰 | 씬 로드 시, 새벽 틱 후 | NPC 현재 지역 == 현재 맵 지역이고 살아 있고 붙잡히지 않은 NPC만 |
| 관계 표시 | NPC 머리 위 `LAYER_OVERLAY` | 단계 아이콘 5종 |
| 전설 식재료 | `LegendActor` (신규) | `IsLegendAvailable`이 참이고 타르투 크기 조건을 만족할 때만 스폰 |
| 타르투 | 기존 동행 Actor | `var:tartu_size`로 모델 스케일·탐지 반경 |
| 알림 | HUD `DrawUI()` | `GetNotices()` 우측 상단 4초 |
| 메뉴판·기록장 | 가게 UI | 그랑 메뉴 7칸, 알아낸 사실, 들은 소문 |
| 튜토리얼 | `Game.cpp` | `Q_DONE`에서 `g_Sim.SetPaused(false)` |

---

## 17. 로그와 디버그

### 17.1 월드 로그 `Logs/world_<시각>.jsonl` (스키마 `gg-world/1`)

```json
{"t":"session_start","schema":"gg-world/1","seed":1234,"data_hash":"..."}
{"t":"tick","day":21,"season":0,"vars":{"player_power":0,"black_army":0}}
{"t":"action","day":21,"npc":"greta","action":"hunt","score":14}
{"t":"plan","day":21,"plan":"val_polpo","stage":"hunt","progress":7,"state":"done"}
{"t":"take","day":21,"legend":"polpo_re","taker":"valentin"}
{"t":"join","day":42,"npc":"valentin","faction":"black_table"}
{"t":"judge","day":111,"winner":"black","player_side":0,"black_side":190}
```

같은 시드·같은 입력이면 파일이 바이트 단위로 같아야 한다.

### 17.2 디버그 오버레이 (`F5`, 디버그 빌드만)

- 날짜·계절·일식까지 남은 날, 전역 변수
- 전설 식재료 표: 소유자, gone, available
- 활성 계획: 이름, 단계, 진행도 막대, block 여부
- 현재 지역 NPC: 유효 친화도, 신뢰도, 전력, 소속, 오늘 행동
- 일식 판정 미리보기: 현재 상태로 판정하면 양측 전력과 승자
- `F6` 하루 진행, `F7` 상태 덤프

### 17.3 헤드리스 테스트

```
SimpleGame.exe --sim-test <days> [seed]
```

창 없이 Sim만 돌리고 결말 축과 주요 플래그를 출력한다. **아무도 개입하지 않은 월드의 예상 결과**:

| 일 (전후) | 사건 |
|---|---|
| 20 | 발렌틴 폴포 레 포획 (발렌틴 전력 55) |
| 36 | 코르비가 바카나 포획 → 검은 식탁 (세베로 70) |
| 40 | 심사 조작 → 검은 식탁 추천인 칠성 (세베로 80) |
| 41 | 발렌틴 검은 식탁 합류 |
| 62 | 몬테 투베로 생포 (세베로 100) |
| 66 | 발렌틴 오로라 사슴 포획 (발렌틴 80) |
| 80 | 하겐 생포 (열흘 버틴 뒤) → 세베로가 열쇠를 앎 |
| 86 | 폴포 레 재출현(76일) 후 검은 식탁 선단 포획 (세베로 115) |
| 90 | 사육 완료 (`black_army` 30) |
| 92 | 별비늘 급습 → 세베로가 열쇠를 가짐 (세베로 120) |
| 111 | 일식: `gg_black`. 검은 식탁 측 전력 약 205 (세베로 120 + 군대 30 + 발렌틴 40 + 도나토 15) |

다르게 나오면 버그이거나 수치 문제다. 월드 로그로 원인을 추적한다.

### 17.4 성능 계측 (`PERF_LOG.md`에 추가)

| 지표 | 종류 | 뜻 |
|---|---|---|
| `sim.tick` | scope | 하루 틱 전체 |
| `sim.plans` / `sim.brain` / `sim.rumors` | scope | 틱 단계별 |
| `sim.actions` | count | 그 틱에 실행된 NPC 행동 수 |
| `sim.rumor_active` | count | 활성 소문 수 |
| `sim.rumor_checks` | count | 전파 판정 횟수 (NPC² 규모, 감시용) |

---

## 18. 구현 단계와 완료 판정

| 단계 | 내용 | 완료 판정 |
|---|---|---|
| **1. 파서와 데이터** | `GgdParser`, `Expr`, `SimData`. 모든 `.ggd` 로드와 검증 | 콘솔 `[Sim] loaded: N npcs, M plans, L legends, K facts`. 경로 부족 경고 없음. 틀린 id는 파일·줄 번호 출력 |
| **2. 시계와 틱** | `World::Update`, 계절, 시한 플래그, RNG, 월드 로그 | `F5`에 날짜가 흐름. `F6`으로 계절이 바뀜 |
| **3. 전설 식재료와 계획** | §5.3, §9, `take` | `--sim-test 112`에서 17.3 표의 21·41·45일 사건까지 재현 |
| **4. NPC 행동** | §10, `recruit_hostile`, `join` | 17.3 표 전체 재현. 로그에 `action`·`join` 줄 |
| **5. 친화도·이벤트·소문** | §7, §8, §11 | 항구에서 리모네 드라고를 잡으면 목격자 친화도가 오르고, 며칠 뒤 만난 적 없는 NPC도 오름 |
| **6. 사실·아이템·선택지** | §12, 15.12~13, 선택지 | 마테오 신뢰도를 올리면 "묻는다"가 생기고 기록장에 추가됨. 오르소 폭로 선택지로 심사 결과가 바뀜 |
| **7. 성장 인터페이스** | §13 | 전설 식재료를 먹으면 전력·메뉴판·타르투 크기가 바뀜 |
| **8. 결말** | §14 | 개입 없는 `--sim-test`는 `gg_black`. 열쇠와 통행증을 얻은 세이브는 다른 결과 |
| **9. 화면 연결** | §16.3 | 튜토리얼 후 시계가 돌고, NPC·전설 식재료가 상태에 따라 나타나고 사라짐 |
| **10. 세이브** | `Save/LoadSave` | 저장 → 불러오기 후 같은 시드 결과가 이어짐 (RNG 포함) |

### 버티컬 슬라이스 목표

- 맵: 고향(튜토리얼) + 항구 도시
- 상호작용 NPC: 고향 주민 + 마테오, 오르소, 세라핀, 발렌틴, 루치아, 도나토
- 전설 식재료: 리모네 드라고(첫 전설 사냥), 폴포 레(발렌틴과의 레이스)
- **폴포 레 레이스를 완전한 사례로 만든다.**

| 플레이어의 선택 | 경로 | 결과 |
|---|---|---|
| 발렌틴보다 먼저 잡기 | 마테오 신뢰 40 또는 엄마의 요리 노트 → 손질법 → 20일 전에 포획 | 발렌틴 인정 +, 친화도 −. 명성 상승 |
| 발렌틴과 친해지기 | 대접으로 인정 70 | 발렌틴이 레이스에서 물러나고 협력자가 됨 |
| 선단 막기 | 루치아 동맹 → `patrol_sea` / 도나토 격퇴 | 검은 식탁이 폴포 레를 못 가져감, 지령서 획득 |
| 방관 | 없음 | 20일 발렌틴, 86일 검은 식탁 |

- 나머지 지역은 전략 층에서만 돈다. 맵을 추가할 때 코드 변경 없이 데이터만 채우면 되는 것이 확장성의 증명이다.

---

## 19. 미정 / 튜닝 대상

| 항목 | 현재 기본값 | 비고 |
|---|---|---|
| 하루 길이 | 1200초 | 전체 37시간 |
| 전력 수치 | 이 문서의 값 | 플레이어가 전부 먹으면 90 + 동맹. 검은 식탁 기본 약 205 → 동맹과 방해가 필수 |
| 전투 | `OnHostileEncounter` 스텁 | 전투 방식 확정 후 연결 |
| 그랑 테라 맵 | 없음 (판정만) | 확장 시 일식 날 최종 지역으로 |
| 칸별 후보 식재료 | 칸당 1종 (+열쇠) | 확장 시 칸당 2~3종 |

---

*이 문서는 스토리와 시뮬레이션의 기준 명세다. 데이터 스키마를 바꾸면 §15와 세이브 버전을 같이 올린다.*
