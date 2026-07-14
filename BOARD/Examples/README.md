# Examples — 엔진으로 만든 예제 게임 모음

이 폴더의 파일들은 **엔진 코어를 전혀 수정하지 않고 `GameLogic.cpp` 하나로 만든** 완성 게임입니다.
"이 엔진으로 실제 게임을 만들 수 있는가"를 검증하는 레퍼런스입니다.

| 파일 | 게임 | 필요한 에셋 |
|------|------|-------------|
| `Gomoku_GameLogic.cpp` | 오목 (15×15, Tile 40) | `assets/click.wav` 만 (돌은 colorTint) |
| `Chess_GameLogic.cpp`  | 체스 (8×8, Tile 64) — 체크/체크메이트/스테일메이트 포함 | `assets/click.wav` 만 (말은 색칸 + 글자 P·N·B·R·Q·K) |

> 저장소 기본 `GameLogic/GameLogic.cpp` 는 현재 **오목**입니다.
> 두 게임 모두 **커스텀 이미지 에셋 없이** 실행됩니다 (체스는 엔진의 Label/`SetObjectText` 사용).

---

## 게임 바꾸기 — GameLogic.cpp 교체 후 재빌드

엔진은 DLL 안의 `GameLogic.cpp` **하나**만 게임 로직으로 사용합니다. 게임을 바꾸려면 그 파일을 갈아끼우면 됩니다.

```
1. 원하는 예제 내용을 GameLogic/GameLogic.cpp 에 통째로 덮어씁니다.
   (엔진 개발자용 경로) BOARD/Engine/GameLogic/GameLogic.cpp
   (SDK 크리에이터 경로) BoardEngine_SDK/GameLogic/GameLogic.cpp
2. GameLogic 프로젝트만 빌드 → GameLogic.dll 갱신
3. 에디터라면 자동 핫 리로드(0.5초). 아니면 GameLogic ▸ Reload Now.
```

게임마다 보드 크기가 다르므로 **씬(scene.json)도 그 게임에 맞게** 만들어 두세요 (오목 15×15/40, 체스 8×8/64).

### 체스 말 표시 — 이미지 없이 글자로

체스 말은 **팀 색으로 칠한 타일 + 말 종류 글자**(P·N·B·R·Q·K)로 표시합니다.
이는 엔진에 추가된 **Label 컴포넌트 / `SetObjectText` API** 덕분이며, **커스텀 이미지가 필요 없습니다.**

```cpp
m_api->AddSpriteRenderer(p, "assets/tile.png");           // 칸
p->GetComponent<SpriteRenderer>()->colorTint = 팀색;       // 흰/검 구분
m_api->SetObjectText(p, "N", 0.1f, 0.1f, 0.1f);           // 나이트 = "N"
```

> **엔진 관찰 → 개선 기록:** 예전엔 오브젝트 위에 글자를 못 그려서(상단 상태텍스트만 존재)
> 말 종류가 많은 게임은 이미지에 의존했습니다. 그래서 엔진에 **Label/SetObjectText** 를
> 추가했고, 이제 체스도 이미지 없이 플레이됩니다. (Unity TextMesh / Godot Label 모티브)
>
> 진짜 말 그림을 쓰고 싶다면 `SetObjectText` 대신 `SetSpriteTexture(p, "assets/chess/w_knight.png")`
> 로 바꾸면 됩니다.

---

## 만든 게임 실행하는 법

### A. 에디터에서 바로 플레이 (개발 중 가장 빠름 — `STANDALONE_MODE 0`)

```
1. BOARD/Engine/Engine.sln 을 Visual Studio 2022 로 연다.
2. 구성: Debug | x64  → 솔루션 빌드(Ctrl+Shift+B) → F5 실행.
   ⚠ 이번에 엔진에 RestartGame API 를 추가했으므로, 저장소에 있던 기존 Engine.exe/DLL 은
      한 번은 반드시 다시 빌드해야 새 기능이 반영됩니다.
3. Board Generator 창에서 보드 생성 (오목 15/15/40, 체스 8/8/64) → File ▸ Save Scene.
4. GameLogic ▸ Load GameLogic.dll.
5. 우상단 [ Play ] 버튼 → 타일/말을 클릭해 플레이.
   - GameLogic.cpp 를 수정하고 GameLogic 만 다시 빌드하면 0.5초 내 자동 핫 리로드.
```

### B. 배포판(에디터 UI 없는 단독 실행 — `STANDALONE_MODE 1`)

```
1. 먼저 A 방식으로 scene.json 을 저장해 둔다 (<exe폴더>/assets/scene.json).
2. Engine/Engine/Engine.h 최상단: #define STANDALONE_MODE 1
3. 구성 Release | x64 로 솔루션 빌드.
4. Engine/x64/Release/ 에 나온 결과물을 한 폴더에 모은다:
     Engine.exe
     GameLogic.dll
     shaders.hlsl              ← 🚨 반드시 복사 (없으면 즉시 어설션 에러)
     assets/                   ← scene.json, tile.png, click.wav, (체스면) chess/*.png
5. Engine.exe 더블클릭 → 씬·DLL 자동 로드, 즉시 Play 모드로 시작.
```

> 배포 시 `shaders.hlsl` 과 `assets/` 를 exe 옆에 두는 것을 잊지 마세요. 자세한 내용은
> 루트 `README.md` 와 `Engine/Engine_Manual.md` 참고.
