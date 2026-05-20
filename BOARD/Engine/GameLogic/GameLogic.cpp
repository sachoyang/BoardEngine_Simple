// ============================================================
// GameLogic.cpp — 보드게임 상태 머신 기본 템플릿
// ============================================================
//
// [크리에이터를 위한 안내]
// 이 파일 하나만 수정해서 체스, 장기, 오목 등 다양한 보드게임을 만들 수 있습니다.
//
// ■ 핵심 흐름
//   클릭 → OnObjectClicked() 진입
//     ├─ [분기 A] 내 기물 클릭    → 선택(Select)
//     ├─ [분기 B] 적 기물 클릭    → 공격(Capture) — 선택된 기물이 있어야 함
//     └─ [분기 C] 빈 타일 클릭   → 이동(Move) 또는 소환(Spawn)
//
// ■ GameObject 식별 필드 (GameObject.h)
//   tag    : 기물 종류 문자열 ("Pawn", "Knight", "Rook" 등)
//   teamID : 0 = 중립(타일), 1 = Player1, 2 = Player2
//
// ■ IEngineAPI 를 통해 사용할 수 있는 엔진 기능
//   Instantiate(name, x, y)         → 새 오브젝트 생성 후 포인터 반환
//   AddSpriteRenderer(obj, texPath) → 텍스처 렌더러 추가
//   Destroy(obj)                    → 프레임 끝에 안전하게 오브젝트 삭제
//   FindObjectByName(name)          → 이름으로 오브젝트 검색
//   PlayAudio(path)                 → .wav 파일 재생 (비동기, 단일 채널)
// ============================================================

#include <windows.h>  // OutputDebugStringA, DirectX 타입
#include <string>
#include "IGameLogic.h"
#include "IEngineAPI.h"
#include "GameObject.h"
#include "Transform.h"
#include "SpriteRenderer.h"

// ============================================================
// 색상 팔레트 — 원하는 색으로 자유롭게 바꾸세요.
// XMFLOAT4{ R, G, B, A }  (각 채널 0.0 ~ 1.0)
// ============================================================
static const DirectX::XMFLOAT4 COLOR_PLAYER1  = { 0.05f, 0.05f, 0.05f, 1.0f }; // 검정 (Player 1)
static const DirectX::XMFLOAT4 COLOR_PLAYER2  = { 0.90f, 0.85f, 0.75f, 1.0f }; // 베이지 (Player 2)
static const DirectX::XMFLOAT4 COLOR_SELECTED = { 0.20f, 0.80f, 0.20f, 1.0f }; // 초록 (선택 하이라이트)

// ============================================================
// 디버그 로그 — Visual Studio 출력 창에 표시됩니다.
// 배포 시 이 매크로를 빈 매크로로 교체하거나 파일 로그로 변경하세요.
// ============================================================
static void Log(const std::string& msg)
{
    OutputDebugStringA(("[GameLogic] " + msg + "\n").c_str());
}

// ============================================================
// GameLogic 클래스 — IGameLogic 구현
// ============================================================
class GameLogic : public IGameLogic
{
    IEngineAPI* m_api           = nullptr;
    int         m_currentTurn   = 1;       // 현재 행동 중인 플레이어 (1 또는 2)
    GameObject* m_selectedPiece = nullptr; // 선택된 기물 포인터 (없으면 nullptr)

public:
    // ----------------------------------------------------------
    // OnLoad: 씬 로드 또는 DLL 핫 리로드 직후 호출됩니다.
    // 게임 초기화(점수판, AI, 타이머 등)를 여기서 수행하세요.
    // ----------------------------------------------------------
    void OnLoad(std::vector<GameObject*>& /*objects*/, IEngineAPI* api) override
    {
        m_api           = api;
        m_currentTurn   = 1;
        m_selectedPiece = nullptr;
        Log("OnLoad — Player 1's turn.");
    }

    // ----------------------------------------------------------
    // Update: 매 프레임 호출됩니다 (dt = 이전 프레임과의 경과 시간, 초 단위).
    // AI 이동, 타이머, 체크 감지 등을 여기서 구현하세요.
    // ----------------------------------------------------------
    void Update(float /*dt*/, std::vector<GameObject*>& /*objects*/) override
    {
        // TODO: AI 로직, 타이머, 승리 조건 등을 여기에 추가하세요.
    }

    // ----------------------------------------------------------
    // OnUnload: DLL 언로드 또는 핫 리로드 직전에 호출됩니다.
    // 동적 할당 해제나 게임 상태 저장은 여기서 수행하세요.
    // ----------------------------------------------------------
    void OnUnload() override
    {
        m_selectedPiece = nullptr; // 댕글링 포인터 방지
    }

    // ----------------------------------------------------------
    // OnObjectClicked: Play Mode 에서 오브젝트를 클릭하면 호출됩니다.
    // 이 함수가 보드게임 상호작용의 핵심 진입점입니다.
    // ----------------------------------------------------------
    void OnObjectClicked(GameObject* obj) override
    {
        if (!m_api || !obj) return;

        auto* tr = obj->GetComponent<Transform>();
        auto* sr = obj->GetComponent<SpriteRenderer>();
        if (!tr || !sr) return;

        // ==============================================
        // 분기 A: 내 기물 클릭 → 선택(Select)
        // obj->teamID 가 현재 플레이어와 같으면 이 기물을 선택합니다.
        // ==============================================
        if (obj->teamID == m_currentTurn)
        {
            SelectPiece(obj);
            return;
        }

        // ==============================================
        // 분기 B: 적 기물 클릭 → 공격(Capture)
        // 선택된 기물이 있을 때만 공격할 수 있습니다.
        // teamID != 0 조건으로 타일(중립)과 구분합니다.
        // ==============================================
        if (obj->teamID != 0 && obj->teamID != m_currentTurn)
        {
            if (!m_selectedPiece)
            {
                Log("Capture failed — no piece selected.");
                return;
            }
            Log("Capture! " + m_selectedPiece->name + " takes " + obj->name);
            OnPieceAction(m_selectedPiece, "capture");
            m_api->Destroy(obj); // 적 기물을 프레임 끝에 안전하게 삭제
            ClearSelection();
            EndTurn();
            return;
        }

        // ==============================================
        // 분기 C: 빈 타일 클릭 → 이동(Move) 또는 소환(Spawn)
        // teamID == 0 인 오브젝트는 중립 타일로 취급합니다.
        // ==============================================
        if (obj->teamID == 0)
        {
            if (m_selectedPiece)
            {
                // 선택된 기물이 있으면 이 타일 위치로 이동
                Log("Move: " + m_selectedPiece->name + " → tile at (" +
                    std::to_string(static_cast<int>(tr->x)) + ", " +
                    std::to_string(static_cast<int>(tr->y)) + ")");
                OnPieceAction(m_selectedPiece, "move");
                MovePiece(m_selectedPiece, tr->x, tr->y);
                ClearSelection();
                EndTurn();
            }
            else
            {
                // 선택된 기물이 없으면 테스트 기물(Pawn)을 이 타일에 소환
                // ※ 실제 게임에서는 이 분기를 제거하거나 원하는 소환 로직으로 교체하세요.
                SpawnPawn(tr, sr);
            }
        }
    }

private:
    // ----------------------------------------------------------
    // 기물 선택 — 이전 선택을 해제하고 새 기물을 하이라이트합니다.
    // ----------------------------------------------------------
    void SelectPiece(GameObject* piece)
    {
        // 이미 같은 기물이 선택되어 있으면 선택 해제(토글)
        if (m_selectedPiece == piece)
        {
            ClearSelection();
            Log("Deselected: " + piece->name);
            return;
        }

        // 이전 선택 기물의 색상 복원
        ClearSelection();

        m_selectedPiece = piece;

        // 선택된 기물을 초록색으로 하이라이트
        auto* sr = piece->GetComponent<SpriteRenderer>();
        if (sr) sr->colorTint = COLOR_SELECTED;

        Log("Selected: " + piece->name + " (tag=" + piece->tag + ")");
    }

    // ----------------------------------------------------------
    // 선택 해제 — 하이라이트 색상을 팀 색상으로 되돌립니다.
    // ----------------------------------------------------------
    void ClearSelection()
    {
        if (!m_selectedPiece) return;

        auto* sr = m_selectedPiece->GetComponent<SpriteRenderer>();
        if (sr)
            sr->colorTint = (m_selectedPiece->teamID == 1) ? COLOR_PLAYER1 : COLOR_PLAYER2;

        m_selectedPiece = nullptr;
    }

    // ----------------------------------------------------------
    // 기물 이동 — Transform 좌표를 대상 타일 위치로 변경합니다.
    // 이동 가능 범위 검사(체스 규칙 등)는 여기서 추가하세요.
    // ----------------------------------------------------------
    void MovePiece(GameObject* piece, float toX, float toY)
    {
        auto* tr = piece->GetComponent<Transform>();
        if (tr) { tr->x = toX; tr->y = toY; }
    }

    // ----------------------------------------------------------
    // 말 종류(tag)별 특수 로직
    // tag 를 읽어 말마다 다른 효과음과 행동을 정의합니다.
    // ── 사용 예 ──────────────────────────────────────────────
    //   piece->tag = "Knight"; → OnPieceAction 에서 별도 처리
    //   piece->tag = "Rook";   → 직선 이동만 허용하는 유효성 검사 추가
    // ─────────────────────────────────────────────────────────
    // ----------------------------------------------------------
    void OnPieceAction(GameObject* piece, const std::string& action)
    {
        const std::string& tag = piece->tag;

        if (tag == "Knight")
        {
            // 기사(나이트): L자 이동. 이동 가능 위치 검증 로직을 여기에 추가하세요.
            m_api->PlayAudio("assets/move_knight.wav");
            Log("Knight " + action + " (L-shape)");
        }
        else if (tag == "Pawn")
        {
            // 폰: 앞으로 한 칸 이동. 첫 수 두 칸, 앙파상 규칙도 여기서 처리하세요.
            m_api->PlayAudio("assets/move_pawn.wav");
            Log("Pawn " + action);
        }
        else
        {
            // 나머지 기물: 공통 효과음 사용
            m_api->PlayAudio("assets/click.wav");
        }
    }

    // ----------------------------------------------------------
    // 테스트 기물(Pawn) 소환 — 빈 타일에 선택된 기물이 없을 때 호출됩니다.
    // 실제 게임에서는 이 함수를 원하는 초기 배치 로직으로 교체하세요.
    // ----------------------------------------------------------
    void SpawnPawn(Transform* tileTr, SpriteRenderer* tileSr)
    {
        // 오브젝트 이름은 고유하게 짓는 것을 권장합니다.
        std::string pawnName = "Pawn_P" + std::to_string(m_currentTurn) + "_" +
                               std::to_string(static_cast<int>(tileTr->x)) + "_" +
                               std::to_string(static_cast<int>(tileTr->y));

        GameObject* piece = m_api->Instantiate(pawnName, tileTr->x, tileTr->y);
        if (!piece)
        {
            Log("Spawn failed — MAX_OBJECTS limit reached.");
            return;
        }

        // ── 태그와 팀 설정 ─────────────────────────────────────
        piece->tag    = "Pawn";
        piece->teamID = m_currentTurn;

        // ── 크기는 타일 그대로 상속 ───────────────────────────
        auto* pTr = piece->GetComponent<Transform>();
        if (pTr) { pTr->width = tileTr->width; pTr->height = tileTr->height; }

        // ── 텍스처 및 팀 색상 적용 ────────────────────────────
        m_api->AddSpriteRenderer(piece, tileSr->GetTexturePath());
        auto* pSr = piece->GetComponent<SpriteRenderer>();
        if (pSr)
            pSr->colorTint = (m_currentTurn == 1) ? COLOR_PLAYER1 : COLOR_PLAYER2;

        m_api->PlayAudio("assets/click.wav");
        Log("Spawned Pawn for Player " + std::to_string(m_currentTurn));

        EndTurn();
    }

    // ----------------------------------------------------------
    // 턴 전환 — 승리 조건 체크는 이 함수 안에서 수행하세요.
    // ----------------------------------------------------------
    void EndTurn()
    {
        m_currentTurn = (m_currentTurn == 1) ? 2 : 1;
        Log("Turn ended → Player " + std::to_string(m_currentTurn) + "'s turn.");
        // TODO: 승리 조건 체크 (체크메이트, 목 없음, 오목 5개 등)
    }
};

// ============================================================
// 팩토리 함수 — Engine 이 GetProcAddress("CreateGameLogic") 로 찾아 호출합니다.
// extern "C" 로 이름 맹글링을 막아야 GetProcAddress 가 심볼을 찾을 수 있습니다.
// ============================================================
extern "C" __declspec(dllexport) IGameLogic* CreateGameLogic()
{
    return new GameLogic();
}
