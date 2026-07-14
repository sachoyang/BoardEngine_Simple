// ============================================================
// Chess_GameLogic.cpp — 체스 2인 대전 (엔진 무수정, GameLogic.cpp 하나로 구현)
// ============================================================
//
// [사용법]  이 파일 내용을 GameLogic/GameLogic.cpp 에 통째로 덮어쓰고 DLL 을 빌드하세요.
// [보드]    Board Generator: Rows 8, Cols 8, Tile Size 64  → File ▸ Save Scene
// [에셋]    assets/click.wav 만 있으면 됩니다. 말은 색칠된 칸 + 글자(P N B R Q K)로 표시하므로
//           별도 이미지가 필요 없습니다. (엔진의 SetObjectText / Label 컴포넌트 사용)
//
// [규칙]    표준 이동/캡처 + 폰 승급(→퀸) + 체크 / 체크메이트 / 스테일메이트.
//           내 킹이 잡히는 수(체크 방치)는 둘 수 없습니다.
//           (캐슬링·앙파상은 생략 — 필요 시 확장하세요)
//
// [사용 엔진 API]  Instantiate / AddSpriteRenderer / Destroy / SetSpriteTexture /
//                  SetObjectText / SetGameStatusText / PlayAudio / RestartGame
// ============================================================

#include <windows.h>
#include <string>
#include <cstring>   // memset
#include "IGameLogic.h"
#include "IEngineAPI.h"
#include "GameObject.h"
#include "Transform.h"
#include "SpriteRenderer.h"

static const int BOARD = 8;
static const int TILE  = 64;

static const DirectX::XMFLOAT4 TINT_WHITE_PIECE = { 0.88f, 0.86f, 0.80f, 1.0f };
static const DirectX::XMFLOAT4 TINT_BLACK_PIECE = { 0.18f, 0.18f, 0.22f, 1.0f };
static const DirectX::XMFLOAT4 TINT_SELECTED    = { 0.35f, 0.85f, 0.35f, 1.0f };

static void Log(const std::string& m) { OutputDebugStringA(("[Chess] " + m + "\n").c_str()); }
static int  sgn(int v) { return (v > 0) - (v < 0); }

class GameLogic : public IGameLogic
{
    IEngineAPI* m_api  = nullptr;
    int         m_turn = 1;              // 1 = White(하단), 2 = Black(상단)
    bool        m_over = false;

    GameObject* m_grid[BOARD][BOARD] = {}; // [row][col] 의 말 (없으면 nullptr)
    GameObject* m_sel  = nullptr;
    int         m_selR = -1, m_selC = -1;

public:
    void OnLoad(std::vector<GameObject*>& /*objects*/, IEngineAPI* api) override
    {
        m_api  = api;
        m_turn = 1;
        m_over = false;
        m_sel  = nullptr;
        m_selR = m_selC = -1;
        memset(m_grid, 0, sizeof(m_grid));

        SetupPieces();
        m_api->SetGameStatusText("White's Turn", 3.0f);
        Log("Chess started. White moves first.");
    }

    void Update(float /*dt*/, std::vector<GameObject*>& /*objects*/) override {}
    void OnUnload() override { m_sel = nullptr; }

    void OnObjectClicked(GameObject* obj) override
    {
        if (!m_api || !obj) return;

        if (m_over) // 종료 후 아무 곳이나 클릭 → 재대국
        {
            m_api->PlayAudio("assets/click.wav");
            m_api->RestartGame();
            return;
        }

        auto* tr = obj->GetComponent<Transform>();
        if (!tr) return;
        int c = static_cast<int>(tr->x / TILE);
        int r = static_cast<int>(tr->y / TILE);
        if (!InBounds(r, c)) return;

        // 내 말 클릭 → 선택(토글/전환)
        if (obj->teamID == m_turn) { Select(obj, r, c); return; }

        if (!m_sel) return; // 옮길 말이 선택되어 있어야 함

        if (!PseudoLegal(m_selR, m_selC, r, c))
        {
            m_api->SetGameStatusText("Invalid move", 1.0f);
            return;
        }
        if (LeavesKingInCheck(m_selR, m_selC, r, c, m_turn))
        {
            m_api->SetGameStatusText("Illegal - your king is in check", 1.5f);
            return;
        }
        DoMove(r, c);
    }

private:
    // ── 유틸 ────────────────────────────────────────────────────
    bool InBounds(int r, int c) const { return r >= 0 && r < BOARD && c >= 0 && c < BOARD; }
    GameObject* PieceAt(int r, int c) const { return InBounds(r, c) ? m_grid[r][c] : nullptr; }
    static int  Other(int team) { return team == 1 ? 2 : 1; }

    std::string Letter(const std::string& tag) const
    {
        if (tag == "pawn")   return "P";
        if (tag == "knight") return "N";
        if (tag == "bishop") return "B";
        if (tag == "rook")   return "R";
        if (tag == "queen")  return "Q";
        if (tag == "king")   return "K";
        return "?";
    }

    // ── 배치 ────────────────────────────────────────────────────
    GameObject* Spawn(const std::string& tag, int team, int r, int c)
    {
        float x = c * TILE + TILE * 0.5f;
        float y = r * TILE + TILE * 0.5f;
        std::string name = std::string(team == 1 ? "W_" : "B_") + tag + "_" +
                           std::to_string(r) + "_" + std::to_string(c);

        GameObject* p = m_api->Instantiate(name, x, y);
        if (!p) { Log("Spawn failed — MAX_OBJECTS reached."); return nullptr; }

        auto* tr = p->GetComponent<Transform>();
        if (tr) { tr->width = TILE; tr->height = TILE; }

        p->tag    = tag;
        p->teamID = team;

        // 색칠된 칸(SpriteRenderer) + 글자(Label) 로 말을 표현 → 이미지 불필요
        m_api->AddSpriteRenderer(p, "assets/tile.png");
        auto* sr = p->GetComponent<SpriteRenderer>();
        if (sr) sr->colorTint = (team == 1) ? TINT_WHITE_PIECE : TINT_BLACK_PIECE;
        ApplyLetter(p, tag, team);

        m_grid[r][c] = p;
        return p;
    }

    void ApplyLetter(GameObject* p, const std::string& tag, int team)
    {
        // 흰 말은 어두운 글자, 검은 말은 밝은 글자 (칸 색과 대비)
        if (team == 1) m_api->SetObjectText(p, Letter(tag), 0.10f, 0.10f, 0.10f);
        else           m_api->SetObjectText(p, Letter(tag), 0.95f, 0.95f, 0.95f);
    }

    void SetupPieces()
    {
        const char* back[BOARD] =
            { "rook", "knight", "bishop", "queen", "king", "bishop", "knight", "rook" };

        for (int c = 0; c < BOARD; c++) Spawn(back[c], 2, 0, c); // Black 후열 (상단)
        for (int c = 0; c < BOARD; c++) Spawn("pawn",  2, 1, c); // Black 폰
        for (int c = 0; c < BOARD; c++) Spawn(back[c], 1, 7, c); // White 후열 (하단)
        for (int c = 0; c < BOARD; c++) Spawn("pawn",  1, 6, c); // White 폰
    }

    // ── 선택 ────────────────────────────────────────────────────
    void Tint(GameObject* p, const DirectX::XMFLOAT4& col)
    {
        auto* sr = p->GetComponent<SpriteRenderer>();
        if (sr) sr->colorTint = col;
    }

    void Select(GameObject* p, int r, int c)
    {
        if (m_sel == p) { Deselect(); return; }
        Deselect();
        m_sel = p; m_selR = r; m_selC = c;
        Tint(p, TINT_SELECTED);
    }

    void Deselect()
    {
        if (!m_sel) return;
        Tint(m_sel, (m_sel->teamID == 1) ? TINT_WHITE_PIECE : TINT_BLACK_PIECE);
        m_sel = nullptr; m_selR = m_selC = -1;
    }

    // ── 이동 실행 ────────────────────────────────────────────────
    void DoMove(int tr, int tc)
    {
        GameObject* mover   = m_sel;
        GameObject* capture = m_grid[tr][tc];

        if (capture)
        {
            m_api->Destroy(capture);   // 프레임 끝에 안전 삭제
            m_grid[tr][tc] = nullptr;  // 포인터 즉시 정리 (이후 접근 금지)
        }

        auto* trns = mover->GetComponent<Transform>();
        if (trns) { trns->x = tc * TILE + TILE * 0.5f; trns->y = tr * TILE + TILE * 0.5f; }
        m_grid[m_selR][m_selC] = nullptr;
        m_grid[tr][tc] = mover;

        // 폰 승급 → 퀸
        if (mover->tag == "pawn" && (tr == 0 || tr == BOARD - 1))
        {
            mover->tag = "queen";
            ApplyLetter(mover, "queen", mover->teamID);
        }

        m_api->PlayAudio("assets/click.wav");
        Deselect();

        // 턴 넘기고 상대의 체크/메이트 판정
        m_turn = Other(m_turn);
        bool inChk   = InCheck(m_turn);
        bool anyMove = HasAnyLegalMove(m_turn);

        if (!anyMove)
        {
            if (inChk)
            {
                std::string winner = (m_turn == 1) ? "Black Wins - Checkmate!"
                                                   : "White Wins - Checkmate!";
                m_api->SetGameStatusText(winner + "  (click to rematch)", 60.0f);
            }
            else
            {
                m_api->SetGameStatusText("Stalemate - Draw!  (click to rematch)", 60.0f);
            }
            m_over = true;
            return;
        }

        std::string msg = (m_turn == 1) ? "White's Turn" : "Black's Turn";
        if (inChk) msg += " - Check!";
        m_api->SetGameStatusText(msg, 3.0f);
    }

    // ── 규칙 판정 ────────────────────────────────────────────────
    bool PathClear(int r0, int c0, int r1, int c1) const
    {
        int dr = sgn(r1 - r0), dc = sgn(c1 - c0);
        int r = r0 + dr, c = c0 + dc;
        while (r != r1 || c != c1)
        {
            if (m_grid[r][c]) return false;
            r += dr; c += dc;
        }
        return true;
    }

    // (fr,fc) 의 말이 (tr,tc) 칸을 '공격'하는가 (체크 판정용, 점유자 무관 · 순수 기하)
    bool Attacks(int fr, int fc, int tr, int tc) const
    {
        GameObject* p = m_grid[fr][fc];
        if (!p) return false;
        int dr = tr - fr, dc = tc - fc;
        int adr = dr < 0 ? -dr : dr;
        int adc = dc < 0 ? -dc : dc;
        const std::string& tag = p->tag;

        if (tag == "knight") return (adr == 1 && adc == 2) || (adr == 2 && adc == 1);
        if (tag == "king")   return adr <= 1 && adc <= 1 && (adr + adc > 0);
        if (tag == "rook")   return (dr == 0 || dc == 0) && PathClear(fr, fc, tr, tc);
        if (tag == "bishop") return (adr == adc) && PathClear(fr, fc, tr, tc);
        if (tag == "queen")  return (dr == 0 || dc == 0 || adr == adc) && PathClear(fr, fc, tr, tc);
        if (tag == "pawn")
        {
            int fwd = (p->teamID == 1) ? -1 : +1;
            return dr == fwd && adc == 1;
        }
        return false;
    }

    // 현재 턴 말의 유사합법(pseudo-legal) 이동 — 체크 방치 여부는 별도 검사
    bool PseudoLegal(int fr, int fc, int tr, int tc) const
    {
        if (!InBounds(tr, tc)) return false;
        if (fr == tr && fc == tc) return false;
        GameObject* p = m_grid[fr][fc];
        if (!p) return false;

        GameObject* dst = m_grid[tr][tc];
        if (dst && dst->teamID == p->teamID) return false; // 아군 칸 불가

        if (p->tag == "pawn")
        {
            int fwd      = (p->teamID == 1) ? -1 : +1;
            int startRow = (p->teamID == 1) ?  6 :  1;
            int dr = tr - fr, dc = tc - fc;
            int adc = dc < 0 ? -dc : dc;

            if (dc == 0) // 직진 (캡처 불가)
            {
                if (dst) return false;
                if (dr == fwd) return true;
                if (fr == startRow && dr == 2 * fwd && !m_grid[fr + fwd][fc]) return true;
                return false;
            }
            if (adc == 1 && dr == fwd) return dst != nullptr; // 대각 캡처 (아군은 위에서 배제)
            return false;
        }

        // 나머지 말은 공격 기하 == 이동 기하
        return Attacks(fr, fc, tr, tc);
    }

    void FindKing(int team, int& kr, int& kc) const
    {
        kr = kc = -1;
        for (int r = 0; r < BOARD; r++)
            for (int c = 0; c < BOARD; c++)
            {
                GameObject* p = m_grid[r][c];
                if (p && p->teamID == team && p->tag == "king") { kr = r; kc = c; return; }
            }
    }

    bool IsSquareAttacked(int r, int c, int byTeam) const
    {
        for (int fr = 0; fr < BOARD; fr++)
            for (int fc = 0; fc < BOARD; fc++)
            {
                GameObject* p = m_grid[fr][fc];
                if (p && p->teamID == byTeam && Attacks(fr, fc, r, c)) return true;
            }
        return false;
    }

    bool InCheck(int team) const
    {
        int kr, kc; FindKing(team, kr, kc);
        if (kr < 0) return false;
        return IsSquareAttacked(kr, kc, Other(team));
    }

    // (fr,fc)→(tr,tc) 를 두면 team 의 킹이 체크에 걸리는가? (m_grid 임시 이동 후 복원)
    bool LeavesKingInCheck(int fr, int fc, int tr, int tc, int team)
    {
        GameObject* mover   = m_grid[fr][fc];
        GameObject* capture = m_grid[tr][tc];

        m_grid[tr][tc] = mover;
        m_grid[fr][fc] = nullptr;

        bool chk = InCheck(team);

        m_grid[fr][fc] = mover;   // 복원
        m_grid[tr][tc] = capture;
        return chk;
    }

    bool HasAnyLegalMove(int team)
    {
        for (int fr = 0; fr < BOARD; fr++)
            for (int fc = 0; fc < BOARD; fc++)
            {
                GameObject* p = m_grid[fr][fc];
                if (!p || p->teamID != team) continue;
                for (int tr = 0; tr < BOARD; tr++)
                    for (int tc = 0; tc < BOARD; tc++)
                        if (PseudoLegal(fr, fc, tr, tc) &&
                            !LeavesKingInCheck(fr, fc, tr, tc, team))
                            return true;
            }
        return false;
    }
};

extern "C" __declspec(dllexport) IGameLogic* CreateGameLogic()
{
    return new GameLogic();
}
