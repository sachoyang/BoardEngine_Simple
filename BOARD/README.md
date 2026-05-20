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

## 빠른 시작

```
1. Visual Studio 2022 에서 Engine/Engine.sln 을 엽니다.
2. 빌드 구성을 Debug 로 설정하고 F5 로 실행합니다.
3. 에디터가 실행되면 Board Generator 에서 보드를 생성하고
   GameLogic 메뉴에서 DLL 을 로드한 뒤 [ Play ] 를 누르세요.
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

## 프로젝트 구조

```
BOARD/
├── README.md                    ← 이 파일
├── Tutorial_01_Gomoku.md        ← 오목 제작 튜토리얼 (한국어)
├── Tutorial_01_Gomoku_EN.md     ← Gomoku tutorial (English)
│
└── Engine/
    ├── Engine.sln               ← Visual Studio 솔루션
    ├── Engine_Manual.md         ← 엔진 내부 구조 상세 문서
    │
    ├── Engine/                  ← 🔒 Engine 프로젝트 (봉인 — 수정 금지)
    │   ├── Engine.h / .cpp
    │   ├── shaders.hlsl         ← 이 파일을 배포 폴더에 복사하세요
    │   ├── assets/
    │   │   ├── tile.png
    │   │   └── scene.json       ← Save Scene 후 생성
    │   └── …
    │
    └── GameLogic/               ← ✅ 여기만 수정하세요
        └── GameLogic.cpp        ← 게임 로직 구현 파일
```

---

## 🔒 절대 규칙 — 크리에이터를 위한 안내

> 새로운 보드게임을 만들 때는 **`GameLogic` 프로젝트 하나만** 수정합니다.
> 엔진 코어(`Engine/` 폴더 내부)는 **절대 건드리지 마세요.**

```
✅ 수정 가능          GameLogic\GameLogic.cpp
❌ 수정 금지 (봉인)   Engine\Engine\Engine.h
❌ 수정 금지 (봉인)   Engine\Engine\Engine.cpp
❌ 수정 금지 (봉인)   Engine\Engine\*.h  /  *.cpp  /  *.hlsl  (전체)
```

엔진 코어를 변경하면 이미 작동하는 다른 게임들이 함께 망가질 수 있습니다.
체스·장기·쇼기·오목 등 어떤 게임이든 `GameLogic.cpp` 하나로 구현할 수 있도록
`IEngineAPI` 인터페이스가 설계되어 있습니다.
자세한 API 명세는 [`Engine_Manual.md`](Engine/Engine_Manual.md)를 참고하세요.

---

## 튜토리얼

| 문서 | 내용 |
|------|------|
| [`Tutorial_01_Gomoku.md`](Tutorial_01_Gomoku.md) | 오목(五目) 2인 대전 게임 제작 (한국어) |
| [`Tutorial_01_Gomoku_EN.md`](Tutorial_01_Gomoku_EN.md) | Gomoku (Five in a Row) — Two-Player (English) |

---

## 라이선스

이 프로젝트는 개인 학습 및 포트폴리오 목적으로 제작되었습니다.
