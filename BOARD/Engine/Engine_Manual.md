# Engine Manual — D3D12 보드게임 엔진 설명서

> 최종 업데이트: Session 21 (SetSpriteTexture / SetGameStatusText API 추가, 엔진 코어 완성)
>
> 이 문서는 엔진 구조가 변경될 때마다 자동으로 최신 상태를 유지합니다.
> 코드와 이 문서가 충돌하면 **코드가 진실**입니다.

---

## 목차

1. [엔진 개요](#1-엔진-개요)
2. [솔루션 구조 및 파일 맵](#2-솔루션-구조-및-파일-맵)
3. [핵심 아키텍처 — Engine.exe / GameLogic.dll 분리](#3-핵심-아키텍처--engineexe--gamelogicdll-분리)
4. [핫 리로드 시스템 (라이브 코딩)](#4-핫-리로드-시스템-라이브-코딩)
5. [컴포넌트 시스템 (ECS)](#5-컴포넌트-시스템-ecs)
6. [좌표계 및 렌더링 파이프라인](#6-좌표계-및-렌더링-파이프라인)
7. [리소스 관리 (ResourceManager)](#7-리소스-관리-resourcemanager)
8. [씬 저장/로드 시스템](#8-씬-저장로드-시스템)
9. [입력 시스템 (InputManager)](#9-입력-시스템-inputmanager)
10. [Lua 스크립팅 (ScriptComponent)](#10-lua-스크립팅-scriptcomponent)
11. [에디터 UI (Dear ImGui)](#11-에디터-ui-dear-imgui)
12. [Play Mode](#12-play-mode)
13. [IEngineAPI 스크립팅 API — 완성 명세](#13-iengineapi-스크립팅-api--완성-명세)
14. [현재 제한사항 및 확장 규칙](#14-현재-제한사항-및-확장-규칙)
15. [새 기능 추가 체크리스트](#15-새-기능-추가-체크리스트)
16. [배포(빌드) 방법 — STANDALONE_MODE](#16-배포빌드-방법--standalone_mode)
17. [보드게임 스크립트 프레임워크 (Tag & Team)](#17-보드게임-스크립트-프레임워크-tag--team)

---

## 1. 엔진 개요

이 엔진은 **Windows 전용 2D 보드게임 에디터 및 런타임**입니다.

| 항목 | 사용 기술 |
|------|-----------|
| 렌더링 API | DirectX 12 (D3D12) |
| 윈도우 관리 | Win32 API (WinAPI) |
| 에디터 UI | Dear ImGui (DX12 백엔드) |
| 게임 로직 스크립팅 | C++ Native DLL (`GameLogic.dll`) |
| 데이터 스크립팅 | Lua 5.4.7 via sol2 v3.3.0 |
| 씬 직렬화 | nlohmann/json v3.11.3 |
| 빌드 환경 | Visual Studio 2022, C++17, x64 |

### 설계 철학

- **에디터와 런타임의 경계 없음.** `Engine.exe`가 에디터이자 런타임입니다. ImGui로 씬을 편집하고, 같은 실행 파일이 게임 로직도 실행합니다.
- **라이브 코딩 우선.** `GameLogic.dll`을 핫 리로드하므로 엔진을 재시작하지 않고 게임 로직을 수정합니다.
- **단순한 ECS.** 무거운 엔티티 시스템 대신 `GameObject` + `Component` 폴리모피즘으로 구성합니다.

---

## 2. 솔루션 구조 및 파일 맵

```
Engine/                         ← 솔루션 루트
├── Engine.sln                  ← Visual Studio 솔루션
├── Engine_Manual.md            ← 이 파일
│
├── Engine/                     ← Engine 프로젝트 (Engine.exe)
│   ├── main.cpp                ← 진입점. Engine 생성 후 Initialize/Run
│   ├── Engine.h / .cpp         ← 엔진 핵심 클래스 (D3D12 초기화, 게임 루프, ImGui)
│   │
│   ├── Component.h             ← 컴포넌트 추상 기반 클래스
│   ├── GameObject.h / .cpp     ← 컴포넌트 컨테이너
│   ├── Transform.h / .cpp      ← 위치·크기·회전 컴포넌트
│   ├── SpriteRenderer.h / .cpp ← 텍스처 렌더 컴포넌트
│   ├── ScriptComponent.h / .cpp← Lua 스크립트 컴포넌트 (pimpl)
│   │
│   ├── ResourceManager.h / .cpp← 텍스처 로딩·캐싱·SRV 할당 싱글톤
│   ├── LuaManager.h / .cpp     ← sol2 sol::state 수명 관리 싱글톤
│   ├── InputManager.h / .cpp   ← 마우스 입력 싱글톤 (프레임별 델타)
│   ├── IGameLogic.h            ← DLL 공유 인터페이스 (순수 가상)
│   ├── IEngineAPI.h            ← 스크립팅 API 인터페이스 (Instantiate/Destroy/Find)
│   │
│   ├── shaders.hlsl            ← VSMain / PSMain (Position + UV)
│   ├── assets/                 ← 런타임 텍스처 폴더 (tile.png 등)
│   │   └── tile.png            ← 기본 타일 이미지
│   │
│   ├── d3dx12.h                ← Microsoft D3D12 헬퍼 헤더
│   ├── imgui/                  ← Dear ImGui 소스 (Win32 + DX12 백엔드)
│   ├── nlohmann/json.hpp       ← JSON 단일 헤더 (v3.11.3)
│   ├── sol/sol.hpp             ← sol2 아말가메이션 헤더 (v3.3.0)
│   ├── sol/config.hpp          ← sol2 별도 설정 헤더 (커스터마이즈 가능)
│   └── lua54/                  ← Lua 5.4.7 .c 소스 (CompileAsC 빌드)
│
└── GameLogic/                  ← GameLogic 프로젝트 (GameLogic.dll)
    ├── GameLogic.cpp           ← IGameLogic 구현 + extern "C" 팩토리
    └── GameLogic.vcxproj       ← DynamicLibrary, /MDd(/MD), OutDir=Engine/x64/...
```

### 런타임 파일 위치

빌드 후 실행 파일 디렉터리(`Engine\x64\Debug\` 또는 `Release\`)에 생성되는 파일들:

```
x64/Debug/
├── Engine.exe
├── GameLogic.dll       ← 원본 (VS 빌드가 여기에 출력)
└── GameLogic_temp.dll  ← 섀도우 카피 (엔진이 실제로 로드하는 파일)
```

작업 디렉터리는 **프로젝트 폴더** (`Engine\Engine\`)이므로, `shaders.hlsl`과 `assets/tile.png`는 해당 위치에 있어야 합니다.

---

## 3. 핵심 아키텍처 — Engine.exe / GameLogic.dll 분리

### 의존성 방향

```
Engine.exe  (IEngineAPI 구현)
  ├── IGameLogic.h        (DLL → Engine 방향 인터페이스)
  ├── IEngineAPI.h        (Engine → DLL 방향 API 주입)
  └── LoadLibrary("GameLogic_temp.dll")
        └── GameLogic.dll
              ├── #include "IGameLogic.h"
              ├── #include "IEngineAPI.h"   ← OnLoad 로 받아 m_api 에 저장
              ├── #include "GameObject.h"
              └── #include "Transform.h"
```

`Engine.exe`는 `GameLogic.dll`의 구현을 **컴파일 타임에 모릅니다.** 오직 `IGameLogic` 인터페이스와 `CreateGameLogicFn` 함수 포인터만 사용합니다.

`GameLogic.dll`은 `IEngineAPI*`를 통해 오브젝트 생성/삭제 등 엔진 핵심 기능을 역방향으로 호출합니다. 이것이 **양방향 통신**의 핵심입니다.

### IGameLogic 인터페이스

```cpp
// Engine\Engine\IGameLogic.h
class IGameLogic {
public:
    virtual ~IGameLogic() = default;
    // api: IEngineAPI 포인터 — DLL 이 Instantiate/Destroy 등을 호출하는 데 사용
    virtual void OnLoad  (std::vector<GameObject*>& objects, IEngineAPI* api) = 0;
    virtual void Update  (float deltaTime, std::vector<GameObject*>& objects) = 0;
    virtual void OnUnload() = 0;
    virtual void OnObjectClicked(GameObject* clickedObj) = 0;
};
using CreateGameLogicFn = IGameLogic* (*)();
```

### DLL 팩토리 패턴

```cpp
// GameLogic\GameLogic.cpp
extern "C" __declspec(dllexport) IGameLogic* CreateGameLogic() {
    return new GameLogic();  // 구체 구현 반환
}
```

`extern "C"`는 C++ 이름 맹글링을 막아 `GetProcAddress("CreateGameLogic")`으로 심볼을 찾을 수 있게 합니다.

### ABI 호환성 (중요)

Engine과 GameLogic은 반드시 **같은 CRT**를 사용해야 합니다.

| 빌드 구성 | Runtime Library | 설정 |
|-----------|----------------|------|
| Debug | `/MDd` | MultiThreadedDebugDLL |
| Release | `/MD` | MultiThreadedDLL |

이유: `std::vector<GameObject*>`를 DLL 경계로 넘기므로 같은 힙 할당자가 필요합니다. CRT가 다르면 크래시가 발생합니다.

---

## 4. 핫 리로드 시스템 (라이브 코딩)

### 문제: LNK1104 파일 잠금

`Engine.exe`가 `GameLogic.dll`을 직접 로드하면, VS 링커가 같은 파일에 빌드 결과를 쓰려 할 때 파일이 잠겨 있어 `LNK1104: cannot open file 'GameLogic.dll'` 오류가 발생합니다.

### 해결: 섀도우 카피 (Shadow Copy)

```
[빌드 시]   VS Linker  → GameLogic.dll  (원본, 잠금 없음)
[로드 시]   Engine.exe → CopyFile()     → GameLogic_temp.dll
                        LoadLibrary()  → GameLogic_temp.dll (이 파일만 잠김)
```

원본 `GameLogic.dll`은 Engine이 절대 직접 열지 않으므로 언제든 VS가 덮어쓸 수 있습니다.

### 핫 리로드 흐름

```
Render() 루프 (매 프레임)
  └── m_hotReloadTimer += dt
        └── [0.5초마다] CheckHotReload()
              └── fs::last_write_time("GameLogic.dll") != m_dllLastWriteTime?
                    └── YES → LoadGameLogicDLL("GameLogic.dll")
                                ├── UnloadGameLogicDLL()    // _temp.dll 잠금 해제
                                ├── CopyFileA(src → _temp)  // 새 버전 복사
                                ├── LoadLibraryA(_temp)     // 로드
                                ├── GetProcAddress → fn()         // 인스턴스 생성
                                ├── OnLoad(m_gameObjects, this)   // 초기화 + API 주입
                                └── 타임스탬프 기록
```

### 관련 Engine 멤버 (Engine.h)

```cpp
HMODULE     m_gameLogicModule  = nullptr;
IGameLogic* m_gameLogic        = nullptr;
std::string m_dllPath;                               // 원본 DLL 경로
std::filesystem::file_time_type m_dllLastWriteTime; // 마지막 로드 시점 타임스탬프
float       m_hotReloadTimer   = 0.0f;              // 주기 검사 누적 시간
std::string m_dllTimestampStr  = "Not loaded";      // ImGui 표시용 문자열
```

---

## 5. 컴포넌트 시스템 (ECS)

### 클래스 계층도

```
Component (추상 기반)
  ├── Transform         — 위치(x, y), 크기(width, height), 회전(rotation, radian)
  ├── SpriteRenderer    — 텍스처 핸들, visible 플래그
  └── ScriptComponent   — Lua 스크립트 파일 실행 (pimpl)
```

### Component 인터페이스

```cpp
class Component {
public:
    virtual ~Component() = default;
    virtual void OnAttach()  {}                   // AddComponent 직후 호출
    virtual void Update(float dt) {}              // 매 프레임 호출
    virtual void Render()    {}                   // 렌더링 단계 (현재 미사용)
    virtual void ImGuiRender() {}                 // Inspector에 속성 UI 표시
    virtual nlohmann::json Serialize()   const { return {}; }
    virtual void Deserialize(const nlohmann::json&) {}
    virtual const char* GetName() const = 0;      // 순수 가상 — 필수 구현
    GameObject* gameObject = nullptr;             // 소유 GameObject 역참조
};
```

### GameObject

```cpp
class GameObject {
public:
    std::string name;
    void AddComponent(Component* comp);  // 소유권 이전, gameObject 포인터 설정
    template<typename T> T* GetComponent() const;  // dynamic_cast 탐색
    const std::vector<Component*>& GetComponents() const;
};
```

`GameObject`의 소멸자가 모든 `Component*`를 `delete`합니다.

### 새 컴포넌트 추가 3단계 규칙

**1단계: 헤더 작성** (`MyComponent.h`)

```cpp
#pragma once
#include "Component.h"

class MyComponent : public Component {
public:
    // 필수
    const char* GetName() const override { return "MyComponent"; }

    // 필요한 것만 오버라이드
    void Update(float dt) override;
    void ImGuiRender() override;
    nlohmann::json Serialize() const override;
    void Deserialize(const nlohmann::json& j) override;
};
```

**2단계: `LoadScene` 분기 추가** (`Engine.cpp`)

```cpp
else if (type == "MyComponent") {
    auto* c = new MyComponent();
    obj->AddComponent(c);
    c->Deserialize(jComp);
}
```

**3단계: `Engine.vcxproj`에 파일 등록**

`.vcxproj`의 `<ItemGroup>`에 `<ClInclude>`와 `<ClCompile>` 항목을 추가합니다. Inspector UI는 `Component::ImGuiRender()`의 다형성 덕분에 **자동으로 표시**됩니다.

---

## 6. 좌표계 및 렌더링 파이프라인

### 2D Orthographic 픽셀 좌표계

```
(0, 0) ─────────────── X+ ──→
  │
  │      스크린 좌상단 원점
  Y+
  ↓
```

`Transform.x, y`는 **스프라이트 중심점** 픽셀 좌표입니다.

```
월드 (0,0)
  ┌────────────────────────────
  │     ┌──────────┐
  │     │          │
  │     │  ●(x,y)  │  ← Transform.x, Transform.y = 중심
  │     │          │
  │     └──────────┘
  │     ← width  →
```

모델 행렬:
```
Scale(width, height, 1) * RotateZ(rotation) * Translate(x, y, 0)
```

### 카메라 시스템

카메라 상태는 `Engine` 의 세 멤버로 표현합니다:

| 멤버 | 의미 | 기본값 |
|------|------|--------|
| `m_cameraX` | 뷰포트 좌상단의 **월드 X 좌표** | `0.0f` |
| `m_cameraY` | 뷰포트 좌상단의 **월드 Y 좌표** | `0.0f` |
| `m_cameraZoom` | 확대 배율 (1.0 = 100%, 2.0 = 2배 확대) | `1.0f` |

기본값 `(0, 0, 1.0)` 에서 월드 `[0, W] × [0, H]` 가 화면에 1:1 매핑됩니다 — 이전 단순 직교 투영과 동일.

#### VP 행렬 계산 (`UpdateCamera()`)

```cpp
// 뷰포트가 보여주는 월드 영역
// X: [cameraX, cameraX + width  / zoom]
// Y: [cameraY, cameraY + height / zoom]  (Y↓ 증가이므로 bottom > top)

XMMatrixOrthographicOffCenterLH(
    m_cameraX,              m_cameraX + W / zoom,  // left, right
    m_cameraY + H / zoom,   m_cameraY,             // bottom, top
    0.0f, 1.0f
);
```

`UpdateCamera()` 는 매 프레임 Update 블록 끝에서 호출되어 `m_viewProj` 를 갱신합니다.

#### 화면 → 월드 역변환 (`ScreenToWorld()`)

```cpp
// 화면 픽셀 좌표 (sx, sy) → 월드 좌표 (wx, wy)
wx = m_cameraX + sx / m_cameraZoom;
wy = m_cameraY + sy / m_cameraZoom;
```

마우스 피킹, 드래그, Snap to Grid 모두 이 역변환을 사용하므로 카메라 이동/줌과 무관하게 정확합니다.

#### 조작법

| 동작 | 효과 |
|------|------|
| **우클릭 드래그** | 카메라 패닝 (cameraX/Y 이동) |
| **마우스 휠** | 줌 (마우스 커서 위치 기준, 범위 0.1× ~ 5.0×) |
| 코드에서 `m_cameraX = m_cameraY = 0; m_cameraZoom = 1` | (0, 0, 1.0) 으로 초기화 |

#### 줌 중심점 수식

마우스 커서 아래의 월드 좌표가 줌 전후에 고정되도록 카메라 위치를 재조정합니다:

```cpp
// 1. 마우스 아래의 월드 좌표 보존
float wx = m_cameraX + mouseX / oldZoom;
float wy = m_cameraY + mouseY / oldZoom;

// 2. 줌 적용
float newZoom = clamp(oldZoom * factor, 0.1f, 5.0f);

// 3. 카메라 재조정 (mx/newZoom 위치가 wx 와 일치하도록)
m_cameraX = wx - mouseX / newZoom;
m_cameraY = wy - mouseY / newZoom;
```

### 타일맵 그리드 정렬 수식

타일 크기 `ts`로 틈 없이 배치하려면:

```
tr->x = col * ts + ts * 0.5f;   // 중심 = 왼쪽 모서리 + 반 타일
tr->y = row * ts + ts * 0.5f;
tr->width  = ts;
tr->height = ts;
```

### Z-order (그리기 순서)

`m_gameObjects` 배열 **순서 = 그리기 순서**입니다. 인덱스가 클수록 위에 그려집니다.

피킹(마우스 클릭)은 **역순 순회**로 제일 위에 있는 오브젝트를 우선 선택합니다.

### 렌더링 루프 단계

```
Render() 호출 순서
  [1] CommandAllocator / CommandList Reset
  [2] Update (델타 타임, Component::Update, GameLogic::Update, 핫 리로드 타이머)
      ├── 좌클릭: 피킹 & 드래그
      ├── 우클릭 드래그: 카메라 패닝
      ├── 마우스 휠: 카메라 줌
      ├── UpdateCamera() → m_viewProj 재계산
      └── Deferred Destroy: m_pendingDestroy 플러시 (일괄 delete)
  [3] ImGui NewFrame + UI 빌드 (CPU only)
  [4] ResourceBarrier: PRESENT → RENDER_TARGET
  [5] ClearRenderTargetView (배경색)
  [6] 오브젝트별 Draw (SetGraphicsRootConstantBufferView + DrawIndexedInstanced)
  [7] ImGui_ImplDX12_RenderDrawData
  [8] ResourceBarrier: RENDER_TARGET → PRESENT
  [9] CommandList Close → ExecuteCommandLists → Present
  [10] WaitForPreviousFrame (Fence 동기화)
```

### 정점 레이아웃

셰이더에 전달되는 정점 구조체:
```cpp
struct Vertex {
    XMFLOAT3 pos;  // POSITION (12 bytes)
    XMFLOAT2 uv;   // TEXCOORD (8 bytes)
};
```

단위 사각형 4정점 (UV 포함), 인덱스 `{0,1,2, 0,2,3}`으로 삼각형 2개.

정점 Y 부호: model Y=-0.5 = 화면 위, Y=+0.5 = 화면 아래 (픽셀 좌표계 Y↓ 방향과 일치).

### Constant Buffer 레이아웃

오브젝트별 데이터는 256바이트 정렬 슬롯으로 관리됩니다:
```cpp
struct ObjectCB {
    XMFLOAT4X4 mvp;    // 64 bytes — MVP 변환 행렬
    XMFLOAT4   color;  // 16 bytes — SpriteRenderer::colorTint
    BYTE _pad[176];    // 패딩 → 합계 256 bytes (D3D12 CBV 정렬 요건)
};
```

셰이더 cbuffer 레이아웃 (`shaders.hlsl`):
```hlsl
cbuffer ObjectCB : register(b0)
{
    float4x4 mvp;    // 64 bytes
    float4   color;  // 16 bytes (tint 곱셈용)
};
```

### 행렬 전달 규칙 (중요 — 흔한 실수)

DirectXMath(행 우선, row-major)와 HLSL cbuffer(열 우선, column-major)의 관계:

- HLSL `cbuffer`의 `float4x4`는 기본적으로 **열 우선(column-major)** 으로 메모리를 읽는다.
- DM 행 우선 행렬을 그대로 GPU에 전달하면, HLSL이 읽을 때 자동으로 **전치(transpose)** 된 행렬로 해석한다.
- 셰이더가 `mul(M, v)` (열벡터 방식) 을 사용하는 경우:
  - CPU: **`XMMatrixTranspose` 호출 없이** MVP를 그대로 전달.
  - HLSL: `mul(mvp, pos)` → HLSL이 읽은 `mvp = DM_MVP^T` → `DM_MVP^T * v` = 올바른 변환.
- 만약 `XMMatrixTranspose`를 잘못 적용하면 이중 전치(DM → 전치 → HLSL이 다시 전치 = 원본)가 되어 이동값이 W 성분에 적용되고 **타일이 방사형으로 찢어지는 왜곡**이 발생한다.

---

## 7. 리소스 관리 (ResourceManager)

싱글톤 `ResourceManager`가 모든 텍스처의 수명을 소유합니다.

### 절대 경로 변환 규칙

VS 디버그 실행 시 작업 디렉터리(Working Directory)가 프로젝트 소스 폴더로 설정되어  
상대 경로(`"assets/tile.png"`)가 올바른 위치를 가리키지 않는 문제를 방지합니다.

**`Engine::GetAbsolutePath(relativePath)`** — `Engine` 전용 private 헬퍼.  
`GetModuleFileNameA`로 구한 exe 디렉터리에 `relativePath`를 결합해 절대 경로를 반환합니다.

```cpp
// Engine.cpp 내 모든 파일 I/O 는 이 함수를 거친다
std::string absPath = GetAbsolutePath("assets/click.wav");
// → "C:\...\x64\Debug\assets\click.wav"
```

**`ResourceManager::SetBasePath(dir)`** — `Engine::InitD3D12`에서 1회 주입.  
이후 `LoadTexture`에 상대 경로가 들어오면 내부에서 자동으로 절대 경로로 변환합니다.  
이미 절대 경로이면 그대로 사용합니다 (중복 변환 없음).

```
초기화 흐름
  InitD3D12()
    ├── ResourceManager::Init(device, commandQueue)
    └── ResourceManager::SetBasePath("C:\...\x64\Debug")  ← GetModuleFileNameA 사용

런타임 흐름
  SpriteRenderer::SetTexture("assets/tile.png")
    └── ResourceManager::LoadTexture("assets/tile.png")
          └── absPath = basePath / "assets/tile.png"  ← 자동 변환
                └── stbi_load(absPath)
```

### 주요 API

```cpp
// Engine::InitD3D12 에서 1회 호출
void SetBasePath(const std::string& basePath);

// 텍스처 로드 (캐시 히트 → 기존 반환, 미스 → D3D12 업로드 후 SRV 등록)
// 상대 경로 → SetBasePath 기준 절대 경로 자동 변환
const TextureHandle* LoadTexture(const std::string& path);

// ImGui SRV 콜백용 (Engine.cpp의 static 함수에서 호출)
void AllocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE*, D3D12_GPU_DESCRIPTOR_HANDLE*);
void FreeDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE);
```

### 폴백 텍스처

`LoadTexture`에 전달한 파일이 없으면, 회색/흰색 체커보드 패턴 텍스처를 자동으로 생성하고 같은 경로 키로 캐싱합니다. 덕분에 `assets/tile.png`가 없어도 렌더링 테스트가 가능합니다.

### SRV Heap

```
총 슬롯 수: MAX_SRV_DESCRIPTORS = 64
  ├── 슬롯 0: ImGui 폰트 아틀라스
  ├── 슬롯 1: tile.png (예시)
  ├── 슬롯 2: ...
  └── 슬롯 63: 최대
```

---

## 8. 씬 저장/로드 시스템

### 저장 형식 (scene.json 예시)

```json
{
    "gameObjects": [
        {
            "name": "Tile_0_0",
            "components": [
                {
                    "type": "Transform",
                    "x": 32.0, "y": 32.0,
                    "width": 64.0, "height": 64.0,
                    "rotation": 0.0
                },
                {
                    "type": "SpriteRenderer",
                    "visible": true,
                    "texture": "assets/tile.png"
                }
            ]
        }
    ]
}
```

### SaveScene 흐름

```
SaveScene(path)
  ├── GetAbsolutePath(path) → 절대 경로 변환
  └── 모든 m_gameObjects 순회
        └── 각 Component::Serialize() 호출
              └── JSON 배열에 추가
                    └── ofstream(절대경로)으로 파일 저장 (들여쓰기 4칸)
```

`Serialize()`가 빈 JSON `{}`를 반환하는 컴포넌트는 저장에서 자동 제외됩니다 (ScriptComponent 등).

### LoadScene 흐름 및 메모리 안전 순서

```
LoadScene(path)
  [0] GetAbsolutePath(path) → 절대 경로 변환
  [1] 파일 열기 → JSON 파싱 (실패 시 현재 씬 유지, 조기 반환)
  [2] 기존 m_gameObjects 전체 delete → clear()  ← 반드시 먼저
  [3] m_selectedObjectIdx = 0, m_isDragging = false (UI 상태 초기화)
  [4] JSON 순회 → new GameObject → 컴포넌트 "type" 분기 → Deserialize
  [5] AddGameObject()
```

**주의:** `[2]` 단계에서 GPU가 해당 리소스를 참조하지 않아야 합니다. `LoadScene`은 `WaitForPreviousFrame()` 직후나 ImGui 버튼 콜백에서 호출되므로 안전합니다. 텍스처 리소스 자체는 `ResourceManager`가 소유하므로 `SpriteRenderer` 삭제 시 해제되지 않습니다.

---

## 9. 입력 시스템 (InputManager)

싱글톤 `InputManager`가 Win32 메시지에서 마우스 상태를 수집합니다.

### 주요 API

```cpp
// WindowProc에서 호출
void OnLButtonDown (int x, int y);
void OnLButtonUp   (int x, int y);
void OnRButtonDown (int x, int y);   // 우클릭 → 카메라 패닝
void OnRButtonUp   (int x, int y);
void OnMouseMove   (int x, int y);
void OnMouseWheel  (int delta);      // WM_MOUSEWHEEL, 단위 WHEEL_DELTA(120)

// Render() 끝에서 호출 (프레임 클리어)
void EndFrame();

// 쿼리
int  GetMouseX() / GetMouseY();    // 현재 마우스 픽셀 위치
int  GetDeltaX() / GetDeltaY();    // 이번 프레임 누적 이동 픽셀
int  GetWheelDelta();              // 이번 프레임 누적 휠 델타
bool IsLButtonPressed();           // 이번 프레임에 막 눌린 경우
bool IsLButtonDown();              // 현재 누른 상태
bool IsRButtonDown();              // 우클릭 현재 누른 상태
```

### Snap to Grid

드래그 중 타일이 격자 셀 중심에 정렬되도록 하는 기능입니다.

| 멤버 | 타입 | 기본값 | 설명 |
|------|------|--------|------|
| `m_snapToGrid` | `bool` | `true` | 스냅 활성화 여부 |
| `m_snapSize` | `int` | `64` | 격자 한 칸의 크기(픽셀) |

Board Generator 창의 **Snap to Grid 체크박스**와 **Snap Size 입력 필드**로 런타임 조절이 가능합니다.

**정렬 수식 (`DragSelectedObject` 내부):**
```cpp
float half = m_snapSize * 0.5f;
tr->x = roundf((tr->x - half) / m_snapSize) * m_snapSize + half;
tr->y = roundf((tr->y - half) / m_snapSize) * m_snapSize + half;
```

격자 셀 중심 좌표는 `k * snapSize + snapSize/2` (k = 0, 1, 2, …)이며, Board Generator가 타일을 배치할 때 사용하는 수식 `col * ts + ts/2`와 정확히 일치합니다. 따라서 드래그로 이동한 타일이 Board Generator가 생성한 타일과 같은 격자에 딱 맞물립니다.

### 마우스 호버 하이라이트

마우스가 올라간 오브젝트를 시각적으로 밝게 표시합니다. Play/Edit 모드 모두 동작합니다.

**동작 흐름 (매 프레임, Render() 내 Update 블록):**

```
[1] 모든 SpriteRenderer::isHovered = false  (항상 초기화)
[2] WantCaptureMouse == false 일 때만:
      ScreenToWorld(mouseX, mouseY) → 월드 좌표 (hx, hy)
      m_gameObjects 역순 순회 (최상위 오브젝트 우선)
        AABB 충돌 검사: hx/hy 가 tr 영역 안이면
          → sr->isHovered = true, break
```

**렌더링 시 색상 보정 (CPU 단, 셰이더 변경 없음):**

```cpp
XMFLOAT4 tint = sr->colorTint;
if (sr->isHovered)
{
    // 원본 60% + 연한 푸른색(R:0.3, G:0.6, B:1.0) 40% 블렌딩
    tint.x = std::min(tint.x * 0.6f + 0.4f * 0.3f, 1.0f);
    tint.y = std::min(tint.y * 0.6f + 0.4f * 0.6f, 1.0f);
    tint.z = std::min(tint.z * 0.6f + 0.4f * 1.0f, 1.0f);
}
memcpy(pSlot + sizeof(XMFLOAT4X4), &tint, sizeof(XMFLOAT4));
```

`isHovered`는 `SpriteRenderer`의 멤버이며 매 프레임 Engine 이 갱신합니다.  
`colorTint` 원본은 수정하지 않으므로 GameLogic 에서 설정한 색상(검정/흰색 틴트 등)은 보존됩니다.

| 항목 | 내용 |
|------|------|
| `SpriteRenderer::isHovered` | `bool`, 기본값 `false`. Engine 이 매 프레임 쓰기 |
| 색상 보정 | RGB +0.3, 1.0 클램프. A(불투명도) 무변경 |
| ImGui 패널 위 | `WantCaptureMouse = true` → isHovered 감지 skip |
| 모드 | Play/Edit 모두 적용 |

### 주의 사항

`ImGui::GetIO().WantCaptureMouse`가 `true`이면 ImGui가 마우스를 소비 중이므로, 씬 피킹/드래그를 실행하면 안 됩니다. `Engine.cpp`의 Update 블록에서 이 가드가 적용됩니다.

---

## 10. Lua 스크립팅 (ScriptComponent)

### 아키텍처

```
ScriptComponent (pimpl)
  └── Impl { sol::protected_function onUpdate; }
        └── sol2 v3.3.0 (Engine\Engine\sol\sol.hpp)
              └── Lua 5.4.7 (Engine\Engine\lua54\*.c)
```

`ScriptComponent.h`에는 sol2 헤더가 포함되지 않습니다. pimpl 패턴으로 컴파일 비용(`sol.hpp` = 29000줄)을 격리합니다.

### Lua 스크립트 인터페이스

```lua
-- 스크립트에서 자동으로 주입되는 전역 변수
-- gameObject: GameObject 래퍼
-- transform: Transform 컴포넌트 래퍼

function OnUpdate(dt)
    transform.x = transform.x + 100.0 * dt
end
```

### LuaManager에 바인딩된 타입

| Lua 타입 | C++ 타입 | 접근 가능 속성 |
|----------|----------|---------------|
| Transform | Transform | x, y, width, height, rotation, SetPosition(x,y) |
| GameObject | GameObject | name |

---

## 11. 에디터 UI (Dear ImGui)

### 창 구성

| ImGui 창 | 역할 |
|----------|------|
| 메인 메뉴바 | File / GameLogic / GameObject 메뉴 + Play/Stop 버튼 |
| **Hierarchy** | 씬 오브젝트 목록. 클릭 시 Inspector 선택 연동 |
| **Inspector** | 선택된 오브젝트의 Name·Tag·Team ID·Transform·SpriteRenderer 속성 편집 |
| **Project (Assets)** | `assets/` 폴더 내 파일 목록 열람 (에셋 브라우저) |
| Board Generator | 그리드 타일맵 자동 생성 + **Snap to Grid** 설정 |

### Inspector 편집 필드

| 필드 | 위젯 | 설명 |
|------|------|------|
| Name | InputText | 오브젝트 이름 실시간 수정 |
| Tag | InputText | 기물 종류 식별자 (`"Pawn"`, `"Knight"` 등) |
| Team ID | InputInt | 팀 소속 (0=중립, 1=Player1, 2=Player2) |
| Position | DragFloat2 | Transform X/Y 좌표 드래그 조작 |
| Visible | Checkbox | SpriteRenderer 렌더링 토글 |
| Texture | Text | 현재 바인딩된 텍스처 경로 표시 |

### 메인 메뉴바 항목

| 메뉴 | 항목 | 기능 |
|------|------|------|
| File | Save Scene | `scene.json` 저장 |
| File | Load Scene | `scene.json` 로드 |
| GameLogic | Load GameLogic.dll | DLL 수동 로드 |
| GameLogic | Reload Now | 강제 핫 리로드 |
| GameLogic | Unload GameLogic.dll | DLL 언로드 |
| GameObject | Create Tile | 단일 타일 오브젝트 생성 |

### ImGui DX12 백엔드 초기화

ImGui SRV 슬롯 할당을 `ResourceManager`에 위임하는 콜백 방식을 사용합니다:
```cpp
initInfo.SrvDescriptorAllocFn = ImGuiSrvAllocFn;  // ResourceManager::AllocateDescriptor
initInfo.SrvDescriptorFreeFn  = ImGuiSrvFreeFn;   // ResourceManager::FreeDescriptor
```

---

## 12. Play Mode

### 개념

엔진은 두 가지 모드를 갖습니다:

| 모드 | `m_isPlaying` | 좌클릭 동작 | 드래그 | 카메라 패닝·줌 |
|------|--------------|-------------|--------|---------------|
| **Edit Mode** | `false` | 오브젝트 선택 + 드래그 | 가능 | 가능 |
| **Play Mode** | `true` | `OnObjectClicked` 이벤트 전달 | 불가 | 가능 |

### 전환 방법

메인 메뉴바 오른쪽의 `[ Play ]` / `[ Stop ]` 버튼으로 토글합니다.

### 클릭 이벤트 흐름

```
Play Mode + 좌클릭
  └── TryPickObject()        — 피킹 수행 (m_isDragging 세팅)
        └── 성공(m_isDragging == true)
              └── m_gameLogic->OnObjectClicked(clickedGameObject)
                    └── DLL 에서 타일 colorTint 변경 등 게임 로직 실행
              └── m_isDragging = false  (Play Mode 에서는 드래그 안 함)
```

### IGameLogic 인터페이스 (클릭 이벤트 포함 전체)

```cpp
class IGameLogic {
    virtual void OnLoad(std::vector<GameObject*>& objects, IEngineAPI* api) = 0;
    virtual void Update(float dt, std::vector<GameObject*>& objects)        = 0;
    virtual void OnUnload()                                                  = 0;
    virtual void OnObjectClicked(GameObject* clickedObj)                     = 0;
};
```

### colorTint 연동

`SpriteRenderer::colorTint`는 DLL 경계를 넘어 직접 접근 가능한 퍼블릭 멤버입니다.  
`OnObjectClicked` 안에서 `obj->GetComponent<SpriteRenderer>()->colorTint` 를 수정하면 즉시 다음 프레임에 반영됩니다.

```cpp
// 예시: 2인 턴제 착색
void OnObjectClicked(GameObject* obj) override
{
    auto* sr = obj->GetComponent<SpriteRenderer>();
    if (!sr) return;
    sr->colorTint = (m_currentTurn == 1)
        ? XMFLOAT4{1,0,0,1}   // Player 1 — 빨강
        : XMFLOAT4{0,0,1,1};  // Player 2 — 파랑
    m_currentTurn = (m_currentTurn == 1) ? 2 : 1;
}
```

### 핫 리로드 호환성

- `colorTint`는 엔진 씬 오브젝트(`SpriteRenderer`)에 저장되므로 DLL 리로드 후에도 유지됩니다.
- `m_currentTurn` 등 DLL 내부 상태는 `OnLoad`가 재호출되며 초기화됩니다.

---

## 13. IEngineAPI 스크립팅 API — 완성 명세

> **엔진 코어가 봉인된 최종 API입니다.** GameLogic.cpp 에서 이 인터페이스만 사용하면  
> 체스·장기·쇼기·오목 등 모든 보드게임을 구현할 수 있습니다.

`Engine` 클래스가 `IEngineAPI`를 상속·구현하며, `OnLoad` 시 `this` 포인터로 DLL에 주입됩니다.

### 인터페이스 전체 (IEngineAPI.h)

```cpp
class IEngineAPI {
public:
    virtual ~IEngineAPI() = default;

    // ── 오브젝트 생성 ────────────────────────────────────────────────────
    // 씬에 새 GameObject 를 생성하고 반환한다. Transform 이 자동 추가된다.
    // 반환값: 생성된 GameObject*. MAX_OBJECTS(256) 초과 시 nullptr.
    virtual GameObject* Instantiate(const std::string& name, float x, float y) = 0;

    // 오브젝트에 SpriteRenderer 를 추가하고 텍스처를 설정한다.
    // Instantiate 직후 호출해야 렌더링이 활성화된다.
    virtual void AddSpriteRenderer(GameObject* obj, const std::string& texturePath) = 0;

    // ── 오브젝트 제거 ────────────────────────────────────────────────────
    // 프레임 끝(Deferred Destroy)에 안전하게 삭제한다.
    // Update / OnObjectClicked 순회 중 호출해도 크래시가 발생하지 않는다.
    virtual void Destroy(GameObject* obj) = 0;

    // ── 오브젝트 검색 ────────────────────────────────────────────────────
    // 이름이 일치하는 첫 번째 오브젝트를 반환한다. 없으면 nullptr.
    virtual GameObject* FindObjectByName(const std::string& name) = 0;

    // ── 오디오 ──────────────────────────────────────────────────────────
    // .wav 파일을 비동기 재생한다 (SND_ASYNC | SND_FILENAME).
    // 새 호출이 이전 재생을 중단시킨다 — 채널은 1개, 턴제 게임에 적합.
    // 상대 경로("assets/click.wav")는 자동으로 exe 기준 절대 경로로 변환된다.
    // 파일이 없으면 MessageBoxA 경고를 표시하고 조용히 반환한다.
    virtual void PlayAudio(const std::string& filePath) = 0;

    // ── 텍스처 실시간 교체 ───────────────────────────────────────────────
    // 오브젝트의 SpriteRenderer 텍스처를 즉시 교체한다.
    // 사용 예: 폰 승급(Pawn→Queen), 쇼기 뒤집기, 체크 하이라이트.
    // texturePath: 상대 경로("assets/queen.png") 또는 절대 경로 모두 허용.
    // SpriteRenderer 가 없는 오브젝트에 호출하면 아무 동작도 하지 않는다.
    virtual void SetSpriteTexture(GameObject* obj, const std::string& texturePath) = 0;

    // ── 인게임 UI 텍스트 ─────────────────────────────────────────────────
    // 화면 상단 중앙에 1.8× 크기 텍스트를 duration 초 동안 표시한다.
    // 사용 예: "Player 1의 턴", "체크메이트!", "무효한 이동".
    // 새 호출은 이전 텍스트와 타이머를 즉시 교체한다.
    // duration <= 0 이면 텍스트를 즉시 지운다.
    // STANDALONE_MODE 와 에디터 모드 모두에서 동작한다.
    virtual void SetGameStatusText(const std::string& text, float duration = 3.0f) = 0;
};
```

### API 사용 요약표

| 함수 | 반환값 | 주요 용도 |
|------|--------|-----------|
| `Instantiate(name, x, y)` | `GameObject*` (null 가능) | 기물 생성 |
| `AddSpriteRenderer(obj, path)` | `void` | 생성 직후 텍스처 부착 |
| `Destroy(obj)` | `void` | 기물 제거 (잡기, 게임 종료) |
| `FindObjectByName(name)` | `GameObject*` (null 가능) | 특정 기물/타일 검색 |
| `PlayAudio(path)` | `void` | 이동/공격/승급 효과음 |
| `SetSpriteTexture(obj, path)` | `void` | 승급·뒤집기 스프라이트 교체 |
| `SetGameStatusText(text, sec)` | `void` | 턴 안내·경고 표시 |

### 실전 코드 예시

**기물 승급 (체스 폰 → 퀸)**

```cpp
// OnObjectClicked 또는 Update 내부에서 호출
if (isPawnAtLastRank(piece))
{
    m_api->SetSpriteTexture(piece, "assets/queen.png");
    piece->tag = "Queen";
    m_api->SetGameStatusText("Pawn promoted to Queen!", 2.5f);
    m_api->PlayAudio("assets/promote.wav");
}
```

**쇼기 뒤집기 (앞면↔뒷면)**

```cpp
if (piece->tag == "Lance")
{
    bool promoted = (piece->teamID == 1);
    m_api->SetSpriteTexture(piece,
        promoted ? "assets/lance_back.png" : "assets/lance_front.png");
    m_api->SetGameStatusText(promoted ? "成香!" : "香車", 1.5f);
}
```

**턴 안내 텍스트**

```cpp
void EndTurn()
{
    m_currentTurn = (m_currentTurn == 1) ? 2 : 1;
    m_api->SetGameStatusText(
        "Player " + std::to_string(m_currentTurn) + "'s Turn", 2.0f);
}
```

**Deferred Destroy 메커니즘**

`Destroy(obj)` 를 호출하면 즉시 삭제되지 않고 `m_pendingDestroy` 큐에 추가됩니다.

```
Render() Update 블록
  ├── GameLogic::Update()        ← Destroy(obj) 호출 → m_pendingDestroy 에 추가
  ├── 입력 처리 (클릭, 드래그)
  ├── UpdateCamera()
  └── [Deferred Destroy Flush]   ← m_pendingDestroy 순회 → erase + delete
```

### 주의 사항

| 항목 | 규칙 |
|------|------|
| `new SpriteRenderer()` | DLL 에서 직접 생성 불가 — 반드시 `AddSpriteRenderer()` 사용 |
| `Instantiate` 반환값 | `nullptr` 체크 필수 (MAX_OBJECTS=256 초과 시 반환) |
| `SetSpriteTexture` 순서 | `AddSpriteRenderer` 가 먼저 호출된 오브젝트에만 사용 가능 |
| `PlayAudio` 채널 | `.wav`만 지원. 동시에 하나의 소리만 재생 — 새 호출이 이전 재생을 중단 |
| `SetGameStatusText` 위치 | 화면 상단 중앙 고정. 위치나 폰트는 Engine.cpp 의 오버레이 블록에서 변경 |
| `FindObjectByName` 중복 | 이름 충돌 시 첫 번째 결과만 반환 — 중요 오브젝트는 고유 이름 필수 |

### 보드 외부 오브젝트 피킹 보장

쇼기처럼 잡은 말 창고(Hand)가 보드 밖에 위치해도 **완벽히 동작합니다.**

```
ScreenToWorld(sx, sy) → world = cameraOrigin + pixel / zoom
```

이 변환에는 보드 경계 필터가 없습니다. `TryPickObject` 역시 `m_gameObjects` 전체를  
역순 순회하므로, 월드 좌표 어디에 있든 AABB 안이면 `OnObjectClicked` 로 전달됩니다.

---

## 14. 현재 제한사항 및 확장 규칙

### MAX_OBJECTS = 256 제한

**원인.** Constant Buffer는 `sizeof(ObjectCB) * MAX_OBJECTS = 256 * 256 = 65,536 bytes`로 고정 할당됩니다. GPU는 이 크기를 생성 시점에 확정하므로 동적 리사이즈가 불가능합니다.

**현재 제한.**
- 씬에 동시에 존재할 수 있는 오브젝트 수 = 256개
- 기본 보드 크기 8×8 = 64 타일 + 최대 64 기물 → 여유 있음

**확장 방법.**

`Engine.h`에서 상수를 변경하면 됩니다:
```cpp
static constexpr UINT MAX_OBJECTS = 256;  // 예시: 256으로 확장
```
`CreateConstantBuffer()`는 `sizeof(ObjectCB) * MAX_OBJECTS`를 자동으로 사용하므로 다른 코드 수정은 불필요합니다.

Board Generator의 경고 텍스트도 자동으로 새 한도를 참조합니다.

### 단일 CommandAllocator / 싱글 스레드 렌더링

현재 GPU 명령 기록이 단일 스레드에서 진행됩니다. 멀티 스레드 렌더링이 필요하면 `CommandAllocator`를 프레임 수 또는 스레드 수만큼 늘려야 합니다.

### 깊이 버퍼 없음

2D 전용이므로 DSV(Depth Stencil View)가 없습니다. Z-order는 배열 순서만으로 결정됩니다.

### 창 크기 변경 (WM_SIZE)

`OnResize()`가 SwapChain 버퍼와 RTV를 재생성합니다. 최소화(`SIZE_MINIMIZED`) 시에는 무시됩니다.

---

## 15. 새 기능 추가 체크리스트

새로운 시스템이나 컴포넌트를 추가할 때 이 체크리스트를 따릅니다.

### 새 컴포넌트 추가 시

- [ ] `MyComponent.h` / `.cpp` 작성 (`Component` 상속, `GetName()` 구현)
- [ ] `Engine.vcxproj`에 `<ClInclude>` / `<ClCompile>` 항목 추가
- [ ] `Engine.cpp`의 `LoadScene()` 분기에 `"type" == "MyComponent"` 추가
- [ ] `Serialize()` / `Deserialize()`에 `"type"` 필드 포함
- [ ] 이 문서의 **컴포넌트 시스템** 섹션 및 바인딩 표 업데이트

### 새 ImGui 창/메뉴 추가 시

- [ ] `Render()` ImGui 블록 안에 `ImGui::Begin` / `ImGui::End` 쌍 추가
- [ ] 이 문서의 **에디터 UI** 섹션 창 구성 표 업데이트

### MAX_OBJECTS 변경 시

- [ ] `Engine.h`의 `MAX_OBJECTS` 상수 수정
- [ ] 이 문서의 **제한사항** 섹션 수치 업데이트

### 새 싱글톤 시스템 (예: SoundManager) 추가 시

- [ ] `Init()` → `Engine::InitD3D12()` 또는 `Initialize()` 끝에서 호출
- [ ] `Shutdown()` → `Engine::~Engine()` 소멸자 해제 순서에 삽입
- [ ] 이 문서의 **솔루션 구조** 파일 맵 및 해당 섹션 추가

---

---

## 16. 배포(빌드) 방법 — STANDALONE_MODE

이 엔진은 **에디터 모드(기본)**와 **배포용 스탠드얼론 모드**를 하나의 코드베이스로 지원합니다.

### STANDALONE_MODE 매크로

`Engine\Engine\Engine.h` 최상단에 위치합니다.

```cpp
// 0 = 에디터 모드 (기본). ImGui 창 표시, 마우스 피킹·드래그 활성.
// 1 = 배포용 스탠드얼론 모드. ImGui 창 숨김, 씬·DLL 자동 로드, 게임 즉시 시작.
#define STANDALONE_MODE 0
```

### 모드 비교

| 항목 | 에디터 모드 (0) | 스탠드얼론 모드 (1) |
|------|----------------|---------------------|
| ImGui 메뉴바 | 표시 | 숨김 |
| Hierarchy 창 | 표시 | 숨김 |
| Inspector 창 | 표시 | 숨김 |
| Project (Assets) 창 | 표시 | 숨김 |
| Board Generator 창 | 표시 | 숨김 |
| 씬 로드 | 수동 (File → Load Scene) | 자동 (`assets/scene.json`) |
| DLL 로드 | 수동 (GameLogic 메뉴) | 자동 (`GameLogic.dll`) |
| 플레이 모드 | 수동 토글 | 즉시 활성 (`m_isPlaying = true`) |

### 게임 배포판 만들기 — 단계별 절차

**1단계: 씬 파일 저장 (에디터 모드에서)**

1. `STANDALONE_MODE 0` 상태로 빌드 후 실행합니다.
2. Board Generator 로 8×8 보드를 생성합니다.
3. 메뉴바 **File → Save Scene** 으로 씬을 저장합니다.
   - 저장 경로: `<exe폴더>\assets\scene.json`
   - `assets/` 디렉터리가 없으면 미리 생성해야 합니다.

**2단계: 스탠드얼론 모드로 빌드**

1. `Engine.h` 에서 매크로를 변경합니다:
   ```cpp
   #define STANDALONE_MODE 1
   ```
2. Visual Studio 빌드 구성을 **Release** 로 변경합니다.
3. **빌드 → 솔루션 빌드** (Ctrl+Shift+B).
4. 출력 경로 `Engine\x64\Release\` 에 생성된 파일:
   - `Engine.exe` — 에디터 UI 없는 순수 게임 실행 파일
   - `GameLogic.dll` — 게임 로직 DLL

**3단계: 배포 패키지 구성**

최종 사용자에게 전달할 폴더:
```
배포폴더/
├── Engine.exe
├── GameLogic.dll
└── assets/
    ├── scene.json    ← 저장한 씬 파일
    └── tile.png      ← 텍스처 리소스
```

### STANDALONE_MODE 자동 시작 흐름

```
InitD3D12() 끝
  └── [STANDALONE_MODE == 1 일 때만]
        ├── LoadScene("assets/scene.json")   → GetAbsolutePath 경유로 절대 경로 변환
        ├── LoadGameLogicDLL("GameLogic.dll") → 섀도우 카피 후 로드, OnLoad(오브젝트, API) 호출
        └── m_isPlaying = true               → 즉시 플레이 모드 진입
```

### 주의 사항

| 항목 | 내용 |
|------|------|
| `assets/scene.json` 필수 | 파일이 없으면 씬이 빈 채로 실행됩니다. 에디터에서 먼저 저장하세요. |
| `GameLogic.dll` 필수 | 파일이 없으면 DLL 로드 실패 메시지박스가 표시됩니다. |
| CRT 일치 | Engine.exe 와 GameLogic.dll 모두 **Release (/MD)** 로 빌드해야 합니다. |
| ImGui 프레임 | STANDALONE_MODE에서도 `ImGui::NewFrame()` / `ImGui::Render()` 는 호출됩니다. 창만 숨긴 것이므로 크래시가 발생하지 않습니다. |
| 에디터로 돌아오려면 | `STANDALONE_MODE 0` 으로 되돌리고 다시 빌드합니다. |

---

---

## 17. 보드게임 스크립트 프레임워크 (Tag & Team)

### GameObject 식별 필드

`GameLogic.cpp`에서 기물을 구분하기 위해 `GameObject`에 두 개의 퍼블릭 필드가 추가되어 있습니다.

```cpp
// GameObject.h
std::string tag    = "Untagged"; // 기물 종류 ("Pawn", "Knight", "Rook" 등)
int         teamID = 0;          // 0 = 중립(타일), 1 = Player1, 2 = Player2
```

### 상태 머신 — Select → Move / Capture

`GameLogic.cpp`의 `OnObjectClicked`는 클릭된 오브젝트의 `teamID`를 보고 세 가지 분기로 처리합니다.

```
OnObjectClicked(obj)
  ├─ [분기 A] obj->teamID == m_currentTurn
  │     → SelectPiece(obj)        — 내 기물 선택, 초록 하이라이트
  │
  ├─ [분기 B] obj->teamID != 0 && obj->teamID != m_currentTurn
  │     → (m_selectedPiece 있어야)
  │       OnPieceAction("capture") — 말 종류별 효과음
  │       api->Destroy(obj)        — 적 기물 제거
  │       ClearSelection() + EndTurn()
  │
  └─ [분기 C] obj->teamID == 0  (빈 타일)
        ├─ (m_selectedPiece 있음) → MovePiece + ClearSelection + EndTurn
        └─ (m_selectedPiece 없음) → SpawnPawn (테스트 소환, 턴 소비)
```

### 말 종류별 분기 — tag 스위치

`OnPieceAction(piece, action)` 안에서 `piece->tag`를 읽어 말마다 다른 로직을 실행합니다.

```cpp
if (tag == "Knight") {
    api->PlayAudio("assets/move_knight.wav");
    // L자 이동 유효성 검사 추가 가능
}
else if (tag == "Pawn") {
    api->PlayAudio("assets/move_pawn.wav");
    // 앞 한 칸 제한, 앙파상 등 추가 가능
}
else {
    api->PlayAudio("assets/click.wav"); // 기본 효과음
}
```

### 크리에이터 커스터마이징 가이드

| 목표 | 수정 위치 |
|------|-----------|
| 새로운 기물 종류 추가 | `piece->tag = "Rook"` 후 `OnPieceAction`에 `else if (tag == "Rook")` 추가 |
| 이동 가능 범위 제한 | `MovePiece` 호출 전 좌표 유효성 검사 추가 |
| AI 상대 구현 | `Update(dt, objects)` 에서 m_currentTurn == 2 일 때 자동 이동 |
| 승리 조건 체크 | `EndTurn()` 끝에 오브젝트 목록 순회하여 판정 |
| 게임 초기 배치 | `OnLoad(objects, api)` 에서 `api->Instantiate` + `tag`/`teamID` 설정 |

### 색상 팔레트 (GameLogic.cpp 상단)

```cpp
static const XMFLOAT4 COLOR_PLAYER1  = { 0.05f, 0.05f, 0.05f, 1.0f }; // 검정
static const XMFLOAT4 COLOR_PLAYER2  = { 0.90f, 0.85f, 0.75f, 1.0f }; // 베이지
static const XMFLOAT4 COLOR_SELECTED = { 0.20f, 0.80f, 0.20f, 1.0f }; // 초록 (선택)
```

이 상수들만 바꾸면 전체 플레이어 색상 테마가 바뀝니다.

---

*이 문서는 엔진 코드베이스와 함께 버전 관리됩니다. 코드 변경 후 관련 섹션을 함께 업데이트해 주세요.*
