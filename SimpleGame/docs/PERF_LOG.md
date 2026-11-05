# PERF_LOG — 성능 로그 스키마와 판독 가이드 (gg-perf/1)

이 문서는 `Logs/perf_*.jsonl` 을 사람과 AI 가 같은 방식으로 읽기 위한 사전이다.
로그를 분석할 때는 이 문서를 함께 읽는다. 지표 이름·단위·경고 코드는 코드와 1:1 로 맞춰져 있다.

---

## 1. 파일

| 경로 | 내용 |
|---|---|
| `Logs/perf_YYYYMMDD_HHMMSS.jsonl` | 실행 1회 = 파일 1개. JSON Lines (한 줄 = 한 레코드) |
| `Logs/perf_latest.txt` | 가장 최근 로그 파일 이름 한 줄 |
| 게임 화면 오른쪽 위 | `FPS 60   16.7 ms` — 0.25초마다 갱신, 목표 대비 초록/노랑/빨강. **F3** 으로 켜고 끔 |
| 콘솔 (사람용) | 5초마다 요약 블록 + 종료 때 세션 요약. 아래 「콘솔 읽는 법」 |

작업 디렉터리 기준 상대 경로다 (Visual Studio 에서 실행하면 프로젝트 폴더의 `Logs/`).
파일은 1초마다 `fflush` 하므로 크래시가 나도 직전 구간까지 남는다.
`session_end` 는 ESC 또는 창 닫기로 정상 종료했을 때만 기록된다.

### 콘솔 읽는 법

```
── 성능 1:25 ────────────────────────────── 정상
  FPS   60.0  (최저 59.2, 목표 60)
  시간  CPU 0.57ms (갱신 0.04 + 그리기 0.53)  ·  GPU 1.17ms  ·  교체 대기 15.9ms  / 예산 16.7ms
  그리기  DrawCall 8  ·  도형 8,206  ·  가장 무거운 액터: 나무·바위 그리기 0.11ms
  ! 끊김 2회 — 가장 긴 프레임 41ms (원인: 몬스터 갱신 35.1ms)
  ! 새 글자 3개 생성 — 처음 나온 글자에서 잠깐 멈출 수 있음
```
- 머리줄 오른쪽 한 단어가 5초 구간의 상태다. **정상**(초록) · **주의**(노랑, 지금은 괜찮지만 커질 수 있는 것) · **경고**(빨강, FPS·끊김·CPU/GPU 예산) · **시작 중**(회색, 시작 3초 포함 — 무시해도 됨)
- `CPU` 가 예산보다 한참 작으면 여유가 있다. `교체 대기` 는 vsync 로 다음 화면을 기다린 시간이라 크면 오히려 정상이다
- `!` 줄은 경고를 한국어로 풀어 쓴 것. 같은 경고는 5초 안에서 한 줄로 합치고 가장 나쁜 값을 보인다
- 끊김의 `원인` 은 그 프레임에서 가장 오래 걸린 세부 작업이다 (게임 갱신·그리기 같은 묶음 이름은 건너뛴다)
- 종료 때 `══ 성능 요약 ══` 에 평균 FPS, 프레임 시간(보통 / 느린 5% / 최악), 끊김 수, 경고가 났던 초 수가 나온다
- 5초 간격은 `Profiler.cpp` 의 `CONSOLE_PERIOD_MS`

---

## 2. 이름 규칙

- 점으로 계층: `<영역>.<대상>.<세부>` — `scene.render.actor.monster`
- 단위는 접미사: `_ms` 밀리초, `_bytes` 바이트, `_mb` 메가바이트, `_pct` 퍼센트. 접미사가 없으면 **개수**
- 영역: `frame` 프레임 루프 · `render` 렌더러 · `gpu` GPU 타이머 · `scene` 씬 그래프 · `text` 글자 · `mem` 메모리 · `level1` / `tutorial` 씬 고유 · `startup` 시작
- **스코프 시간은 포함 시간이다.** `level1.render` 는 그 안의 `scene.render.*`, `render.flush` 를 포함한다. 합치면 중복 계산된다.

### 지표 종류

| 종류 | 매크로 | window 레코드 위치 | 집계 |
|---|---|---|---|
| scope | `PROFILE_SCOPE` | `cpu_ms` | 프레임당 평균 ms, 프레임 최대 ms, 프레임당 호출 수 |
| counter | `PROFILE_COUNT` | `counters` | 프레임당 평균, 프레임 최대, 구간 합 |
| sample | `PROFILE_SAMPLE` | `samples` | 표본 평균, 최대, 표본 수 (GPU 시간처럼 프레임과 어긋나 도착하는 값) |
| gauge | `PROFILE_GAUGE` | `gauges` | 마지막 값 |

---

## 3. 레코드 종류

모든 레코드에 `"type"` 과 `"schema":"gg-perf/1"` 이 있다. `t_s` 는 첫 프레임 기준 초, `frame_index` 는 프레임 번호.

### session_start (항상 첫 줄)
```json
{"type":"session_start","local_time":"2026-09-23T15:04:05","build":"Release",
 "gl_renderer":"NVIDIA GeForce ...","gl_version":"4.6.0 ...","resolution":[1280,720],
 "target_fps":60,"vsync":-1,"budget_ms":16.667,"startup_ms":180.2,"warmup_ms":3000}
```
- `budget_ms` = 1000 / target_fps. 모든 "느리다" 판단의 기준
- `vsync` -1 은 드라이버 기본값 (대부분 켜짐)

### window (1초마다)
```json
{"type":"window","t_s":12.0,"frame_index":720,"warmup":false,"frames":60,"fps":60.1,
 "target_fps":60,"budget_ms":16.667,
 "frame":{"interval_ms":{"avg":16.6,"p95":17.1,"max":18.0},
          "cpu_work_ms":{"avg":2.1,"p95":2.8,"max":3.4},"headroom_ms":14.5,"hitches":0},
 "cpu_ms":{"frame.render":{"avg":1.6,"max":2.1,"calls_per_frame":1.0}, ...},
 "counters":{"render.draw_calls":{"avg":7,"max":7,"sum":420}, ...},
 "samples":{"gpu.frame_ms":{"avg":1.2,"max":1.5,"n":60}, ...},
 "gauges":{"mem.working_set_mb":120.3, ...},
 "warnings":[{"code":"HITCH","value":1,"limit":0},
             {"code":"ACTOR_UPDATE_SLOW","value":2.4,"limit":2.0,"subject":"scene.update.actor.monster"}]}
```
- `cpu_ms` 는 **평균이 큰 순서로 정렬**되어 있다. 앞에서부터 읽으면 된다
- `frame.interval_ms` 실제 프레임 간격 (벽시계). FPS 의 역수
- `frame.cpu_work_ms` = `frame.update` + `frame.render`. CPU 가 실제로 일한 시간 (스왑 대기 제외)
- `frame.headroom_ms` = budget − cpu_work 평균. **남은 여유**. 음수면 CPU 가 예산을 넘는다
- `warmup:true` 인 구간(시작 3초)은 경고를 내지 않는다. 셰이더 컴파일·글리프 생성 같은 시작 비용 때문

### hitch (예산의 2배를 넘긴 프레임)
```json
{"type":"hitch","t_s":8.4,"frame_index":503,"warmup":false,"interval_ms":41.2,
 "cpu_work_ms":38.0,"budget_ms":16.667,
 "top_cpu_ms":[{"name":"level1.update","ms":35.1},{"name":"scene.update","ms":34.9}, ...],
 "counters":{"text.glyph_builds":12,"render.draw_calls":7}}
```
- 그 **한 프레임**의 상위 스코프 8개와 0 이 아닌 카운터. 원인 프레임을 직접 짚는다
- 구간당 최대 20개까지만 기록 (로그 폭주 방지). 전체 수는 window 의 `hitches`

### event (드문 일)
| name | 필드 | 뜻 |
|---|---|---|
| `startup.models_load` | ms | 모델 캐시 로드/빌드 |
| `startup.complete` | ms | main 시작부터 첫 프레임 직전까지 |
| `level1.reset` | seed, rooms, monsters, props, actors, ms | 맵 생성 (R 키 포함). 실행 중이면 그 프레임이 히치가 된다 |
| `level1.ground_bake` | decor, decor_skipped, ms | 지면 장식(잡초·자갈) 정적 배치 기록 (맵마다 1회). skipped 는 곡선 경계 밖이라 안 심은 수 |
| `render.terrain_upload` | tiles | 곡선 지형용 타일 종류 텍스처 업로드 (맵마다 1회) |
| `render.static_batch_upload` | instances, bytes | 정적 배치 GPU 업로드 |
| `text.atlas_reset` | reason | 글리프 아틀라스가 가득 차 비움. 한 프레임 글자가 깨질 수 있다 |
| `scene.switch` | to | F1/F2 씬 전환 |

### session_end (정상 종료 시 마지막 줄)
- `frame_interval_ms` p50/p95/p99/max — 세션 전체 체감 품질
- `worst_frames` 최악 10 프레임 (`frame_index` 로 hitch 레코드와 대조)
- `cpu_ms`, `counters` 세션 평균/최대, `gauges_max`
- `warnings_seen` 코드별 발생 구간 수

---

## 4. 지표 사전

### frame
| 이름 | 종류 | 뜻 | 문제 신호 |
|---|---|---|---|
| `frame.update` | scope | 게임 갱신 전체 (Tick) | 예산의 50% 이상 |
| `frame.render` | scope | 그리기 전체 (CPU 쪽 명령 생성 + 큐 제출) | 예산의 50% 이상 |
| `frame.swap` | scope | `glutSwapBuffers` 대기 | 길면 **GPU 또는 vsync 대기**. CPU 최적화로는 안 줄어든다 |
| `tutorial.update` / `tutorial.render` | scope | 튜토리얼 씬 (F1) | |

### render (Renderer.cpp)
| 이름 | 종류 | 뜻 | 문제 신호 |
|---|---|---|---|
| `render.draw_calls` | counter | 프레임당 glDraw* 수. 정상 8 (지형 1 + 지면 장식 1 + 월드 큐 1 + 사후처리 4 + UI 1) | > 100 이면 `DRAW_CALLS_HIGH`. 늘었다면 `render.flush.*` 로 원인 확인 |
| `render.instances` | counter | 그린 도형 수 (정적 포함) | > 20,000 `INSTANCES_HIGH` |
| `render.instances_static` | counter | 그중 정적 배치에서 나온 것 | 맵이 커지면 비례 증가 → 청크 분할 필요 |
| `render.upload_bytes` | counter | 프레임당 GPU 버퍼 업로드 | > 2 MB `UPLOAD_HIGH` |
| `render.flush.fbo_switch` | counter | 렌더 타깃 전환으로 큐를 비운 횟수 (EndFrame) | 정상 1 |
| `render.flush.frame_end` | counter | 프레임 끝 (FinishFrame) | 정상 1 |
| `render.flush.static_batch` | counter | 정적 배치 앞에서 큐를 비운 횟수 | 0 이 정상. 늘면 정적 배치 사이에 동적 도형이 끼었다 |
| `render.flush.capacity` | counter | 큐가 65,536 을 넘어 중간에 비움 | 0 이 정상 |
| `render.flush.terrain` | counter | 지형 앞에서 큐를 비운 횟수 | 0 이 정상 (지형은 프레임 맨 처음) |
| `render.terrain` | scope | 곡선 지형 그리기 제출 (CPU 쪽). GPU 비용은 `gpu.scene_ms` 에 포함 | |
| `render.flush` | scope | 큐 업로드 + 인스턴스 draw 제출 | |
| `render.static_draw` / `render.static_upload` | scope | 정적 배치 그리기 / 업로드 | |
| `render.post` | scope | 사후처리 4패스 제출 (CPU 쪽) | |
| `render.queue_capacity` | gauge | 큐 벡터 용량 | 계속 커지면 한 프레임 도형 수 폭증 |
| `gpu.scene_ms` / `gpu.post_ms` / `gpu.ui_ms` | sample | GPU 가 각 패스에 쓴 시간 (타이머 쿼리, 몇 프레임 늦게 도착) | |
| `gpu.frame_ms` | sample | 세 패스 합 | > 예산 × 0.8 `GPU_BOUND` |
| `gpu.query_not_ready` | counter | 링 버퍼가 부족해 GPU 결과를 못 읽음 | 자주 나오면 GPU 가 4프레임 이상 밀려 있다 |

### scene (SceneGraph.cpp)
| 이름 | 종류 | 뜻 | 문제 신호 |
|---|---|---|---|
| `scene.nodes` | counter | 트리의 Actor 수 | 모든 순회 비용의 선행 지표 |
| `scene.update` | scope | 트리 갱신 전체 | |
| `scene.flush` | scope | 추가/삭제 반영 | 크면 한 프레임에 생성·파괴가 몰렸다 |
| `scene.update.actor.<클래스>` | scope | 그 클래스의 OnUpdate 합 | > 2 ms `ACTOR_UPDATE_SLOW` |
| `scene.render.actor.<클래스>` | scope | 그 클래스의 OnRender 합 | > 2 ms `ACTOR_RENDER_SLOW` |
| `scene.render.bounds` / `collect` / `sort` / `emit` | scope | 경계 갱신 / 컬링 순회 / 깊이 정렬 / OnRender 호출 | |
| `scene.visited` | counter | Render 에서 방문한 노드 (월드+오버레이 두 번 합) | |
| `scene.culled_subtrees` | counter | 서브트리째 버린 노드 | nodes > 500 인데 0 이면 `CULLING_INEFFECTIVE` |
| `scene.culled_self` / `scene.drawn_actors` | counter | 자기만 버림 / 실제 그린 Actor | |

클래스 이름: `ground` `prop` `monster` `player` `weapon` `drop` `float_text`

### text (KoreanFont.cpp)
| 이름 | 종류 | 뜻 | 문제 신호 |
|---|---|---|---|
| `text.glyph_builds` | counter | GDI 로 새 글자를 그려 아틀라스에 올린 수 | 워밍업 뒤에도 > 0 이면 `GLYPH_BUILD_RUNTIME` (처음 보는 글자마다 멈칫) |
| `text.glyph_build` | scope | 글리프 생성 시간 | |
| `text.upload_bytes` | counter | 아틀라스 업로드 | |
| `text.glyphs_cached` | gauge | 캐시된 글리프 수 (3개 폰트 합) | |
| `text.atlas_used_pct` | gauge | 아틀라스 사용률 | > 85 `ATLAS_NEAR_FULL` |
| `text.atlas_resets` | counter | 아틀라스 비움 | > 0 `ATLAS_RESET` |

### mem
| 이름 | 종류 | 뜻 | 문제 신호 |
|---|---|---|---|
| `mem.working_set_mb` | gauge | 물리 메모리 사용 | 워밍업 뒤 기준보다 64 MB 넘게 늘면 `MEMORY_GROWTH` (누수 의심) |
| `mem.private_mb` | gauge | 전용 커밋 메모리 | |

### level1 (Level1.cpp)
| 이름 | 종류 | 뜻 | 문제 신호 |
|---|---|---|---|
| `level1.update` / `level1.render` / `level1.ui` | scope | 레벨 갱신 / 그리기 / UI | |
| `level1.monster_sep_pairs` | counter | 몬스터 밀어내기 비교 횟수 (N²) | > 20,000 `QUADRATIC_GROWTH` → 공간 격자 필요 |
| `level1.monsters_alive` / `drops` / `float_texts` | gauge | 개체 수 | |
| `level1.prop_cache_builds` / `prop_cache_instances` | counter | 프롭 도형 기록 (처음 화면에 들어올 때 1회) | 계속 나오면 캐시가 무효화되고 있다 |

### tutorial (Game.cpp — 고향 마을)
| 이름 | 종류 | 뜻 | 문제 신호 |
|---|---|---|---|
| `tutorial.update` / `tutorial.render` | scope | 마을 갱신 / 그리기 (SimpleGame.cpp) | |
| `tutorial.npc` | scope | 주민 에이전트 갱신 (일과 이동, 행동, 말풍선) | > 2 ms 면 행동 로직이나 길찾기 |
| `tutorial.path` | scope | A* 길찾기 1회. 프레임당 최대 3회 (`PATH_BUDGET`) | 새벽·저녁 전환 때만 몰려야 정상 |
| `tutorial.path_nodes` | counter | 그 프레임 A* 가 연 칸 수 | 평소에도 수천이면 목표가 막힌 곳 (경로 실패 반복) |
| `tutorial.npcs_present` / `monsters_alive` / `drawn_entities` | gauge | 보이는 주민 / 살아 있는 잡몹 / 그린 개체 | monsters_alive 가 계속 줄면 재출현이 안 되고 있다 |

### sim (Sim/SimWorld.cpp — STORY_SIM.md 17.4)
| 이름 | 종류 | 뜻 | 문제 신호 |
|---|---|---|---|
| `sim.tick` | scope | 하루 틱 전체 (새벽마다 1회) | 틱 프레임이 히치로 잡히면 여기부터 |
| `sim.brain` | scope | NPC 행동 선택·실행 | |
| `sim.actions` | counter | 그 틱에 실행된 NPC 행동 수 | |

---

## 5. 경고 코드

| 코드 | 조건 | 흔한 원인 | 먼저 볼 것 |
|---|---|---|---|
| `LOW_FPS` | fps < target × 0.9 | CPU 또는 GPU 초과, 히치 | `frame.headroom_ms`, `gpu.frame_ms`, `frame.swap` |
| `FRAME_BUDGET_EXCEEDED` | cpu_work p95 > budget | CPU 쪽 작업 과다 | `cpu_ms` 상위 항목 |
| `HITCH` | 구간 안 히치 수 > 0 | 맵 생성, 글리프 생성, 대량 생성/파괴 | 같은 구간의 `hitch` 레코드 `top_cpu_ms` |
| `GPU_BOUND` | gpu.frame_ms 평균 > budget × 0.8 | 화면 채움(fill), 사후처리, 도형 과다 | `gpu.scene_ms` vs `gpu.post_ms` |
| `DRAW_CALLS_HIGH` | draw_calls 평균 > 100 | 배치가 끊김 | `render.flush.*` 중 늘어난 것 |
| `INSTANCES_HIGH` | instances 평균 > 20,000 | 도형 수 과다 | `render.instances_static`, `scene.drawn_actors` |
| `UPLOAD_HIGH` | upload 평균 > 2 MB/프레임 | 동적 도형 과다, 정적 배치 재업로드 | `render.static_batch_upload` 이벤트 반복 여부 |
| `GLYPH_BUILD_RUNTIME` | 워밍업 뒤 glyph_builds > 0 | 처음 보는 글자 | 자주 쓰는 글자를 시작 때 미리 만든다 |
| `ATLAS_NEAR_FULL` / `ATLAS_RESET` | 사용률 > 85 / 리셋 발생 | 글자 종류 과다, 폰트 크기 추가 | 아틀라스 크기 (Renderer.cpp `m_Atlas.Create`) |
| `CULLING_INEFFECTIVE` | nodes > 500 이고 culled_subtrees 평균 < 1 | 셀로 묶지 않은 대량 Actor, 경계 미설정(무한) | 새 Actor 의 `SetBounds`, 그룹 구조 |
| `QUADRATIC_GROWTH` | sep_pairs 평균 > 20,000 | 몬스터 수 증가 | 몬스터 분리 로직에 공간 격자 |
| `MEMORY_GROWTH` | working_set 증가 > 64 MB | 누수, 캐시 무한 증가 | `gauges` 중 계속 커지는 것 |
| `ACTOR_UPDATE_SLOW` / `ACTOR_RENDER_SLOW` | 한 클래스 합 > 2 ms | 그 클래스 로직/그리기 | `scene.update.actor.*` / `scene.render.actor.*` |

---

## 6. 분석 절차 (AI 용)

1. `perf_latest.txt` 로 파일을 찾는다.
2. `session_start` 에서 `build`(Debug 는 수 배 느리다), `gl_renderer`, `target_fps`, `vsync`, `budget_ms` 를 확인한다.
3. `session_end` 가 있으면 `frame_interval_ms.p95/p99`, `hitches`, `warnings_seen` 으로 전체 그림을 잡는다. 없으면 비정상 종료다.
4. `warmup:false` 인 `window` 중 `warnings` 가 있는 구간을 모은다.
5. 구간마다 CPU 인지 GPU 인지 가른다.
   - `frame.cpu_work_ms` 가 예산에 가깝다 → CPU. `cpu_ms` 를 앞에서부터 본다 (포함 시간이므로 부모-자식 관계를 이름으로 따라간다)
   - `gpu.frame_ms` 가 크다, `frame.swap` 이 길다 → GPU
   - 둘 다 작은데 `interval_ms` 가 크다 → 루프 박자/OS 스케줄링. `target_fps`, 타이머 설정 확인
6. `hitch` 레코드는 `frame_index` 와 `t_s` 로 `event` 와 대조한다 (예: `level1.reset` 직후 히치).
7. 추세를 본다: `scene.nodes`, `render.instances`, `level1.monster_sep_pairs`, `mem.*` 가 시간에 따라 늘면 지금은 괜찮아도 앞으로 터질 곳이다.

---

## 7. 계측 추가 규칙

- 새 시스템(AI, 물리, 스트리밍 등)은 진입 함수에 `PROFILE_SCOPE("<영역>.<이름>")` 를 단다.
- 새 Actor 클래스는 생성자에서
  ```cpp
  static const ActorProfile prof = SceneGraph::RegisterActorProfile("<클래스>");
  SetProfile(prof);
  ```
- 개수가 늘어나면 비용이 커지는 곳(N², 전체 순회)은 비교 횟수를 `PROFILE_COUNT` 로 남긴다.
- 드물지만 비싼 일은 `Profiler::Event(name, "\"ms\":...")`.
- 새 지표를 만들면 이 문서의 4장에 한 줄 추가한다. 경고 규칙을 추가하면 5장과 `Profiler.cpp` 의 `FlushWindow` 를 같이 고친다.
- 이름은 한번 정하면 바꾸지 않는다. 바꾸면 과거 로그와 비교가 끊긴다. 바꿔야 하면 `schema` 버전을 올린다.
