# Tutorial 03 — P2P Multiplayer Gomoku

> This tutorial builds on Tutorial 01 (Gomoku) to show you how two computers — or two windows on the same machine — can play against each other over a 1-on-1 connection.
> Using Winsock2 non-blocking sockets, the network state is checked every frame inside `Update` with no separate thread required.
> If you completed Tutorial 01, you have everything you need. Let's go! 🌐

---

## 🚨 Before You Start

```
✅ Complete Tutorial 01 — Building Gomoku first.
   This tutorial adds networking on top of Tutorial 01's GameLogic.cpp.

✅ There is exactly ONE file you will edit.
   GameLogic/GameLogic.cpp
```

---

## What You'll Build

```
Two players each open their own Engine.exe window.
One presses H (Host) and the other presses C (Client) to connect.
After connecting, they take turns placing stones.
When someone lines up 5 in a row, the winner is shown on both screens.
```

---

## Table of Contents

- [Step 1. Network Basics: Host and Client](#step-1-network-basics-host-and-client)
- [Step 2. Winsock2 Setup](#step-2-winsock2-setup)
- [Step 3. Selecting Host / Client Mode](#step-3-selecting-host--client-mode)
- [Step 4. Sending and Receiving Packets](#step-4-sending-and-receiving-packets)
- [Step 5. Local Test with Two Windows](#step-5-local-test-with-two-windows)

---

## Step 1. Network Basics: Host and Client

### 1-1. Server-Client Structure

It looks like P2P, but in board game networking one side **waits for a connection (server role)** while the other side **initiates the connection (client role)**.

| Role | Key | Behavior | Stone Color |
|------|-----|----------|-------------|
| Host | `H` | Opens a socket and waits for the opponent | ● Black (goes first) |
| Client | `C` | Connects to the host's IP address | ○ White (goes second) |

> **Key order:** The Host must press `H` first, then the Client presses `C`.
> If the order is reversed, the Client has no target to connect to and will fail.

### 1-2. What Is a Non-Blocking Socket?

Standard socket calls (`recv`, `accept`) **wait indefinitely** until data arrives.
Using them inside a game loop freezes the screen.

A **non-blocking socket** returns `-1` immediately when no data is available.
This lets `Update` run freely every frame and pull data only when it exists.

```
Update called every frame
  │
  ├─ recv() called
  │    ├─ Data available   → process opponent's stone position ✅
  │    └─ No data yet      → return immediately, game loop continues ✅
  │
  └─ Render frame, handle key input ...
```

One line switches a socket to non-blocking mode:

```cpp
u_long nonBlock = 1;
ioctlsocket(sock, FIONBIO, &nonBlock);  // FIONBIO = enable non-blocking mode
```

---

## Step 2. Winsock2 Setup

### 2-1. Add the Header and Library

Add these lines at the very top of `GameLogic.cpp`, **before all other `#include` directives**.

```cpp
// ⚠️ winsock2.h must be included before windows.h.
//    Reversing the order causes conflicts with the older winsock.h definitions.
#include <winsock2.h>
#include <ws2tcpip.h>   // for inet_pton
#pragma comment(lib, "ws2_32.lib")

#include "IGameLogic.h"
#include "IEngineAPI.h"
#include "GameObject.h"
#include "Transform.h"
#include "SpriteRenderer.h"
#include <vector>
#include <cstring>
```

### 2-2. Initialize Winsock — OnLoad

```cpp
void OnLoad(std::vector<GameObject*>& /*objects*/, IEngineAPI* api) override
{
    m_api         = api;
    m_currentTurn = 1;
    m_gameOver    = false;
    m_netState    = IDLE;
    m_myTurn      = false;
    m_prevKeyH    = false;
    m_prevKeyC    = false;
    memset(m_board, 0, sizeof(m_board));

    // Request Winsock 2.2
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        m_api->SetGameStatusText("Winsock init failed!", 5.0f);
        return;
    }

    m_api->SetGameStatusText("H = Start as Host  /  C = Join as Client (127.0.0.1)", 0.0f);
}
```

### 2-3. Clean Up Winsock — OnUnload

```cpp
void OnUnload() override
{
    if (m_sock != INVALID_SOCKET)
    {
        closesocket(m_sock);
        m_sock = INVALID_SOCKET;
    }
    if (m_listenSock != INVALID_SOCKET)
    {
        closesocket(m_listenSock);
        m_listenSock = INVALID_SOCKET;
    }
    WSACleanup();
}
```

> `WSAStartup` and `WSACleanup` must always be called as a matched pair.
> Sockets are properly released even across DLL hot-reloads.

---

## Step 3. Selecting Host / Client Mode

### 3-1. Add Member Variables

Add these network-related variables to the `GameLogic` class.

```cpp
class GameLogic : public IGameLogic
{
    IEngineAPI* m_api = nullptr;
    int  m_currentTurn = 1;

    // ── Gomoku board ────────────────────────────────────────────
    static const int BOARD_SIZE = 15;
    static const int TILE_SIZE  = 40;
    int  m_board[BOARD_SIZE][BOARD_SIZE] = {};
    bool m_gameOver = false;

    // ── Network ─────────────────────────────────────────────────
    static const int PORT = 5000;

    enum NetState { IDLE, HOSTING, JOINING, CONNECTED };
    NetState m_netState   = IDLE;
    SOCKET   m_listenSock = INVALID_SOCKET;  // host-only listen socket
    SOCKET   m_sock       = INVALID_SOCKET;  // active communication socket

    int  m_myColor  = 1;      // 1 = black (host), 2 = white (client)
    bool m_myTurn   = false;

    // edge-detection flags to prevent repeated key triggers per frame
    bool m_prevKeyH = false;
    bool m_prevKeyC = false;
    // ────────────────────────────────────────────────────────────
```

### 3-2. Open the Host Socket — StartHost()

```cpp
void StartHost()
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) return;

    // Set the listen socket to non-blocking as well
    u_long nonBlock = 1;
    ioctlsocket(s, FIONBIO, &nonBlock);

    sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;   // accept on all network interfaces

    bind  (s, (sockaddr*)&addr, sizeof(addr));
    listen(s, 1);                        // queue at most 1 pending client

    m_listenSock = s;
    m_netState   = HOSTING;
    m_api->SetGameStatusText("Waiting for opponent on port 5000... (they press C)", 0.0f);
}
```

### 3-3. Connect as Client — StartClient()

```cpp
void StartClient(const char* ip)
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) return;

    // Non-blocking connect — WSAEWOULDBLOCK means "connecting", not an error
    u_long nonBlock = 1;
    ioctlsocket(s, FIONBIO, &nonBlock);

    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port   = htons(PORT);
    inet_pton(AF_INET, ip, &serverAddr.sin_addr);

    connect(s, (sockaddr*)&serverAddr, sizeof(serverAddr));

    m_sock     = s;
    m_netState = JOINING;
    m_api->SetGameStatusText("Connecting to host (127.0.0.1:5000)...", 0.0f);
}
```

### 3-4. Watch Connection State — HandleNetworkState()

Call this every frame from `Update` to detect when the connection completes.

```cpp
void HandleNetworkState()
{
    // ── Host: wait for incoming connection ───────────────────────
    if (m_netState == HOSTING)
    {
        sockaddr_in clientAddr = {};
        int addrLen = sizeof(clientAddr);
        SOCKET incoming = accept(m_listenSock, (sockaddr*)&clientAddr, &addrLen);
        if (incoming != INVALID_SOCKET)
        {
            u_long nonBlock = 1;
            ioctlsocket(incoming, FIONBIO, &nonBlock);

            closesocket(m_listenSock);      // listen socket no longer needed
            m_listenSock = INVALID_SOCKET;
            m_sock       = incoming;

            m_myColor  = 1;               // host = black (first move)
            m_myTurn   = true;
            m_netState = CONNECTED;
            m_api->SetGameStatusText("Connected! You are Black — your move first.", 3.0f);
        }
    }

    // ── Client: detect connect() completion ──────────────────────
    else if (m_netState == JOINING)
    {
        fd_set writeSet, exceptSet;
        FD_ZERO(&writeSet);   FD_SET(m_sock, &writeSet);
        FD_ZERO(&exceptSet);  FD_SET(m_sock, &exceptSet);
        timeval tv = { 0, 0 };   // return immediately (non-blocking poll)

        if (select(0, nullptr, &writeSet, &exceptSet, &tv) > 0)
        {
            if (FD_ISSET(m_sock, &exceptSet))
            {
                closesocket(m_sock);
                m_sock     = INVALID_SOCKET;
                m_netState = IDLE;
                m_api->SetGameStatusText("Connection failed. Did the host press H first?", 4.0f);
            }
            else
            {
                m_myColor  = 2;           // client = white (second move)
                m_myTurn   = false;
                m_netState = CONNECTED;
                m_api->SetGameStatusText("Connected! You are White — wait for host's move.", 3.0f);
            }
        }
    }
}
```

### 3-5. Handle Key Input — HandleKeyInput()

```cpp
void HandleKeyInput()
{
    if (m_netState != IDLE) return;  // already connecting or connected

    bool keyH = (GetAsyncKeyState('H') & 0x8000) != 0;
    bool keyC = (GetAsyncKeyState('C') & 0x8000) != 0;

    if (keyH && !m_prevKeyH) StartHost();
    if (keyC && !m_prevKeyC) StartClient("127.0.0.1");

    m_prevKeyH = keyH;
    m_prevKeyC = keyC;
}
```

> **Connecting across two physical computers?**
> Change `"127.0.0.1"` in `StartClient(...)` to the host PC's **local IP address**.
> Run `ipconfig` on the host PC to find its IPv4 address (e.g., `"192.168.0.5"`).

---

## Step 4. Sending and Receiving Packets

### 4-1. Define the Packet Struct

Add this at the top of `GameLogic.cpp`, outside the class declaration.

```cpp
// All information needed for one stone placement: row and column (8 bytes total)
struct Packet
{
    int row;
    int col;
};
```

This struct is sent with `send()` and received with `recv()`.
At just 8 bytes, it adds virtually no network overhead.

### 4-2. Place Stone + Send — OnObjectClicked

Replace the entire `OnObjectClicked` function with the code below.

```cpp
void OnObjectClicked(GameObject* obj) override
{
    if (!m_api || !obj) return;
    if (m_gameOver) return;

    // ── Not connected yet — remind the player ────────────────────
    if (m_netState != CONNECTED)
    {
        m_api->SetGameStatusText("Press H (host) or C (client) to connect first.", 2.0f);
        return;
    }

    // ── Not our turn — ignore the click ─────────────────────────
    if (!m_myTurn)
    {
        m_api->SetGameStatusText("It's your opponent's turn. Please wait...", 1.0f);
        return;
    }

    if (obj->teamID != 0) return;

    auto* tr = obj->GetComponent<Transform>();
    if (!tr) return;

    int col = static_cast<int>(tr->x / TILE_SIZE);
    int row = static_cast<int>(tr->y / TILE_SIZE);
    if (col < 0 || col >= BOARD_SIZE || row < 0 || row >= BOARD_SIZE) return;

    if (m_board[row][col] != 0)
    {
        m_api->SetGameStatusText("That cell is already occupied!", 1.0f);
        return;
    }

    // ── Record stone + swap texture ──────────────────────────────
    m_board[row][col] = m_myColor;
    const char* tex = (m_myColor == 1) ? "assets/black.png" : "assets/white.png";
    m_api->SetSpriteTexture(obj, tex);
    m_api->PlayAudio("assets/click.wav");

    // ── Send coordinates to opponent ─────────────────────────────
    Packet pkt = { row, col };
    send(m_sock, (char*)&pkt, sizeof(pkt), 0);

    // ── Check win condition ──────────────────────────────────────
    if (CheckWin(row, col))
    {
        const char* msg = (m_myColor == 1) ? "Black Wins! (You)" : "White Wins! (You)";
        m_api->SetGameStatusText(msg, 10.0f);
        m_gameOver = true;
        return;
    }

    // ── Hand turn to opponent ────────────────────────────────────
    m_myTurn      = false;
    m_currentTurn = (m_myColor == 1) ? 2 : 1;
    m_api->SetGameStatusText("Opponent's turn. Please wait...", 0.0f);
}
```

### 4-3. Receive Opponent's Stone — Update

Replace the entire `Update` function with the code below.

```cpp
void Update(float dt, std::vector<GameObject*>& objects) override
{
    HandleNetworkState();
    HandleKeyInput();

    if (m_netState != CONNECTED) return;

    // ── Poll recv() every frame (non-blocking — returns instantly if no data) ──
    Packet pkt = {};
    int received = recv(m_sock, (char*)&pkt, sizeof(pkt), 0);
    if (received != sizeof(pkt)) return;

    // ── Apply opponent's stone to our board ──────────────────────
    int opponentColor = (m_myColor == 1) ? 2 : 1;
    m_board[pkt.row][pkt.col] = opponentColor;

    // Find the matching tile in the objects list and swap its texture
    for (auto* obj : objects)
    {
        auto* tr = obj->GetComponent<Transform>();
        if (!tr || obj->teamID != 0) continue;

        int c = static_cast<int>(tr->x / TILE_SIZE);
        int r = static_cast<int>(tr->y / TILE_SIZE);
        if (r == pkt.row && c == pkt.col)
        {
            const char* tex = (opponentColor == 1) ? "assets/black.png" : "assets/white.png";
            m_api->SetSpriteTexture(obj, tex);
            break;
        }
    }

    m_api->PlayAudio("assets/click.wav");

    // ── Check if the opponent just won ───────────────────────────
    if (CheckWin(pkt.row, pkt.col))
    {
        const char* msg = (opponentColor == 1) ? "Black Wins!" : "White Wins!";
        m_api->SetGameStatusText(msg, 10.0f);
        m_gameOver = true;
        return;
    }

    // ── Now it is our turn ───────────────────────────────────────
    m_myTurn      = true;
    m_currentTurn = m_myColor;
    const char* turnMsg = (m_myColor == 1) ? "Black's Turn (You)" : "White's Turn (You)";
    m_api->SetGameStatusText(turnMsg, 0.0f);
}
```

> **Why check `received == sizeof(pkt)`?**
> TCP is a stream protocol — in theory, 8 bytes could arrive in multiple fragments.
> For this tutorial's local (127.0.0.1) test, they always arrive in one piece.
> For a real internet service, you would need an accumulation buffer.

### 4-4. Full Code — Copy and Paste

Delete your existing `GameLogic.cpp` and replace it with the complete code below.

```cpp
// ⚠️ winsock2.h must come before any header that might include windows.h
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include "IGameLogic.h"
#include "IEngineAPI.h"
#include "GameObject.h"
#include "Transform.h"
#include "SpriteRenderer.h"
#include <vector>
#include <cstring>

struct Packet { int row; int col; };

class GameLogic : public IGameLogic
{
    IEngineAPI* m_api = nullptr;
    int  m_currentTurn = 1;

    // ── Gomoku board ────────────────────────────────────────────
    static const int BOARD_SIZE = 15;
    static const int TILE_SIZE  = 40;
    int  m_board[BOARD_SIZE][BOARD_SIZE] = {};
    bool m_gameOver = false;

    // ── Network ─────────────────────────────────────────────────
    static const int PORT = 5000;
    enum NetState { IDLE, HOSTING, JOINING, CONNECTED };
    NetState m_netState   = IDLE;
    SOCKET   m_listenSock = INVALID_SOCKET;
    SOCKET   m_sock       = INVALID_SOCKET;
    int  m_myColor  = 1;
    bool m_myTurn   = false;
    bool m_prevKeyH = false;
    bool m_prevKeyC = false;

    // ── Win condition ────────────────────────────────────────────
    bool CheckWin(int row, int col)
    {
        int color = m_board[row][col];
        int dr[]  = { 0, 1, 1,  1 };
        int dc[]  = { 1, 0, 1, -1 };
        for (int d = 0; d < 4; ++d)
        {
            int count = 1;
            for (int sign : { 1, -1 })
            {
                int r = row + dr[d] * sign;
                int c = col + dc[d] * sign;
                while (r >= 0 && r < BOARD_SIZE &&
                       c >= 0 && c < BOARD_SIZE &&
                       m_board[r][c] == color)
                {
                    ++count;
                    r += dr[d] * sign;
                    c += dc[d] * sign;
                }
            }
            if (count >= 5) return true;
        }
        return false;
    }

    // ── Open host socket ─────────────────────────────────────────
    void StartHost()
    {
        SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == INVALID_SOCKET) return;

        u_long nonBlock = 1;
        ioctlsocket(s, FIONBIO, &nonBlock);

        sockaddr_in addr = {};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(PORT);
        addr.sin_addr.s_addr = INADDR_ANY;
        bind  (s, (sockaddr*)&addr, sizeof(addr));
        listen(s, 1);

        m_listenSock = s;
        m_netState   = HOSTING;
        m_api->SetGameStatusText("Waiting for opponent on port 5000... (they press C)", 0.0f);
    }

    // ── Connect as client ────────────────────────────────────────
    void StartClient(const char* ip)
    {
        SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == INVALID_SOCKET) return;

        u_long nonBlock = 1;
        ioctlsocket(s, FIONBIO, &nonBlock);

        sockaddr_in serverAddr = {};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port   = htons(PORT);
        inet_pton(AF_INET, ip, &serverAddr.sin_addr);
        connect(s, (sockaddr*)&serverAddr, sizeof(serverAddr));

        m_sock     = s;
        m_netState = JOINING;
        m_api->SetGameStatusText("Connecting to host (127.0.0.1:5000)...", 0.0f);
    }

    // ── Watch connection state ───────────────────────────────────
    void HandleNetworkState()
    {
        if (m_netState == HOSTING)
        {
            sockaddr_in clientAddr = {};
            int addrLen = sizeof(clientAddr);
            SOCKET incoming = accept(m_listenSock, (sockaddr*)&clientAddr, &addrLen);
            if (incoming != INVALID_SOCKET)
            {
                u_long nonBlock = 1;
                ioctlsocket(incoming, FIONBIO, &nonBlock);
                closesocket(m_listenSock);
                m_listenSock = INVALID_SOCKET;
                m_sock       = incoming;
                m_myColor    = 1;
                m_myTurn     = true;
                m_netState   = CONNECTED;
                m_api->SetGameStatusText("Connected! You are Black — your move first.", 3.0f);
            }
        }
        else if (m_netState == JOINING)
        {
            fd_set writeSet, exceptSet;
            FD_ZERO(&writeSet);   FD_SET(m_sock, &writeSet);
            FD_ZERO(&exceptSet);  FD_SET(m_sock, &exceptSet);
            timeval tv = { 0, 0 };
            if (select(0, nullptr, &writeSet, &exceptSet, &tv) > 0)
            {
                if (FD_ISSET(m_sock, &exceptSet))
                {
                    closesocket(m_sock);
                    m_sock     = INVALID_SOCKET;
                    m_netState = IDLE;
                    m_api->SetGameStatusText("Connection failed. Did the host press H first?", 4.0f);
                }
                else
                {
                    m_myColor  = 2;
                    m_myTurn   = false;
                    m_netState = CONNECTED;
                    m_api->SetGameStatusText("Connected! You are White — wait for host's move.", 3.0f);
                }
            }
        }
    }

    // ── Key input ────────────────────────────────────────────────
    void HandleKeyInput()
    {
        if (m_netState != IDLE) return;
        bool keyH = (GetAsyncKeyState('H') & 0x8000) != 0;
        bool keyC = (GetAsyncKeyState('C') & 0x8000) != 0;
        if (keyH && !m_prevKeyH) StartHost();
        if (keyC && !m_prevKeyC) StartClient("127.0.0.1");
        m_prevKeyH = keyH;
        m_prevKeyC = keyC;
    }

public:

    void OnLoad(std::vector<GameObject*>& /*objects*/, IEngineAPI* api) override
    {
        m_api         = api;
        m_currentTurn = 1;
        m_gameOver    = false;
        m_netState    = IDLE;
        m_myTurn      = false;
        m_prevKeyH    = false;
        m_prevKeyC    = false;
        memset(m_board, 0, sizeof(m_board));

        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        {
            m_api->SetGameStatusText("Winsock init failed!", 5.0f);
            return;
        }

        m_api->SetGameStatusText("H = Start as Host  /  C = Join as Client (127.0.0.1)", 0.0f);
    }

    void Update(float dt, std::vector<GameObject*>& objects) override
    {
        HandleNetworkState();
        HandleKeyInput();

        if (m_netState != CONNECTED) return;

        Packet pkt = {};
        int received = recv(m_sock, (char*)&pkt, sizeof(pkt), 0);
        if (received != sizeof(pkt)) return;

        int opponentColor = (m_myColor == 1) ? 2 : 1;
        m_board[pkt.row][pkt.col] = opponentColor;

        for (auto* obj : objects)
        {
            auto* tr = obj->GetComponent<Transform>();
            if (!tr || obj->teamID != 0) continue;
            int c = static_cast<int>(tr->x / TILE_SIZE);
            int r = static_cast<int>(tr->y / TILE_SIZE);
            if (r == pkt.row && c == pkt.col)
            {
                const char* tex = (opponentColor == 1) ? "assets/black.png" : "assets/white.png";
                m_api->SetSpriteTexture(obj, tex);
                break;
            }
        }

        m_api->PlayAudio("assets/click.wav");

        if (CheckWin(pkt.row, pkt.col))
        {
            const char* msg = (opponentColor == 1) ? "Black Wins!" : "White Wins!";
            m_api->SetGameStatusText(msg, 10.0f);
            m_gameOver = true;
            return;
        }

        m_myTurn      = true;
        m_currentTurn = m_myColor;
        const char* turnMsg = (m_myColor == 1) ? "Black's Turn (You)" : "White's Turn (You)";
        m_api->SetGameStatusText(turnMsg, 0.0f);
    }

    void OnObjectClicked(GameObject* obj) override
    {
        if (!m_api || !obj) return;
        if (m_gameOver) return;

        if (m_netState != CONNECTED)
        {
            m_api->SetGameStatusText("Press H (host) or C (client) to connect first.", 2.0f);
            return;
        }
        if (!m_myTurn)
        {
            m_api->SetGameStatusText("It's your opponent's turn. Please wait...", 1.0f);
            return;
        }
        if (obj->teamID != 0) return;

        auto* tr = obj->GetComponent<Transform>();
        if (!tr) return;

        int col = static_cast<int>(tr->x / TILE_SIZE);
        int row = static_cast<int>(tr->y / TILE_SIZE);
        if (col < 0 || col >= BOARD_SIZE || row < 0 || row >= BOARD_SIZE) return;
        if (m_board[row][col] != 0)
        {
            m_api->SetGameStatusText("That cell is already occupied!", 1.0f);
            return;
        }

        m_board[row][col] = m_myColor;
        const char* tex = (m_myColor == 1) ? "assets/black.png" : "assets/white.png";
        m_api->SetSpriteTexture(obj, tex);
        m_api->PlayAudio("assets/click.wav");

        Packet pkt = { row, col };
        send(m_sock, (char*)&pkt, sizeof(pkt), 0);

        if (CheckWin(row, col))
        {
            const char* msg = (m_myColor == 1) ? "Black Wins! (You)" : "White Wins! (You)";
            m_api->SetGameStatusText(msg, 10.0f);
            m_gameOver = true;
            return;
        }

        m_myTurn      = false;
        m_currentTurn = (m_myColor == 1) ? 2 : 1;
        m_api->SetGameStatusText("Opponent's turn. Please wait...", 0.0f);
    }

    void OnUnload() override
    {
        if (m_sock != INVALID_SOCKET)
        {
            closesocket(m_sock);
            m_sock = INVALID_SOCKET;
        }
        if (m_listenSock != INVALID_SOCKET)
        {
            closesocket(m_listenSock);
            m_listenSock = INVALID_SOCKET;
        }
        WSACleanup();
    }
};

// DLL exports
extern "C" __declspec(dllexport) IGameLogic* CreateGameLogic()
{
    return new GameLogic();
}
extern "C" __declspec(dllexport) void DestroyGameLogic(IGameLogic* gl)
{
    delete gl;
}
```

---

## Step 5. Local Test with Two Windows

### 5-1. Build

1. Open `GameLogic/GameLogic_SDK.sln` in Visual Studio 2022.
2. Press **Ctrl+Shift+B** to build.
3. On success, `GameLogic.dll` appears in the `BoardEngine_SDK/` root folder.

```
BoardEngine_SDK/
├── Engine.exe
├── GameLogic.dll    ← freshly built
├── shaders.hlsl
└── assets/
```

### 5-2. Launch Two Engine.exe Windows

**Double-click the same `Engine.exe` twice** in File Explorer to open two separate windows.

```
[Window 1 — Host]       [Window 2 — Client]
  Engine.exe               Engine.exe
       │                        │
       ▼                        ▼
   press H               (a moment later) press C
```

> **Important:** Press H in Window 1 first, then C in Window 2.

### 5-3. Game Start Sequence

| Step | Window 1 (Host) | Window 2 (Client) |
|------|-----------------|-------------------|
| ① Host ready | Press `H` → "Waiting for opponent..." appears | — |
| ② Client joins | — | Press `C` → "Connecting..." appears |
| ③ Connected | "Connected! You are Black — your move first." | "Connected! You are White — wait for host's move." |
| ④ Playing | Click tile → black stone placed, synced to other window | Click tile on your turn → white stone placed |
| ⑤ Game over | Line up 5 → "Black Wins! (You)" | Line up 5 → "White Wins! (You)" |

### 5-4. Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| "Connection failed" message | Client pressed C before Host pressed H | Restart both windows and try again in the correct order |
| Both screens freeze | Socket was accidentally left in blocking mode | Check that `ioctlsocket` calls are present and correct |
| Port 5000 already in use | Another program occupies that port | Change `PORT = 5001` (or any unused port) in both copies |
| Opponent's stone not visible | `assets/black.png` or `white.png` missing | Revisit Tutorial 01 Step 1 to set up assets |

---

## Going Further

The communication structure built in this tutorial applies equally to any turn-based board game.

- For chess or shogi, extend `Packet` with `fromRow, fromCol, toRow, toCol`.
- To play across two physical machines, replace `"127.0.0.1"` in `StartClient(...)` with the host PC's IPv4 address.
- For a rematch feature, define a second packet type and reset both boards when `R` is pressed.

Well done — your multiplayer Gomoku is complete! 🎉
