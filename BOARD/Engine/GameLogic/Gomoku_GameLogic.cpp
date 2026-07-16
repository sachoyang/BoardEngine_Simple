// ============================================================
// GameLogic.cpp — 오목(Gomoku) 2인 대전 완성 구현
// ============================================================
//
// [크리에이터를 위한 안내]
// 이 파일 하나만 수정해서 체스, 장기, 오목 등 다양한 보드게임을 만들 수 있습니다.
// 엔진 코어(include/*.h, Engine/ 폴더)는 절대 건드리지 마세요.
//
// ■ 게임 규칙
//   흑(●) → 백(○) 순으로 번갈아 착수. 가로/세로/대각선으로 5개가 먼저
//   이어지는 플레이어가 승리합니다. 판이 가득 차면 무승부입니다.
//   게임이 끝난 뒤 보드 아무 곳이나 클릭하면 재대국(Rematch)이 시작됩니다.
//
// ■ 준비물 (Board Generator 설정과 반드시 일치)
//   Rows/Cols = 15, Tile Size = 40  → File ▸ Save Scene 으로 assets/scene.json 저장
//
// ■ 사용하는 엔진 API (IEngineAPI)
//   SetSpriteTexture / colorTint     → 돌 표시 (여기서는 colorTint 사용, 별도 이미지 불필요)
//   PlayAudio(path)                  → 착수 효과음
//   SetGameStatusText(text, sec)     → 턴 안내 · 승부 결과 표시
//   RestartGame()                    → 게임 종료 후 재대국 (엔진이 씬을 초기화하고 OnLoad 재호출)
// ============================================================

#include <windows.h>  // OutputDebugStringA
#include <string>
#include <cstring>    // memset
#include "IGameLogic.h"
#include "IEngineAPI.h"
#include "GameObject.h"
#include "Transform.h"
#include "SpriteRenderer.h"

// 돌 색상 (텍스처 위에 곱해지는 colorTint). 별도 이미지 없이 흑/백을 구별합니다.
static const DirectX::XMFLOAT4 STONE_BLACK = { 0.05f, 0.05f, 0.05f, 1.0f }; // 흑돌
static const DirectX::XMFLOAT4 STONE_WHITE = { 0.97f, 0.97f, 0.97f, 1.0f }; // 백돌

static void Log(const std::string& msg)
{
    OutputDebugStringA(("[Gomoku] " + msg + "\n").c_str());
}

class GameLogic : public IGameLogic
{
    IEngineAPI* m_api         = nullptr;
    int         m_currentTurn = 1;       // 1 = 흑(Black), 2 = 백(White)

    // ── 보드 설정 ── Board Generator 설정과 반드시 일치시키세요 ──
    static const int BOARD_SIZE = 15;    // Rows / Cols
    static const int TILE_SIZE  = 40;    // Tile Size (픽셀)
    // ─────────────────────────────────────────────────────────

    int  m_board[BOARD_SIZE][BOARD_SIZE] = {}; // 0=빈칸, 1=흑, 2=백
    int  m_stones   = 0;                        // 지금까지 놓인 돌 개수 (무승부 판정용)
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
        // 오목은 매 프레임 처리할 일이 없습니다. AI 상대를 붙이려면 여기서 구현하세요.
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

        if (obj->teamID != 0) return; // 타일(teamID==0)만 받습니다

        auto* tr = obj->GetComponent<Transform>();
        auto* sr = obj->GetComponent<SpriteRenderer>();
        if (!tr || !sr) return;

        // ── 클릭한 타일의 행/열 좌표 계산 ─────────────────────────
        // 타일 중심 X = col * TILE_SIZE + TILE_SIZE/2 이므로 (int)(x / TILE_SIZE) = col.
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

        Log((m_currentTurn == 1 ? "Black" : "White") + std::string(" placed at (") +
            std::to_string(row) + "," + std::to_string(col) + ")");

        // ── 승리 판정 ─────────────────────────────────────────────
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

        // ── 턴 전환 ───────────────────────────────────────────────
        m_currentTurn = (m_currentTurn == 1) ? 2 : 1;
        m_api->SetGameStatusText(
            (m_currentTurn == 1) ? "Black's Turn" : "White's Turn", 3.0f);
    }

private:
    // 4방향(가로·세로·대각선2) × 양쪽 탐색으로 5목 완성 여부를 판정합니다.
    bool CheckWin(int row, int col, int player)
    {
        const int dr[] = { 0,  1,  1,  1 };
        const int dc[] = { 1,  0,  1, -1 };

        for (int d = 0; d < 4; d++)
        {
            int count = 1; // 방금 놓은 돌 포함

            for (int i = 1; i < 5; i++) // 정방향
            {
                int r = row + dr[d] * i, c = col + dc[d] * i;
                if (r < 0 || r >= BOARD_SIZE || c < 0 || c >= BOARD_SIZE) break;
                if (m_board[r][c] != player) break;
                count++;
            }
            for (int i = 1; i < 5; i++) // 역방향
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

// ============================================================
// 팩토리 함수 — Engine 이 GetProcAddress("CreateGameLogic") 로 찾아 호출합니다.
// ============================================================
extern "C" __declspec(dllexport) IGameLogic* CreateGameLogic()
{
    return new GameLogic();
}
