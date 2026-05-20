# Tutorial 01 — Building Gomoku (Five in a Row)

> By the end of this tutorial, you'll have a fully playable two-player Gomoku game in your hands.
> A basic understanding of C++ is all you need. Let's get started! 🎯

---

## 🚨 Absolute Rule: Do NOT Touch the Engine Core!

```
❌ Never modify any files inside the include/ folder.
   IEngineAPI.h, IGameLogic.h, GameObject.h — all read-only. Hands off.

✅ There is exactly ONE file you are allowed to edit.
   GameLogic/GameLogic.cpp
```

The SDK package does not include engine source code. Your only job is to write the game rules inside `GameLogic/GameLogic.cpp`. Modifying headers in `include/` will be overwritten on the next SDK update. Please respect this boundary.

---

## What You'll Build

```
Black (●) and White (○) alternate turns placing stones.
The first player to line up 5 stones in a row — horizontally,
vertically, or diagonally — wins the game.
"Black Wins!" or "White Wins!" is displayed at the top of the screen.
```

---

## Table of Contents

- [Step 1. Prerequisites — Scene and Asset Setup](#step-1-prerequisites--scene-and-asset-setup)
- [Step 2. Turn System](#step-2-turn-system)
- [Step 3. Placing Stones — Texture Swap Instead of Instantiate](#step-3-placing-stones--texture-swap-instead-of-instantiate)
- [Step 4. Win Condition Logic](#step-4-win-condition-logic)
- [Step 5. UI Feedback](#step-5-ui-feedback)
- [Step 6. Full Code — Copy and Paste](#step-6-full-code--copy-and-paste)
- [Step 7. Shipping the Game — Packaging Your Files](#step-7-shipping-the-game--packaging-your-files)

---

## Step 1. Prerequisites — Scene and Asset Setup

### 1-1. Copy Asset Files

Prepare three image files and one sound effect for Gomoku.
Place them in the following directory:

```
assets/
├── black.png     ← Black stone image (e.g., a 64×64 PNG with a black circle)
├── white.png     ← White stone image (e.g., a 64×64 PNG with a white circle)
└── click.wav     ← Stone placement sound effect (a short click)
```

> **No images yet?**  
> No problem. In Step 3 we'll show you an alternative using `colorTint` to visually
> distinguish black and white stones — no image files required.

### 1-2. Create the Board Scene

1. Double-click `Engine.exe` in the SDK root folder to launch the editor.
   The SDK build of `Engine.exe` runs in editor mode — no build step required.

2. (A blank viewport is normal at this point — no scene has been loaded yet.)

3. In the **Board Generator** panel, set the following values:

   | Field | Value | Reason |
   |-------|-------|--------|
   | Rows | 15 | Standard Gomoku board size |
   | Columns | 15 | Standard Gomoku board size |
   | Tile Size | 40 | 15 × 40 = 600 pixels, fits comfortably on screen |
   | Snap to Grid | ✅ | Keeps dragged tiles aligned |
   | Snap Size | 40 | Must match Tile Size |

   > **Quick check:** 15 × 15 = 225 tiles. The engine's MAX_OBJECTS limit is 256 — we're safe. ✓

4. Click the **Generate Board** button. A 15×15 grid should appear on screen.

5. From the menu bar, go to **File → Save Scene**.
   - The scene is saved as `assets/scene.json`.

### 1-3. Verify the GameLogic DLL Connection

- Click **GameLogic → Load GameLogic.dll** in the menu bar.
- Press the **[ Play ]** button to enter Play Mode.
- Click a tile. If the default template responds, you're all set.

---

## Step 2. Turn System

Open `GameLogic.cpp` and add the Gomoku-specific member variables to the `GameLogic` class.

The existing template already has `m_currentTurn` and `m_api`. Add the new variables shown below:

```cpp
class GameLogic : public IGameLogic
{
    IEngineAPI* m_api         = nullptr;
    int  m_currentTurn        = 1;       // 1 = Black, 2 = White

    // ── Gomoku-specific variables ─────────────────────────────
    static const int BOARD_SIZE = 15;    // Must match Board Generator Rows/Cols
    static const int TILE_SIZE  = 40;    // Must match Board Generator Tile Size (pixels)
    // ─────────────────────────────────────────────────────────────

    int  m_board[BOARD_SIZE][BOARD_SIZE] = {};  // 0 = empty, 1 = black, 2 = white
    bool m_gameOver = false;
```

> `BOARD_SIZE` and `TILE_SIZE` **must match** the values you set in the Board Generator.
> If you used an 8×8 board, set `BOARD_SIZE = 8` and `TILE_SIZE = 64`.

Update `OnLoad` to initialize the game state:

```cpp
void OnLoad(std::vector<GameObject*>& /*objects*/, IEngineAPI* api) override
{
    m_api         = api;
    m_currentTurn = 1;
    m_gameOver    = false;

    // Reset the board (fill with zeros)
    memset(m_board, 0, sizeof(m_board));

    m_api->SetGameStatusText("Black's Turn", 3.0f);
}
```

---

## Step 3. Placing Stones — Texture Swap Instead of Instantiate

In Gomoku, instead of spawning a new object for every stone, we **replace the tile's texture** with a stone image. This approach doesn't consume any MAX_OBJECTS budget, and the code is much cleaner.

Replace the `OnObjectClicked` function with the following:

```cpp
void OnObjectClicked(GameObject* obj) override
{
    if (!m_api || !obj) return;

    // ── Ignore clicks after the game ends ────────────────────
    if (m_gameOver) return;

    // ── Only accept empty tiles (teamID == 0) ─────────────────
    // Piece objects (teamID != 0) are not used in Gomoku.
    if (obj->teamID != 0) return;

    auto* tr = obj->GetComponent<Transform>();
    if (!tr) return;

    // ── Convert click position to board grid coordinates ──────
    // Tile center X = col * TILE_SIZE + TILE_SIZE/2, so
    // (int)(tr->x / TILE_SIZE) gives us the column index.
    int col = static_cast<int>(tr->x / TILE_SIZE);
    int row = static_cast<int>(tr->y / TILE_SIZE);

    // Bounds check
    if (col < 0 || col >= BOARD_SIZE || row < 0 || row >= BOARD_SIZE) return;

    // ── Reject occupied cells ─────────────────────────────────
    if (m_board[row][col] != 0)
    {
        m_api->SetGameStatusText("Already occupied!", 1.0f);
        return;
    }

    // ── Record the move ───────────────────────────────────────
    m_board[row][col] = m_currentTurn;

    // ── Swap the tile texture to display the stone ────────────
    if (m_currentTurn == 1)
        m_api->SetSpriteTexture(obj, "assets/black.png");  // Black stone
    else
        m_api->SetSpriteTexture(obj, "assets/white.png");  // White stone

    // ── Play placement sound ──────────────────────────────────
    m_api->PlayAudio("assets/click.wav");

    // ── Check win condition (implemented in Step 4) ───────────
    if (CheckWin(row, col, m_currentTurn))
    {
        std::string winner = (m_currentTurn == 1) ? "Black Wins!" : "White Wins!";
        m_api->SetGameStatusText(winner, 30.0f);
        m_gameOver = true;
        return;
    }

    // ── Switch turns ──────────────────────────────────────────
    m_currentTurn = (m_currentTurn == 1) ? 2 : 1;
    std::string turnMsg = (m_currentTurn == 1) ? "Black's Turn" : "White's Turn";
    m_api->SetGameStatusText(turnMsg, 3.0f);
}
```

> **Alternative: No image files? Use colorTint instead.**
>
> ```cpp
> // Color-tint version — no image files required
> auto* sr = obj->GetComponent<SpriteRenderer>();
> if (sr)
> {
>     sr->colorTint = (m_currentTurn == 1)
>         ? DirectX::XMFLOAT4{0.05f, 0.05f, 0.05f, 1.0f}  // Near-black
>         : DirectX::XMFLOAT4{0.95f, 0.95f, 0.95f, 1.0f}; // Near-white
> }
> ```

---

## Step 4. Win Condition Logic

Add the following function to the `private:` section of the `GameLogic` class.

The winning condition in Gomoku is **5 or more stones in a row** — horizontally, vertically, or diagonally. By checking 4 direction vectors and scanning both ways from the newly placed stone, all 8 directions are covered.

```cpp
private:
    // ----------------------------------------------------------
    // Five-in-a-row win check.
    // Call this immediately after placing a stone at (row, col).
    // Returns true if `player` has 5 or more consecutive stones
    // in any of the 4 axis directions.
    // ----------------------------------------------------------
    bool CheckWin(int row, int col, int player)
    {
        // Direction vectors (dr = row delta, dc = col delta)
        // Scanning both forward and backward covers all 8 directions.
        const int dr[] = { 0,  1,  1,  1 };
        const int dc[] = { 1,  0,  1, -1 };

        for (int d = 0; d < 4; d++)
        {
            int count = 1; // Count the stone just placed

            // Scan in the positive direction
            for (int i = 1; i < 5; i++)
            {
                int r = row + dr[d] * i;
                int c = col + dc[d] * i;
                if (r < 0 || r >= BOARD_SIZE || c < 0 || c >= BOARD_SIZE) break;
                if (m_board[r][c] != player) break;
                count++;
            }

            // Scan in the negative direction
            for (int i = 1; i < 5; i++)
            {
                int r = row - dr[d] * i;
                int c = col - dc[d] * i;
                if (r < 0 || r >= BOARD_SIZE || c < 0 || c >= BOARD_SIZE) break;
                if (m_board[r][c] != player) break;
                count++;
            }

            if (count >= 5) return true; // Five in a row!
        }

        return false; // No winner yet
    }
```

> **Algorithm walkthrough**
>
> ```
> d=0: (dr=0, dc=1)  → Horizontal  ─
> d=1: (dr=1, dc=0)  → Vertical    │
> d=2: (dr=1, dc=1)  → Diagonal    ↘
> d=3: (dr=1, dc=-1) → Diagonal    ↙
>
> For each direction, forward + backward scans are summed,
> so both sides of the newly placed stone are considered.
>
> Example (d=0, horizontal): ●●●●[new]●  → count = 1+2+3 = 6 ≥ 5 → Win!
> ```

---

## Step 5. UI Feedback

Win detection and turn announcements are already wired into the Step 3 code.
`SetGameStatusText` displays the message at the top-center of the screen.

| Situation | Message Displayed | Duration |
|-----------|-------------------|----------|
| Game start / DLL reload | `"Black's Turn"` | 3 sec |
| Turn change | `"Black's Turn"` / `"White's Turn"` | 3 sec |
| Clicking an occupied cell | `"Already occupied!"` | 1 sec |
| Black wins | `"Black Wins!"` | 30 sec (game over) |
| White wins | `"White Wins!"` | 30 sec (game over) |

To restart the game, simply click **GameLogic → Reload Now** in the menu bar. This re-invokes `OnLoad`, which resets the board state and starts a fresh game.

---

## Step 6. Full Code — Copy and Paste

Replace the entire contents of `GameLogic/GameLogic.cpp` with the code below.

```cpp
// GameLogic.cpp — Gomoku (Five in a Row) — Two-Player Implementation
// Do NOT modify anything inside the Engine/ folder. Edit only this file.

#include <windows.h>
#include <string>
#include <cstring>    // memset
#include "IGameLogic.h"
#include "IEngineAPI.h"
#include "GameObject.h"
#include "Transform.h"
#include "SpriteRenderer.h"

static void Log(const std::string& msg)
{
    OutputDebugStringA(("[Gomoku] " + msg + "\n").c_str());
}

class GameLogic : public IGameLogic
{
    IEngineAPI* m_api         = nullptr;
    int  m_currentTurn        = 1;       // 1 = Black, 2 = White

    // ── Board config — must match Board Generator settings ────
    static const int BOARD_SIZE = 15;   // Rows / Cols
    static const int TILE_SIZE  = 40;   // Tile Size (pixels)
    // ─────────────────────────────────────────────────────────

    int  m_board[BOARD_SIZE][BOARD_SIZE] = {};  // 0 = empty, 1 = black, 2 = white
    bool m_gameOver = false;

public:
    void OnLoad(std::vector<GameObject*>& /*objects*/, IEngineAPI* api) override
    {
        m_api         = api;
        m_currentTurn = 1;
        m_gameOver    = false;
        memset(m_board, 0, sizeof(m_board));

        m_api->SetGameStatusText("Black's Turn", 3.0f);
        Log("Gomoku started. Black goes first.");
    }

    void Update(float /*dt*/, std::vector<GameObject*>& /*objects*/) override
    {
        // Nothing to update per-frame for basic Gomoku.
        // Add AI logic here if you want a single-player mode.
    }

    void OnUnload() override {}

    void OnObjectClicked(GameObject* obj) override
    {
        if (!m_api || !obj) return;
        if (m_gameOver) return;
        if (obj->teamID != 0) return;   // Only accept empty tiles (teamID == 0)

        auto* tr = obj->GetComponent<Transform>();
        if (!tr) return;

        int col = static_cast<int>(tr->x / TILE_SIZE);
        int row = static_cast<int>(tr->y / TILE_SIZE);
        if (col < 0 || col >= BOARD_SIZE || row < 0 || row >= BOARD_SIZE) return;

        if (m_board[row][col] != 0)
        {
            m_api->SetGameStatusText("Already occupied!", 1.0f);
            return;
        }

        // ── Place the stone ───────────────────────────────────
        m_board[row][col] = m_currentTurn;

        if (m_currentTurn == 1)
            m_api->SetSpriteTexture(obj, "assets/black.png");
        else
            m_api->SetSpriteTexture(obj, "assets/white.png");

        m_api->PlayAudio("assets/click.wav");

        Log((m_currentTurn == 1 ? "Black" : "White") +
            std::string(" placed at (") + std::to_string(row) + "," +
            std::to_string(col) + ")");

        // ── Check win condition ───────────────────────────────
        if (CheckWin(row, col, m_currentTurn))
        {
            std::string winner = (m_currentTurn == 1) ? "Black Wins!" : "White Wins!";
            m_api->SetGameStatusText(winner, 30.0f);
            m_gameOver = true;
            Log(winner);
            return;
        }

        // ── Switch turns ──────────────────────────────────────
        m_currentTurn = (m_currentTurn == 1) ? 2 : 1;
        m_api->SetGameStatusText(
            (m_currentTurn == 1) ? "Black's Turn" : "White's Turn", 3.0f);
    }

private:
    // Checks whether `player` has 5 or more stones in a row
    // along any of the 4 axis directions, centered on (row, col).
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

### Build and Test

1. Open `GameLogic/GameLogic_SDK.sln` in Visual Studio 2022.
2. **Build → Build Solution** (Ctrl+Shift+B). `GameLogic.dll` is placed next to `Engine.exe` automatically.
3. Run `Engine.exe` and click **GameLogic → Load GameLogic.dll**.
4. Press **[ Play ]** in the menu bar to enter Play Mode.
5. Click tiles and verify that black and white stones alternate correctly.
6. Line up 5 stones in a row and confirm the win message appears.

---

## Step 7. Shipping the Game — Packaging Your Files

The SDK does not include `Engine.h`, so you cannot rebuild the engine binary.
Instead, bundle the files already present in the SDK and ship them as-is.

### 7-1. Distribution Checklist

Verify that all four items below are present in your delivery package:

```
MyGomokuGame/
├── Engine.exe          ← The Engine.exe from your SDK (no rebuild needed)
├── GameLogic.dll       ← Your build output from Step 6
├── shaders.hlsl        ← 🚨 Required at runtime — omitting this crashes on launch
└── assets/
    ├── scene.json      ← The 15×15 board scene saved in Step 1
    ├── black.png       ← Black stone image
    ├── white.png       ← White stone image
    └── click.wav       ← Sound effect
```

### 7-2. Package and Deliver

Place all four items in one folder, compress it into a `.zip`, and share it — you're done!

```
MyGomokuGame.zip
└── MyGomokuGame/
    ├── Engine.exe
    ├── GameLogic.dll
    ├── shaders.hlsl
    └── assets/
        ├── scene.json
        ├── black.png
        ├── white.png
        └── click.wav
```

> **For the end player:** Unzip, double-click `Engine.exe`, and the game launches immediately.
> No installer required.

### 7-3. Want a Cleaner Standalone Build?

If you need a version with no editor UI visible to players, ask the engine developer to provide
a `STANDALONE_MODE 1` build of `Engine.exe` and swap it into your package.

---

## Going Further — Level Up Your Gomoku

Once you've completed the tutorial, try adding these features — all inside `GameLogic.cpp`:

| Feature | Hint |
|---------|------|
| **Draw detection** | After every move, check if all cells are filled with no winner → display `"Draw!"` |
| **Renju rule (black forbidden moves)** | If black would form a double-three or double-four, display `"Forbidden move!"` and reject the move |
| **Restart on keypress** | In `Update()`, detect `GetAsyncKeyState('R')` and re-run the same initialization logic as `OnLoad` |
| **AI opponent** | In `Update()`, when `m_currentTurn == 2`, scan the board and pick the best available cell automatically |
| **Stone counter** | Use `std::to_string` to display `"Black: 5 stones"` via `SetGameStatusText` |

---

> Stuck on anything, or ready for the next challenge?
> Next up: **Tutorial 02 — Building Chess**. See you there!
