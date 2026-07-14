# D3D12 Board Game Engine

> **DirectX 12 기반 범용 2D 보드게임 엔진 & 에디터**
>
> 오목·체스·장기·쇼기 등 다양한 보드게임을 단일 엔진 위에서 제작·배포할 수 있는 Windows 전용 2D 게임 개발 환경입니다.
> 에디터와 런타임이 하나의 실행 파일로 통합되어 있으며, `GameLogic.dll` 핫 리로드를 통해 엔진 재시작 없이 게임 로직을 라이브 수정할 수 있습니다.

---

## 기술 스택

| 항목 | 사용 기술 |
|------|-----------|
| 렌더링 | DirectX 12 (D3D12) |
| 윈도우 | Win32 API |
| 에디터 UI | Dear ImGui (DX12 백엔드) |
| 게임 로직 | C++ Native DLL (`GameLogic.dll`) |
| 데이터 스크립팅 | Lua 5.4.7 via sol2 v3.3.0 |
| 씬 직렬화 | nlohmann/json v3.11.3 |
| 빌드 환경 | Visual Studio 2022, C++17, x64, Windows 10/11 |

---

## 배포 3단 생태계

이 엔진은 세 가지 역할로 운영됩니다. 본인의 역할에 맞는 경로를 선택하세요.

| 역할 | 진입점 | 해야 할 일 |
|------|--------|-----------|
| **엔진 코어 개발자** | `Engine/Engine.sln` | D3D12 렌더러·에디터 소스 전체를 수정. VS2022 + C++17 환경에서 직접 빌드. |
| **게임 크리에이터** | `BoardEngine_SDK/` | `.\MakeSDK.ps1`으로 SDK를 추출한 뒤 `GameLogic/GameLogic.cpp`만 수정. 엔진 소스 접근 불필요. |
| **최종 게이머** | 배포 패키지 | 크리에이터가 압축한 파일(`Engine.exe` + `GameLogic.dll` + `shaders.hlsl` + `assets/`)을 받아 실행. |

### 역할별 빠른 시작

**엔진 코어 개발자**
```
1. Engine/Engine.sln 을 Visual Studio 2022 에서 엽니다.
2. Debug|x64 빌드 후 F5 로 실행합니다.
3. Board Generator 에서 보드를 생성하고 GameLogic 메뉴에서 DLL 을 로드합니다.
4. [ Play ] 버튼으로 게임 로직을 테스트합니다.
5. Release|x64 로 솔루션 빌드 후 .\MakeSDK.ps1 을 실행해 SDK 를 추출합니다.
```

**게임 크리에이터**
```
1. 엔진 개발자로부터 BoardEngine_SDK/ 폴더를 받습니다.
2. BoardEngine_SDK/GameLogic/GameLogic_SDK.sln 을 Visual Studio 2022 에서 엽니다.
3. GameLogic.cpp 에 게임 로직을 작성합니다.
4. 빌드(Ctrl+Shift+B)하면 GameLogic.dll 이 Engine.exe 옆에 자동 출력됩니다.
5. Engine.exe 실행 → GameLogic 메뉴 → Load GameLogic.dll → [ Play ] 로 테스트합니다.
6. Engine.exe + GameLogic.dll + shaders.hlsl + assets/ 를 압축해 게이머에게 배포합니다.
```

**최종 게이머**
```
1. 크리에이터로부터 받은 압축 파일을 풀면 완성된 게임이 실행됩니다.
   (Engine.exe 를 더블클릭하면 바로 게임이 시작됩니다.)
```

---

## 배포 패키지 파일 구조

게임을 최종 사용자에게 전달할 때 아래 파일이 **모두 동일한 폴더**에 있어야 합니다.

```
MyGame/
├── Engine.exe          ← 게임 실행 파일 (에디터 UI 없음)
├── GameLogic.dll       ← 게임 로직 DLL
├── shaders.hlsl        ← 🚨 필수 — 절대 누락하지 마세요 (아래 경고 참고)
└── assets/
    ├── scene.json      ← 에디터에서 Save Scene 으로 저장한 씬 파일
    ├── tile.png        ← 보드 타일 텍스처
    └── (game-specific images, .wav files …)
```

> **빌드 출력 경로:** `Engine\x64\Release\`

---

## 🚨 `shaders.hlsl` 누락 시 어설션 에러 발생

```
Assertion failed: "Vertex Shader 컴파일 실패 - shaders.hlsl 이 exe 폴더에 있는지 확인"
```

**이 오류의 원인은 거의 항상 `shaders.hlsl` 파일 누락입니다.**

엔진은 시작 시 `D3DCompileFromFile`을 호출해 런타임에 HLSL 셰이더를 컴파일합니다.
이 함수는 **`Engine.exe`와 동일한 디렉터리**에서 `shaders.hlsl`을 찾습니다.
파일이 없으면 버텍스 셰이더 컴파일에 실패하고 즉시 어설션 에러로 종료됩니다.

| 상황 | `shaders.hlsl` 위치 |
|------|---------------------|
| Debug 실행 (VS F5) | `Engine\Engine\` 폴더 — 이미 존재 ✓ |
| Release 빌드 배포 | `Engine\x64\Release\` 폴더에 **수동 복사 필요** ⚠️ |

**배포 전 체크리스트:**

- [ ] `Engine\Engine\shaders.hlsl` 을 `Engine\x64\Release\` 에 복사했는가?
- [ ] `assets/scene.json` 이 `x64\Release\assets\` 아래에 있는가?
- [ ] `GameLogic.dll` 이 `Engine.exe` 와 같은 폴더에 있는가?

---

## 모드 전환 및 빌드 가이드

엔진은 **에디터 모드**와 **배포 모드** 두 가지로 동작합니다.
`Engine\Engine\Engine.h` 최상단의 매크로 한 줄로 전환합니다.

```cpp
// Engine\Engine\Engine.h — 첫 번째 줄
#define STANDALONE_MODE 0   // 0 = 에디터 모드  |  1 = 배포 모드
```

### 모드 비교

| 항목 | `0` 에디터 모드 | `1` 배포 모드 |
|------|----------------|---------------|
| ImGui 에디터 UI | 표시 | **숨김** |
| 씬 로드 | 수동 (File → Load Scene) | 자동 (`assets/scene.json`) |
| DLL 로드 | 수동 (GameLogic 메뉴) | 자동 (`GameLogic.dll`) |
| 핫 리로드 | 지원 (0.5초 주기 감지) | 비활성 |
| Play Mode | 수동 토글 ([ Play ] 버튼) | **즉시 활성** |
| 권장 빌드 구성 | Debug | **Release** |

---

### 에디터 모드 워크플로 (`STANDALONE_MODE 0`)

```
1. Engine.h → #define STANDALONE_MODE 0
2. Debug 빌드 후 F5 실행
3. Board Generator 에서 보드 크기·타일 사이즈 설정 → Generate Board
4. File → Save Scene  (assets/scene.json 저장)
5. GameLogic 메뉴 → Load GameLogic.dll
6. [ Play ] 버튼 → 클릭 이벤트 테스트
7. GameLogic.cpp 수정 → VS 에서 GameLogic 프로젝트만 빌드 → 자동 핫 리로드
```

### 배포 모드 빌드 (`STANDALONE_MODE 1`)

```
1. Engine.h → #define STANDALONE_MODE 1
2. 빌드 구성을 Release 로 변경
3. 빌드 → 솔루션 빌드 (Ctrl+Shift+B)
4. Engine\x64\Release\ 에 생성된 파일 확인
5. shaders.hlsl 을 x64\Release\ 에 복사        ← 🚨 절대 잊지 마세요
6. assets\ 폴더 전체를 x64\Release\assets\ 에 복사
7. Engine.exe, GameLogic.dll, shaders.hlsl, assets\ 를 한 폴더에 묶어 배포
```

> **에디터로 돌아올 때:**
> ```cpp
> #define STANDALONE_MODE 0  // 되돌리기
> ```
> Debug 구성으로 다시 빌드하면 에디터 모드가 복구됩니다.

---

## SDK 배포 가이드 — MakeSDK.ps1 사용법

엔진을 다른 개발자(크리에이터)에게 배포할 때는 **엔진 C++ 소스를 숨기고 헤더와 바이너리만 공개하는 SDK 형태**로 패키징합니다.
루트 경로의 `MakeSDK.ps1` 스크립트가 이 과정을 완전 자동화합니다.

### SDK 구조 — 엔진 소스 없이 동작하는 독립 패키지

```
BoardEngine_SDK/                    ← MakeSDK.ps1 이 생성하는 배포 폴더
├── Engine.exe                      ← 엔진 런타임 바이너리
├── GameLogic.dll                   ← 크리에이터가 빌드하면 여기에 자동 출력
├── shaders.hlsl                    ← 🚨 필수 런타임 셰이더
├── assets/                         ← 게임 에셋 (scene.json, 이미지, .wav)
│
├── include/                        ← 🔓 공개 API 헤더 (소스 없음)
│   ├── IEngineAPI.h                ← 엔진 기능 호출 인터페이스
│   ├── IGameLogic.h                ← 구현해야 할 게임 로직 인터페이스
│   ├── GameObject.h
│   ├── Component.h
│   ├── Transform.h
│   ├── SpriteRenderer.h
│   └── nlohmann/json.hpp           ← Component.h 의존성
│
└── GameLogic/                      ← ✅ 크리에이터가 작업하는 영역
    ├── GameLogic_SDK.sln           ← Visual Studio 에서 이 파일을 여세요
    ├── GameLogic_SDK.vcxproj       ← include/ 참조, DLL → SDK 루트 출력
    └── GameLogic.cpp               ← 게임 로직 구현 파일
```

### vcxproj 핵심 설정

SDK 전용 프로젝트 파일은 원본 `GameLogic.vcxproj`와 두 가지 경로가 다릅니다.

| 항목 | 원본 (Engine.sln 내부) | SDK (`GameLogic_SDK.vcxproj`) |
|------|----------------------|-------------------------------|
| 헤더 포함 경로 | `$(SolutionDir)Engine` | `$(ProjectDir)..\include` |
| DLL 출력 경로 | `$(SolutionDir)x64\$(Configuration)\` | `$(ProjectDir)..\` (SDK 루트) |

DLL이 SDK 루트(`Engine.exe` 옆)에 직접 출력되므로 별도 복사 없이 즉시 실행·테스트할 수 있습니다.

### MakeSDK.ps1 실행 방법

```
[사전 요건]  Engine.sln 을 Release|x64 로 빌드하여 Engine.exe 를 생성해야 합니다.

[실행]
  PowerShell 에서 프로젝트 루트로 이동 후:
  .\MakeSDK.ps1

[실행 권한 오류 시]
  Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
  .\MakeSDK.ps1
```

스크립트가 수행하는 작업 순서:

| 단계 | 작업 |
|------|------|
| Step 0 | `Engine.exe` / `shaders.hlsl` / `GameLogic.cpp` 존재 여부 사전 검사 |
| Step 1 | 기존 `BoardEngine_SDK/` 삭제 후 폴더 구조 재생성 |
| Step 2 | `Engine.exe` 복사 |
| Step 3 | `shaders.hlsl` 복사 |
| Step 4 | `assets/` 폴더 전체 복사 |
| Step 5 | 공개 헤더 6종 + `nlohmann/json.hpp` → `include/` 복사 |
| Step 6 | `GameLogic.cpp` → `GameLogic/` 복사 |
| Step 7 | `GameLogic_SDK.vcxproj` 자동 생성 |
| Step 8 | `GameLogic_SDK.sln` 자동 생성 |
| Step 9 | `README_SDK.md` (SDK 퀵스타트 문서) 자동 생성 |

### 크리에이터 워크플로 (SDK 수령 후)

```
1. BoardEngine_SDK/GameLogic/GameLogic_SDK.sln 을 Visual Studio 2022 에서 엽니다.
2. GameLogic.cpp 를 수정합니다 (엔진 헤더는 include/ 에서 자동 참조).
3. Release|x64 로 빌드 → GameLogic.dll 이 Engine.exe 와 같은 폴더에 출력됩니다.
4. Engine.exe 를 실행 → GameLogic 메뉴 → Load GameLogic.dll → [ Play ] 테스트.
5. 배포 시: SDK 루트 폴더(Engine.exe + GameLogic.dll + shaders.hlsl + assets/) 를 압축해 전달.
```

---

## 프로젝트 구조

```
BOARD/
├── README.md                    ← 이 파일
├── MakeSDK.ps1                  ← SDK 자동 추출 스크립트
├── Tutorial_01_Gomoku.md        ← 오목 제작 튜토리얼 (한국어)
├── Tutorial_01_Gomoku_EN.md     ← Gomoku tutorial (English)
├── Tutorial_03_Multiplayer_P2P.md    ← P2P 멀티플레이어 튜토리얼 (한국어)
├── Tutorial_03_Multiplayer_P2P_EN.md ← P2P Multiplayer tutorial (English)
│
├── BoardEngine_SDK/             ← MakeSDK.ps1 실행 후 생성 (배포용)
│   ├── Engine.exe / shaders.hlsl / assets/
│   ├── include/                 ← 공개 헤더만 (엔진 소스 없음)
│   └── GameLogic/               ← 크리에이터 작업 영역
│
└── Engine/
    ├── Engine.sln               ← Visual Studio 솔루션
    ├── Engine_Manual.md         ← 엔진 내부 구조 상세 문서
    │
    ├── Engine/                  ← 🔒 Engine 프로젝트 (봉인 — 수정 금지)
    │   ├── Engine.h / .cpp
    │   ├── shaders.hlsl         ← MakeSDK.ps1 이 SDK 로 복사
    │   ├── assets/
    │   └── …
    │
    └── GameLogic/               ← ✅ 원본 게임 로직 (엔진 개발자용)
        └── GameLogic.cpp
```

---

## 🔒 절대 규칙 — 크리에이터를 위한 안내

> 새로운 보드게임을 만들 때는 **`GameLogic` 프로젝트 하나만** 수정합니다.
> 엔진 코어(`Engine/` 폴더 내부)는 **절대 건드리지 마세요.**

```
✅ 수정 가능 (크리에이터)   GameLogic\GameLogic.cpp
❌ 수정 금지 (읽기 전용)   include\*.h  (IEngineAPI.h, GameObject.h 등 전체)
❌ 수정 금지 (봉인)        Engine\Engine\Engine.h  /  .cpp  /  .hlsl  (코어 개발자 전용)
```

`include/` 헤더를 수정하면 다음 SDK 업데이트 시 변경 내용이 덮어씌워집니다.
체스·장기·쇼기·오목 등 어떤 게임이든 `GameLogic.cpp` 하나로 구현할 수 있도록
`IEngineAPI` 인터페이스가 설계되어 있습니다.
자세한 API 명세는 [`Engine_Manual.md`](Engine/Engine_Manual.md)를 참고하세요.

---

## 튜토리얼

> **참고:** 저장소 기본 `GameLogic/GameLogic.cpp` 에는 **완성된 오목**(승리·무승부 판정, 게임 종료 후 클릭 재대국)이 이미 구현되어 있습니다. 15×15 / Tile Size 40 보드를 만들어 `Load GameLogic.dll` → `[ Play ]` 로 바로 즐길 수 있습니다.
>
> **엔진으로 만든 예제 게임 모음:** [`Examples/`](BOARD/Examples/README.md) — 오목(`Gomoku_GameLogic.cpp`)과 체스(`Chess_GameLogic.cpp`)가 들어 있습니다. `GameLogic.cpp` 를 갈아끼우는 방법과 **만든 게임을 실행하는 법**을 Examples/README.md 에 정리해 두었습니다.
>
> **검증 체크리스트:** [`VERIFICATION.md`](BOARD/VERIFICATION.md) — 빌드·엔진 개선(RestartGame, Label)·오목·체스를 VS2022 에서 확인하는 단계별 목록입니다(체스 체크메이트 재현 시나리오 포함).

| 문서 | 내용 |
|------|------|
| [`Tutorial_01_Gomoku.md`](Tutorial_01_Gomoku.md) | 오목(五目) 2인 대전 게임 제작 (한국어) |
| [`Tutorial_01_Gomoku_EN.md`](Tutorial_01_Gomoku_EN.md) | Gomoku (Five in a Row) — Two-Player (English) |
| [`Tutorial_03_Multiplayer_P2P.md`](Tutorial_03_Multiplayer_P2P.md) | Winsock2 P2P 멀티플레이어 연동 (한국어) |
| [`Tutorial_03_Multiplayer_P2P_EN.md`](Tutorial_03_Multiplayer_P2P_EN.md) | Winsock2 P2P Multiplayer — Online Play (English) |

---

## 라이선스

이 프로젝트는 개인 학습 및 포트폴리오 목적으로 제작되었습니다.
