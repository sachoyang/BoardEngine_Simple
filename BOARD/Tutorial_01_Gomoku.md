# Tutorial 01 — 오목(Gomoku) 만들기

> 이 튜토리얼을 마치면 완성된 2인 오목 게임이 손에 쥐어집니다.
> C++ 지식이 조금 있다면 충분합니다. 함께 해봐요! 🎯

---

## 🚨 절대 규칙: 엔진 코어를 건드리지 마세요!

```
❌ include/ 폴더 안의 헤더는 절대 수정하지 마세요.
   IEngineAPI.h, IGameLogic.h, GameObject.h 등 모두 읽기 전용입니다.

✅ 수정해도 되는 파일은 딱 하나입니다.
   GameLogic/GameLogic.cpp
```

SDK 패키지에는 엔진 소스가 포함되어 있지 않습니다. 여러분이 해야 할 일은 `GameLogic/GameLogic.cpp`에 게임 규칙을 작성하는 것뿐입니다. `include/` 헤더를 수정하면 다음 SDK 업데이트 시 덮어씌워집니다. 꼭 지켜주세요.

---

## 완성 미리보기

```
흑돌(●)과 백돌(○)이 번갈아 놓이며,
가로/세로/대각선으로 5개가 먼저 이어지는 플레이어가 승리합니다.
화면 상단에 "Black Wins!" 또는 "White Wins!" 텍스트가 표시됩니다.
```

---

## 목차

- [Step 1. 준비물 — 씬과 에셋 준비하기](#step-1-준비물--씬과-에셋-준비하기)
- [Step 2. 턴 시스템 만들기](#step-2-턴-시스템-만들기)
- [Step 3. 돌 놓기 — Instantiate 없이 텍스처 교체로 구현](#step-3-돌-놓기--instantiate-없이-텍스처-교체로-구현)
- [Step 4. 승리 판정 알고리즘](#step-4-승리-판정-알고리즘)
- [Step 5. UI 피드백](#step-5-ui-피드백)
- [Step 6. 완성 코드 전체 붙여넣기](#step-6-완성-코드-전체-붙여넣기)
- [Step 7. 게임 배포 — 패키지 구성하기](#step-7-게임-배포--패키지-구성하기)
- [Step 8. 친구와 함께하기: 멀티플레이어(P2P) 서버 연동](#step-8-친구와-함께하기-멀티플레이어p2p-서버-연동)

---

## Step 1. 준비물 — 씬과 에셋 준비하기

### 1-1. 에셋 파일 복사

완성본은 돌을 이미지가 아닌 `colorTint`(색 틴트)로 표시하므로 **필요한 에셋은 효과음 하나뿐**입니다.

```
assets/
└── click.wav     ← 돌 놓는 효과음 (짧은 클릭음) — 엔진 기본 assets 에 이미 포함
```

> **돌을 이미지로 표시하고 싶다면?**  
> 아래 두 파일을 추가하고 Step 6 하단의 안내대로 `SetSpriteTexture` 로 바꾸세요.
> ```
> assets/black.png   ← 흑돌 이미지 (예: 검은 원 64×64 PNG)
> assets/white.png   ← 백돌 이미지 (예: 흰 원 64×64 PNG)
> ```

### 1-2. 보드 씬 만들기

1. SDK 루트 폴더에서 `Engine.exe`를 더블클릭해 실행합니다.
   에디터가 실행되면 준비 완료입니다.

2. (아무 내용도 나타나지 않는 것은 정상입니다. 아직 씬이 없으므로 빈 화면이 나타납니다.)

3. 에디터 **Board Generator** 창에서 다음과 같이 설정합니다.

   | 항목 | 값 | 이유 |
   |------|----|------|
   | Rows | 15 | 오목 표준 보드 크기 |
   | Columns | 15 | 오목 표준 보드 크기 |
   | Tile Size | 40 | 15×40 = 600픽셀, 화면에 딱 맞음 |
   | Snap to Grid | ✅ | 드래그 정렬 유지 |
   | Snap Size | 40 | Tile Size 와 일치 |

   > **잠깐!** 15×15 = 225개 타일. 엔진 MAX_OBJECTS 256 이내이므로 안전합니다. ✓

4. **Generate Board** 버튼을 클릭합니다. 15×15 격자가 화면에 그려집니다.

5. 메뉴바에서 **File → Save Scene** 을 클릭합니다.
   - 씬이 `assets/scene.json`으로 저장됩니다.

### 1-3. GameLogic DLL 연결 확인

- 메뉴바 **GameLogic → Load GameLogic.dll** 을 클릭합니다.
- 메뉴바 **[ Play ]** 버튼을 누르면 Play Mode 진입.
- 타일을 클릭해 보세요. 기본 템플릿이 동작하면 준비 완료입니다.

---

## Step 2. 턴 시스템 만들기

`GameLogic.cpp`를 열고 클래스 멤버 변수를 추가합니다.

기존 `GameLogic` 클래스에는 이미 `m_currentTurn`과 `m_api`가 있습니다.
오목 전용 변수를 추가로 선언합니다.

```cpp
class GameLogic : public IGameLogic
{
    IEngineAPI* m_api         = nullptr;
    int  m_currentTurn        = 1;       // 1 = 흑(Black), 2 = 백(White)

    // ── 오목 전용 변수 ────────────────────────────────────────
    static const int BOARD_SIZE = 15;    // Board Generator Rows/Cols 와 일치해야 함
    static const int TILE_SIZE  = 40;    // Board Generator Tile Size 와 일치해야 함
    int  m_board[BOARD_SIZE][BOARD_SIZE] = {};  // 0=빈칸, 1=흑돌, 2=백돌
    bool m_gameOver = false;
    // ──────────────────────────────────────────────────────────
```

> `BOARD_SIZE`와 `TILE_SIZE`는 Board Generator에서 설정한 값과 **반드시 일치**해야 합니다.
> 8×8 보드로 만들었다면 `BOARD_SIZE = 8`, `TILE_SIZE = 64`로 바꾸세요.

`OnLoad`에서 게임 상태를 초기화합니다.

```cpp
void OnLoad(std::vector<GameObject*>& /*objects*/, IEngineAPI* api) override
{
    m_api         = api;
    m_currentTurn = 1;
    m_gameOver    = false;

    // 보드 상태 초기화 (memset 으로 전체 0 填充)
    memset(m_board, 0, sizeof(m_board));

    m_api->SetGameStatusText("Black's Turn", 3.0f);
}
```

---

## Step 3. 돌 놓기 — Instantiate 없이 텍스처 교체로 구현

오목에서는 새 오브젝트를 생성하는 대신 **타일 자체의 텍스처를 돌 이미지로 교체**합니다.
이 방식은 MAX_OBJECTS 한도를 소모하지 않으며 코드도 훨씬 단순합니다.

`OnObjectClicked` 함수를 다음과 같이 교체합니다.

```cpp
void OnObjectClicked(GameObject* obj) override
{
    if (!m_api || !obj) return;

    // ── 게임 종료 상태에서는 클릭 무시 ───────────────────────
    if (m_gameOver) return;

    // ── 타일만 받기 (teamID == 0 = 빈 타일) ──────────────────
    // 기물 오브젝트(teamID != 0)는 오목에서 사용하지 않습니다.
    if (obj->teamID != 0) return;

    auto* tr = obj->GetComponent<Transform>();
    if (!tr) return;

    // ── 클릭한 타일의 행/열 좌표 계산 ────────────────────────
    // 타일 중심 X = col * TILE_SIZE + TILE_SIZE/2 이므로
    // (int)(tr->x / TILE_SIZE) 가 col 이 됩니다.
    int col = static_cast<int>(tr->x / TILE_SIZE);
    int row = static_cast<int>(tr->y / TILE_SIZE);

    // 범위 초과 방어
    if (col < 0 || col >= BOARD_SIZE || row < 0 || row >= BOARD_SIZE) return;

    // ── 이미 돌이 놓인 칸은 무시 ─────────────────────────────
    if (m_board[row][col] != 0)
    {
        m_api->SetGameStatusText("Already occupied!", 1.0f);
        return;
    }

    // ── 보드 상태 기록 ────────────────────────────────────────
    m_board[row][col] = m_currentTurn;

    // ── 텍스처 교체로 돌 표시 ────────────────────────────────
    if (m_currentTurn == 1)
        m_api->SetSpriteTexture(obj, "assets/black.png");  // 흑돌
    else
        m_api->SetSpriteTexture(obj, "assets/white.png");  // 백돌

    // ── 효과음 재생 ───────────────────────────────────────────
    m_api->PlayAudio("assets/click.wav");

    // ── 승리 판정 (Step 4 에서 구현) ─────────────────────────
    if (CheckWin(row, col, m_currentTurn))
    {
        std::string winner = (m_currentTurn == 1) ? "Black Wins!" : "White Wins!";
        m_api->SetGameStatusText(winner, 30.0f);
        m_gameOver = true;
        return;
    }

    // ── 턴 전환 ───────────────────────────────────────────────
    m_currentTurn = (m_currentTurn == 1) ? 2 : 1;
    std::string turnMsg = (m_currentTurn == 1) ? "Black's Turn" : "White's Turn";
    m_api->SetGameStatusText(turnMsg, 3.0f);
}
```

> **이미지 파일이 없을 때 대안**
> `SetSpriteTexture` 대신 `colorTint`를 직접 수정해 색으로 구별할 수도 있습니다.
>
> ```cpp
> // 이미지 없는 버전 — 색상 틴트로 흑/백 구별
> auto* sr = obj->GetComponent<SpriteRenderer>();
> if (sr)
> {
>     sr->colorTint = (m_currentTurn == 1)
>         ? DirectX::XMFLOAT4{0.05f, 0.05f, 0.05f, 1.0f}  // 거의 검정
>         : DirectX::XMFLOAT4{0.95f, 0.95f, 0.95f, 1.0f}; // 거의 흰색
> }
> ```

---

## Step 4. 승리 판정 알고리즘

`GameLogic` 클래스의 `private:` 섹션에 다음 함수를 추가합니다.

오목의 승리 조건은 **가로, 세로, 대각선(두 방향)** 중 하나로 5개 이상 연속입니다.
방향 벡터 4개를 순서대로 검사하면 8방향을 모두 커버할 수 있습니다.

```cpp
private:
    // ----------------------------------------------------------
    // 5목 승리 판정
    // (row, col) 에 player 의 돌이 놓인 직후 호출합니다.
    // 4방향(가로·세로·대각선2)으로 연속 5개 이상이면 true 를 반환합니다.
    // ----------------------------------------------------------
    bool CheckWin(int row, int col, int player)
    {
        // 방향 벡터 (dr=행 증분, dc=열 증분)
        // 한 방향씩 정/역으로 세면 전체 8방향을 커버합니다.
        const int dr[] = { 0,  1,  1,  1 };
        const int dc[] = { 1,  0,  1, -1 };

        for (int d = 0; d < 4; d++)
        {
            int count = 1; // 방금 놓은 돌 자신을 포함

            // 정방향으로 세기
            for (int i = 1; i < 5; i++)
            {
                int r = row + dr[d] * i;
                int c = col + dc[d] * i;
                if (r < 0 || r >= BOARD_SIZE || c < 0 || c >= BOARD_SIZE) break;
                if (m_board[r][c] != player) break;
                count++;
            }

            // 역방향으로 세기
            for (int i = 1; i < 5; i++)
            {
                int r = row - dr[d] * i;
                int c = col - dc[d] * i;
                if (r < 0 || r >= BOARD_SIZE || c < 0 || c >= BOARD_SIZE) break;
                if (m_board[r][c] != player) break;
                count++;
            }

            if (count >= 5) return true; // 5목 완성!
        }

        return false; // 아직 승리 없음
    }
```

> **알고리즘 해설**
>
> ```
> 방향 d=0: (dr=0, dc=1) → 가로  ─
> 방향 d=1: (dr=1, dc=0) → 세로  │
> 방향 d=2: (dr=1, dc=1) → 대각선 ↘
> 방향 d=3: (dr=1, dc=-1)→ 대각선 ↙
>
> 각 방향에서 정방향+역방향을 합산하면
> 새로 놓은 돌을 중심으로 양쪽이 모두 고려됩니다.
>
> 예시 (d=0 가로): ●●●●[새돌]●  → count = 1+2+3 = 6 ≥ 5 → 승리
> ```

---

## Step 5. UI 피드백

승리 판정과 턴 안내는 이미 Step 3 코드에 포함되어 있습니다.
`SetGameStatusText`가 화면 상단 중앙에 텍스트를 표시합니다.

| 상황 | 표시 텍스트 | 지속 시간 |
|------|------------|-----------|
| 게임 시작 / 리로드 | `"Black's Turn"` | 3초 |
| 턴 전환 | `"Black's Turn"` / `"White's Turn"` | 3초 |
| 이미 돌이 있는 칸 클릭 | `"Already occupied!"` | 1초 |
| 흑/백 승리 | `"Black Wins!  (click to rematch)"` | 60초 (게임 종료) |
| 무승부 | `"Draw!  (click to rematch)"` | 60초 (게임 종료) |

게임이 끝나면 보드 아무 곳이나 클릭해 **재대국**할 수 있습니다 (`m_api->RestartGame()`).
에디터에서 **GameLogic → Reload Now** 로 DLL 을 다시 로드해도 `OnLoad`가 재호출됩니다.

---

## Step 6. 완성 코드 전체 붙여넣기

아래 코드를 `GameLogic/GameLogic.cpp` 에 **전체 교체**하여 붙여넣으세요.

> 아래 코드는 저장소에 실제로 포함된 `GameLogic.cpp` 와 **동일**합니다.
> 돌은 이미지 파일 없이 `colorTint` 로 흑/백을 표시하므로 `black.png` / `white.png` 가 필요 없습니다.
> **무승부 판정**과 **재대국(Rematch)** 까지 포함된 완성본입니다.

```cpp
// GameLogic.cpp — 오목(Gomoku) 2인 대전 완성 구현
// 수정 금지: Engine/ 폴더 내부 파일. 이 파일만 편집하세요.

#include <windows.h>
#include <string>
#include <cstring>    // memset
#include "IGameLogic.h"
#include "IEngineAPI.h"
#include "GameObject.h"
#include "Transform.h"
#include "SpriteRenderer.h"

// 돌 색상 (텍스처 위에 곱해지는 colorTint). 별도 이미지 없이 흑/백을 구별합니다.
static const DirectX::XMFLOAT4 STONE_BLACK = { 0.05f, 0.05f, 0.05f, 1.0f };
static const DirectX::XMFLOAT4 STONE_WHITE = { 0.97f, 0.97f, 0.97f, 1.0f };

static void Log(const std::string& msg)
{
    OutputDebugStringA(("[Gomoku] " + msg + "\n").c_str());
}

class GameLogic : public IGameLogic
{
    IEngineAPI* m_api         = nullptr;
    int  m_currentTurn        = 1;       // 1 = 흑, 2 = 백

    // ── 보드 설정 ── Board Generator 설정과 반드시 일치시키세요 ──
    static const int BOARD_SIZE = 15;   // Rows / Cols
    static const int TILE_SIZE  = 40;   // Tile Size (픽셀)
    // ─────────────────────────────────────────────────────────────

    int  m_board[BOARD_SIZE][BOARD_SIZE] = {};  // 0=빈칸, 1=흑, 2=백
    int  m_stones   = 0;                         // 놓인 돌 개수 (무승부 판정용)
    bool m_gameOver = false;

public:
    void OnLoad(std::vector<GameObject*>& /*objects*/, IEngineAPI* api) override
    {
        m_api         = api;
        m_currentTurn = 1;
        m_stones      = 0;
        m_gameOver    = false;
        memset(m_board, 0, sizeof(m_board));

        m_api->SetGameStatusText("Black's Turn", 3.0f);
        Log("Gomoku started. Black goes first.");
    }

    void Update(float /*dt*/, std::vector<GameObject*>& /*objects*/) override
    {
        // 오목은 Update 에서 특별한 처리가 없습니다.
        // AI 상대를 추가하고 싶다면 여기서 구현하세요.
    }

    void OnUnload() override {}

    void OnObjectClicked(GameObject* obj) override
    {
        if (!m_api || !obj) return;

        // ── 게임 종료 상태: 아무 곳이나 클릭하면 재대국 ──────────────
        if (m_gameOver)
        {
            m_api->PlayAudio("assets/click.wav");
            m_api->RestartGame();   // 엔진이 씬을 초기화하고 OnLoad 를 재호출합니다.
            return;                 // RestartGame 직후에는 obj 를 더 사용하지 않습니다.
        }

        if (obj->teamID != 0) return;   // 타일(teamID==0)만 받습니다

        auto* tr = obj->GetComponent<Transform>();
        auto* sr = obj->GetComponent<SpriteRenderer>();
        if (!tr || !sr) return;

        int col = static_cast<int>(tr->x / TILE_SIZE);
        int row = static_cast<int>(tr->y / TILE_SIZE);
        if (col < 0 || col >= BOARD_SIZE || row < 0 || row >= BOARD_SIZE) return;

        if (m_board[row][col] != 0)
        {
            m_api->SetGameStatusText("Already occupied!", 1.0f);
            return;
        }

        // ── 돌 놓기 (colorTint 로 흑/백 표시) ─────────────────────
        m_board[row][col] = m_currentTurn;
        m_stones++;
        sr->colorTint = (m_currentTurn == 1) ? STONE_BLACK : STONE_WHITE;
        m_api->PlayAudio("assets/click.wav");

        Log((m_currentTurn == 1 ? "Black" : "White") +
            std::string(" placed at (") + std::to_string(row) + "," +
            std::to_string(col) + ")");

        // ── 승리 판정 ────────────────────────────────────────────
        if (CheckWin(row, col, m_currentTurn))
        {
            std::string winner = (m_currentTurn == 1) ? "Black Wins!" : "White Wins!";
            m_api->SetGameStatusText(winner + "  (click to rematch)", 60.0f);
            m_gameOver = true;
            Log(winner);
            return;
        }

        // ── 무승부 판정 (판이 가득 참) ────────────────────────────
        if (m_stones >= BOARD_SIZE * BOARD_SIZE)
        {
            m_api->SetGameStatusText("Draw!  (click to rematch)", 60.0f);
            m_gameOver = true;
            Log("Draw — board full.");
            return;
        }

        // ── 턴 전환 ──────────────────────────────────────────────
        m_currentTurn = (m_currentTurn == 1) ? 2 : 1;
        m_api->SetGameStatusText(
            (m_currentTurn == 1) ? "Black's Turn" : "White's Turn", 3.0f);
    }

private:
    // 4방향 × 양쪽 탐색으로 5목 완성 여부를 판정합니다.
    bool CheckWin(int row, int col, int player)
    {
        const int dr[] = { 0,  1,  1,  1 };
        const int dc[] = { 1,  0,  1, -1 };

        for (int d = 0; d < 4; d++)
        {
            int count = 1;

            for (int i = 1; i < 5; i++)
            {
                int r = row + dr[d] * i, c = col + dc[d] * i;
                if (r < 0 || r >= BOARD_SIZE || c < 0 || c >= BOARD_SIZE) break;
                if (m_board[r][c] != player) break;
                count++;
            }
            for (int i = 1; i < 5; i++)
            {
                int r = row - dr[d] * i, c = col - dc[d] * i;
                if (r < 0 || r >= BOARD_SIZE || c < 0 || c >= BOARD_SIZE) break;
                if (m_board[r][c] != player) break;
                count++;
            }

            if (count >= 5) return true;
        }
        return false;
    }
};

extern "C" __declspec(dllexport) IGameLogic* CreateGameLogic()
{
    return new GameLogic();
}
```

> **돌을 이미지로 바꾸고 싶다면?** `sr->colorTint = ...` 두 줄을
> `m_api->SetSpriteTexture(obj, m_currentTurn == 1 ? "assets/black.png" : "assets/white.png");`
> 로 교체하고 `assets/`에 `black.png` / `white.png` 를 넣으세요.

### 빌드 및 테스트

1. `GameLogic/GameLogic_SDK.sln` 을 Visual Studio 2022 에서 엽니다.
2. **빌드 → 솔루션 빌드** (Ctrl+Shift+B). `GameLogic.dll` 이 `Engine.exe` 옆에 자동 출력됩니다.
3. `Engine.exe` 를 실행하고 **GameLogic → Load GameLogic.dll** 을 클릭합니다.
4. 메뉴바 **[ Play ]** 를 눌러 Play Mode 로 진입합니다.
5. 타일을 클릭해 흑/백 돌이 번갈아 나타나는지 확인합니다.
6. 5개를 연속으로 놓아 승리 텍스트가 뜨는지 확인합니다.

---

## Step 7. 게임 배포 — 패키지 구성하기

SDK 버전은 `Engine.h` 소스 파일이 포함되어 있지 않으므로 엔진을 재빌드할 수 없습니다.
대신, SDK 폴더에 이미 들어 있는 파일들을 그대로 묶어 배포하면 됩니다.

### 7-1. 배포 파일 체크리스트

게이머에게 전달할 패키지에 아래 4가지가 모두 있는지 확인하세요.

```
내_오목게임/
├── Engine.exe          ← SDK 에 포함된 실행 파일 (그대로 사용)
├── GameLogic.dll       ← Step 6 빌드 결과물
├── shaders.hlsl        ← 🚨 반드시 포함 — 없으면 실행 즉시 에러 발생
└── assets/
    ├── scene.json      ← Step 1 에서 저장한 15×15 보드 씬
    ├── tile.png        ← 보드 타일 텍스처
    ├── click.wav       ← 효과음
    └── (선택) black.png / white.png  ← 돌을 이미지로 표시할 때만
```

### 7-2. 패키지 구성 및 전달

위 파일들을 하나의 폴더에 담고 압축 파일(`.zip`)로 묶어 전달하면 완성입니다!

```
내_오목게임.zip
└── 내_오목게임/
    ├── Engine.exe
    ├── GameLogic.dll
    ├── shaders.hlsl
    └── assets/
        ├── scene.json
        ├── tile.png
        └── click.wav
```

> **게이머 입장에서:** 압축을 풀고 `Engine.exe`를 더블클릭하면 게임이 바로 시작됩니다.
> 별도 설치가 필요하지 않습니다.

### 7-3. 더 높은 품질을 원한다면

완성도 높은 단독 실행 파일(에디터 UI 없음)이 필요하다면, 엔진 소스를 보유한 개발자에게
`STANDALONE_MODE 1` 빌드를 요청하세요. 그 `Engine.exe`로 교체하면 됩니다.

---

## 더 나아가기 — 오목을 업그레이드해 보세요

튜토리얼을 완료했다면 다음 기능에 도전해 보세요. 모두 `GameLogic.cpp`만 수정하면 됩니다.

| 기능 | 힌트 |
|------|------|
| **무승부 판정** | ✅ 이미 구현됨 — `m_stones` 가 225(15×15)에 도달하면 `"Draw!"` |
| **재대국(Rematch)** | ✅ 이미 구현됨 — 게임 종료 후 클릭 시 `m_api->RestartGame()` (엔진 API) |
| **렌주 룰 (흑의 금수)** | 흑이 3×3이나 4×4를 만들면 `"Forbidden move!"` 표시 후 무효 처리 |
| **AI 상대** | `Update()` 에서 `m_currentTurn == 2` 일 때 보드를 순회해 최선의 빈 칸을 선택 |
| **돌 개수 카운터** | `m_stones` 를 `SetGameStatusText` 로 "Stones: 12" 처럼 표시 |

### 재대국(Rematch)은 어떻게 동작하나요?

이번 버전에는 엔진에 새로 추가된 **`RestartGame()`** API 를 사용합니다.
(Unity/Godot 의 씬 리로드 — Godot `reload_current_scene()` 에서 착안)

```cpp
if (m_gameOver)
{
    m_api->RestartGame();  // 엔진이 scene.json 을 다시 로드하고 OnLoad 를 재호출
    return;
}
```

`RestartGame()` 을 호출하면 엔진이 **프레임 끝에서** 씬을 처음 상태로 복원하고
`OnLoad` 를 다시 부릅니다. 착색된 돌(`colorTint`)도 원본으로 돌아가 보드가 깨끗해집니다.
직접 `m_board` 를 초기화할 필요가 없습니다 — `OnLoad` 가 다 해줍니다.

> ⚠️ `RestartGame()` 은 씬 전체를 지우고 다시 만들므로, 호출 직후에는
> `obj` 등 오브젝트 포인터를 더 사용하지 말고 바로 `return` 하세요.

---

---

## Step 8. 친구와 함께하기: 멀티플레이어(P2P) 서버 연동

혼자 하는 게임은 이제 그만! 우리가 방금 완성한 `GameLogic.cpp`에 윈도우 네트워크 소켓(Winsock2)을 조금만 추가하면, 두 대의 컴퓨터를 연결해 완벽한 온라인 오목 게임을 만들 수 있습니다.

서버를 따로 구축할 필요 없이, 한 명이 방장(Host)이 되고 한 명이 참가자(Client)가 되어 서로 돌의 좌표(row, col)를 실시간으로 주고받는 구조입니다.

### 8-1. 작동 원리

```
[방장 PC]                              [참가자 PC]
 Engine.exe 실행                        Engine.exe 실행
   │                                      │
   H 키 → 대기 소켓 열기                C 키 → 방장에게 연결
   │                                      │
   ◄─────────── TCP 연결 수립 ───────────►
   │                                      │
   타일 클릭 → send(row, col) ──────────► 수신 → 보드에 돌 반영
                                           타일 클릭 → send(row, col)
   ◄─────────────────────────────────────  수신 → 보드에 돌 반영
```

### 8-2. 이게 가능한 이유

`GameLogic.cpp`는 일반 Windows DLL입니다.
`#include <winsock2.h>` 한 줄로 윈도우 기본 소켓 라이브러리를 그대로 사용할 수 있습니다.
**엔진 코어는 전혀 건드리지 않아도 됩니다.**

| 추가해야 할 것 | 설명 |
|---------------|------|
| `#include <winsock2.h>` | 소켓 함수 사용 |
| `#pragma comment(lib, "ws2_32.lib")` | 링커에 ws2_32.dll 자동 연결 |
| `ioctlsocket(FIONBIO)` | 논블로킹 설정 — 게임 루프를 멈추지 않음 |
| `Update()`에서 `recv()` | 매 프레임 상대방 좌표 수신 |
| `OnObjectClicked()`에서 `send()` | 돌 놓을 때 좌표 전송 |

### 8-3. 다음 단계로

상세한 코드와 단계별 구현 방법은 **Tutorial 03** 에서 다룹니다.
지금 바로 도전해 보세요!

> 📖 **[Tutorial 03 — P2P 멀티플레이어 오목](Tutorial_03_Multiplayer_P2P.md)**
>
> 이 파일과 같은 폴더에 있는 `Tutorial_03_Multiplayer_P2P.md` 를 열어
> `GameLogic.cpp` 를 온라인 멀티플레이어 버전으로 업그레이드하세요.
> 오목판은 그대로 두고, 네트워크 코드만 추가하면 됩니다. 정말이에요! 🚀

---

> 이 튜토리얼에서 막히는 부분이 있거나 다음 튜토리얼이 궁금하다면 언제든 물어보세요.
> 다음은 **Tutorial 02 — 체스 만들기** 입니다. 함께 만들어 봐요!
