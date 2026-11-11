# Art — 블렌더 에셋 파이프라인

컨셉: **먹음직스러운 미니어처 디오라마.** 둥글고 통통한 클레이 형태, 구운 듯한 재질,
윤기·반투명·김은 음식과 미식종에만. 색은 프로방스 기준 팔레트 (CLAUDE.md 5.2).

모든 에셋은 `scripts/` 의 파이썬이 블렌더로 처음부터 만든다. 손으로 고친 .blend 는
다음 재생성 때 덮어써지므로, 바꾼 점은 스크립트에도 반영한다.

## 폴더

| 경로 | 내용 |
|---|---|
| `blend/*.blend` | 원본. 메쉬·아마추어·스키닝·IK·액션·머티리얼·카메라·조명이 다 들어 있다. 열면 게임과 같은 쿼터뷰 카메라 |
| `textures/<에셋>/` | 베이크 텍스처 5종: `basecolor`(sRGB), `roughness`, `normal`(탄젠트, OpenGL +Y), `bump`(미세 높이), `height`(노멀의 원본 높이, 참고용) |
| `renders/` | 스프라이트 원본 프레임 (재생성 시 생김, 커밋 불필요) |
| `scripts/` | 생성 스크립트 |

## 에셋 목록

| 에셋 | 리그 | 액션 (스프라이트) |
|---|---|---|
| 리노 `rino` | 22본, 다리 IK(폴 타깃) + 팔 FK | idle 6 · walk 8 · gratitude 8 (감사 의례) |
| 리노 + 사냥 국자 `rino_ladle` | 리노와 같은 리그, 국자는 오른손 | idle 6 · walk 8 · attack 8 (레벨 1, `LADLE_T_*` 구간과 같은 박자) |
| 주민 `villager` · `villager_cloth` · `villager_hat` | 22본 (리노와 같은 구성, 성인 비례) | idle 6 · walk 8. 옷 레이어는 엔진이 NPC 옷 색을 곱한다. 모자 11종은 기본 자세 한 장씩 + 프레임마다 머리 앵커 |
| 타르투 `tartu` | 16본 (척추·목·머리·귀·꼬리2·다리8) | idle 6 · walk 6 · sniff 6 |
| 토마토마 `tomatoma` | 6본 (몸·꼭지·꼭다리·발2) | idle 6 · hop 8 · caught 8 (빙글 돌며 빛이 됨, 머티리얼 발광 키) |
| 집 `house` | — | 정적 |
| 나무 `tree` | 5본 바람 리그 (`tree_wind` 액션) | 정적 (흔들림은 엔진 `SetSway`) |
| 화덕 + 솥 `pot` | — | `pot_hearth`, `pot_cauldron` 두 장 (사이에 엔진 불꽃) |
| 요리 `dish` | — | `dish_icon` (월드), `dish_closeup` (조리 완료 화면) |
| 알감자 `potato` · 방울콩 `bean` · 버섯당 `mush` | 토마토마와 같은 6본 | idle 6 · hop 8 · caught 8 (`critters.py`) |
| 호수사슴 `lakedeer` | 15본 (몸·목·머리·귀·꼬리·다리8) | idle 8 |
| 알밤멧돼지 `boar` | 10본 (몸·머리·귀2·꼬리·다리4) | idle 6 · run 8 · windup 6 · charge 4 · caught 8 (레벨 1) |
| 재료 `items` | — | `item_berry` `item_meat` `item_core` (레벨 1 드롭) |
| 광장 건물 `landmarks` | — | `house_tavern` `house_inn` `house_guild` `mill` `house_manor` + `guild_flag_0..7` (바람) + `mill_blades_0..15` (90도 회전). 건물과 같은 피벗으로 겹친다 |
| 소품 `props` | — | 소나무, 덤불, 바위, 우물, 상자, 술통, 울타리, 포도나무(+송이), 갈대, 햇살풀, 천막 2색, 포장마차, 노점 5색, 모닥불 (`props.py`) |

## 다시 만들기

블렌더 5.2 의 파이썬 모듈(`pip install bpy==5.2.3`) + `numpy pillow OpenEXR` 가 필요하다.
블렌더 앱으로 돌려도 된다: `blender -b -P scripts/rino.py`.

```
cd Art/scripts
python rino.py              # 리노 → blend/rino.blend, textures/rino, renders/rino
python tartu.py
python tomatoma.py
python env.py house         # house | tree | pot | dish
python critters.py          # potato bean mush
python lakedeer.py
python villager.py          # GG_ONLY=hats 면 모자만 다시
python boar.py
python items.py
python landmarks.py         # tavern inn guild mill manor 중 골라서도 된다
python props.py             # 이름을 주면 그 소품만 (python props.py well crate)
python pack_atlas.py        # renders/* → ../Data/Sprites/sprites.{png,txt}, sprites_n.png
python mock_scene.py        # 엔진과 같은 좌표 계산으로 마을 한 장면 합성 (검수)
```

`GG_STAGE=model` 을 주면 모델 미리보기만, `GG_STAGE=anim` 은 리그·애니메이션 검수 렌더만
`preview/` 에 낸다. CPU 2코어 기준 전체 재생성 약 12분.

## 규격 (엔진과 맞춘 숫자)

- 정사영 카메라 회전 (60°, 0°, 45°). **1 BU = 1 타일.** 1배 기준 가로 50.91 px/BU, 수직 44.09 px/BU
- 블렌더 X = 게임 wx, 블렌더 Y = −게임 wy. 캐릭터 정면은 블렌더 −Y
- 스프라이트 배율 2 (타르투 3). 엔진은 `sprites.txt` 의 scale 로 나눠 1배로 그린다
- 뷰 변환 Standard. 톤 커브·그레이딩은 엔진 Composite.fs 몫
- 조명은 공용 리그 하나 (`ggcommon.light_rig`). 바닥 그림자는 굽지 않는다
- 캐릭터·몬스터는 `G.MATTE = 1.0` (반사광 한 단계 낮춤). 음식·환경은 0
- 따로 굽는 오브젝트끼리 머티리얼을 공유하지 않는다. 블렌더 베이크는 오브젝트의 모든 머티리얼의 활성 이미지 노드에 결과를 써서, 먼저 구운 오브젝트의 텍스처를 덮어쓴다
- 겹쳐 그리는 레이어(주민 옷·모자, 깃발, 풍차 날개)는 나머지를 홀드아웃 머티리얼로 바꿔 굽는다. 가려지는 부분이 정확해서 엔진은 순서대로 겹치기만 하면 된다
- `sprites.txt` 의 `f` 줄 끝에 `<ax> <ay>` 가 붙으면 피벗 기준 앵커. 주민 몸 = 머리, 모자 = 모자 자리. 엔진은 (몸 앵커 - 모자 앵커) 만큼 모자를 옮긴다
- 노멀맵은 머티리얼의 높이장을 셰이더 안에서 3D 유한차분으로 미분해 탄젠트 공간으로 굽는다 (블렌더의 Bump 노드는 베이크에서 0 이 되기 때문)
- `sprites_n.png` 는 스프라이트용 카메라 공간 노멀. 엔진은 아직 안 읽는다 — 횃불·낮밤 동적 조명을 붙일 때 쓴다
