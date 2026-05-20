# Tutorial 03 — P2P 멀티플레이어 오목

> 이 튜토리얼은 Tutorial 01(오목)을 기반으로, 두 대의 컴퓨터(또는 두 개의 실행 창)에서 1:1로 통신하며 게임을 즐기는 방법을 다룹니다.
> Winsock2와 논블로킹 소켓을 이용해, 별도의 스레드 없이 매 프레임 `Update`에서 네트워크 상태를 체크합니다.
> Tutorial 01을 완료한 분이라면 충분히 따라올 수 있습니다. 함께 해봐요! 🌐

---

## 🚨 시작 전 확인 사항

```
✅ Tutorial 01 — 오목 만들기를 먼저 완료하세요.
   이 튜토리얼은 Tutorial 01의 GameLogic.cpp에 네트워크 기능을 추가합니다.

✅ 수정 파일은 딱 하나입니다.
   GameLogic/GameLogic.cpp
```

---

## 완성 미리보기

```
두 플레이어가 각자의 Engine.exe 창을 열고
한 명은 H 키 (방장), 한 명은 C 키 (참가자)를 눌러 연결합니다.
연결 후에는 번갈아 돌을 놓으며 오목을 즐깁니다.
한 쪽에서 5개를 이으면 양쪽 화면에 승자가 표시됩니다.
```

---

## 목차

- [Step 1. 네트워크의 기초: 방장(Host)과 참가자(Client)](#step-1-네트워크의-기초-방장host과-참가자client)
- [Step 2. Winsock2 세팅](#step-2-winsock2-세팅)
- [Step 3. 방장/참가자 모드 선택](#step-3-방장참가자-모드-선택)
- [Step 4. 패킷(데이터) 주고받기](#step-4-패킷데이터-주고받기)
- [Step 5. 두 개의 창으로 로컬 테스트하기](#step-5-두-개의-창으로-로컬-테스트하기)

---

## Step 1. 네트워크의 기초: 방장(Host)과 참가자(Client)

### 1-1. 서버-클라이언트 구조

P2P처럼 보이지만, 보드게임 통신에서는 한쪽이 **연결을 기다리는 쪽(서버 역할)** 이 되고, 다른 쪽이 **먼저 연결을 요청하는 쪽(클라이언트 역할)** 이 됩니다.

| 역할 | 키 | 동작 | 오목 색 |
|------|----|------|---------|
| 방장 (Host) | `H` | 소켓을 열고 상대방 접속을 기다립니다 | ● 흑돌 (선공) |
| 참가자 (Client) | `C` | 방장의 IP 주소로 연결을 시도합니다 | ○ 백돌 (후공) |

> **핵심 순서:** 방장이 먼저 `H`를 누르고, 참가자가 나중에 `C`를 눌러야 합니다.
> 순서가 바뀌면 참가자가 연결할 대상이 없어서 실패합니다.

### 1-2. 논블로킹(Non-blocking) 소켓이란?

일반 소켓 함수(`recv`, `accept`)는 데이터가 올 때까지 **무한 대기**합니다.
게임 루프 안에서 이 방식을 쓰면 화면이 얼어버립니다.

**논블로킹 소켓**은 데이터가 없으면 즉시 `-1`을 반환합니다.
덕분에 `Update` 함수가 매 프레임 자유롭게 돌면서 필요할 때만 데이터를 꺼낼 수 있습니다.

```
매 프레임 Update 호출
  │
  ├─ recv() 호출
  │    ├─ 데이터 있음  → 상대방 돌 위치 처리 ✅
  │    └─ 데이터 없음  → 즉시 반환, 게임 루프 계속 진행 ✅
  │
  └─ 화면 렌더링, 키 입력 처리 ...
```

논블로킹 설정은 `ioctlsocket` 한 줄이면 됩니다.

```cpp
u_long nonBlock = 1;
ioctlsocket(sock, FIONBIO, &nonBlock);  // FIONBIO = 논블로킹 모드 ON
```

---

## Step 2. Winsock2 세팅

### 2-1. 헤더와 라이브러리 추가

`GameLogic.cpp` 최상단, 다른 `#include`보다 **반드시 먼저** 추가합니다.

```cpp
// ⚠️ winsock2.h 는 windows.h 보다 먼저 포함해야 합니다.
//    순서가 바뀌면 구버전 winsock.h 와 충돌이 발생합니다.
#include <winsock2.h>
#include <ws2tcpip.h>   // inet_pton 사용
#pragma comment(lib, "ws2_32.lib")

#include "IGameLogic.h"
#include "IEngineAPI.h"
#include "GameObject.h"
#include "Transform.h"
#include "SpriteRenderer.h"
#include <vector>
#include <cstring>
```

### 2-2. Winsock 초기화 — OnLoad

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

    // Winsock 2.2 초기화
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        m_api->SetGameStatusText("Winsock 초기화 실패!", 5.0f);
        return;
    }

    m_api->SetGameStatusText("H = 방장 시작  /  C = 참가자 접속 (127.0.0.1)", 0.0f);
}
```

### 2-3. Winsock 정리 — OnUnload

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

> `WSAStartup`과 `WSACleanup`은 항상 쌍으로 호출합니다.
> DLL 핫리로드 시에도 소켓이 안전하게 정리됩니다.

---

## Step 3. 방장/참가자 모드 선택

### 3-1. 멤버 변수 추가

`GameLogic` 클래스에 네트워크 관련 변수를 추가합니다.

```cpp
class GameLogic : public IGameLogic
{
    IEngineAPI* m_api = nullptr;
    int  m_currentTurn = 1;

    // ── 오목 보드 ──────────────────────────────────────────────
    static const int BOARD_SIZE = 15;
    static const int TILE_SIZE  = 40;
    int  m_board[BOARD_SIZE][BOARD_SIZE] = {};
    bool m_gameOver = false;

    // ── 네트워크 ───────────────────────────────────────────────
    static const int PORT = 5000;

    enum NetState { IDLE, HOSTING, JOINING, CONNECTED };
    NetState m_netState   = IDLE;
    SOCKET   m_listenSock = INVALID_SOCKET;  // 방장 전용 대기 소켓
    SOCKET   m_sock       = INVALID_SOCKET;  // 실제 통신 소켓

    int  m_myColor  = 1;      // 1 = 흑(방장), 2 = 백(참가자)
    bool m_myTurn   = false;

    // 키 엣지 감지 (프레임마다 중복 입력 방지)
    bool m_prevKeyH = false;
    bool m_prevKeyC = false;
    // ──────────────────────────────────────────────────────────
```

### 3-2. 방장 소켓 시작 — StartHost()

```cpp
void StartHost()
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) return;

    // 대기 소켓을 논블로킹으로 설정
    u_long nonBlock = 1;
    ioctlsocket(s, FIONBIO, &nonBlock);

    sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;   // 모든 인터페이스에서 수신

    bind  (s, (sockaddr*)&addr, sizeof(addr));
    listen(s, 1);                        // 동시 대기 클라이언트 최대 1

    m_listenSock = s;
    m_netState   = HOSTING;
    m_api->SetGameStatusText("방장 대기 중 (포트 5000)... 상대방이 C 를 누르면 시작됩니다.", 0.0f);
}
```

### 3-3. 참가자 소켓 연결 — StartClient()

```cpp
void StartClient(const char* ip)
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) return;

    // 논블로킹 설정 후 connect() — WSAEWOULDBLOCK 은 "연결 중"이므로 정상입니다
    u_long nonBlock = 1;
    ioctlsocket(s, FIONBIO, &nonBlock);

    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port   = htons(PORT);
    inet_pton(AF_INET, ip, &serverAddr.sin_addr);

    connect(s, (sockaddr*)&serverAddr, sizeof(serverAddr));

    m_sock     = s;
    m_netState = JOINING;
    m_api->SetGameStatusText("방장에게 연결 중 (127.0.0.1:5000)...", 0.0f);
}
```

### 3-4. 연결 상태 감시 — HandleNetworkState()

`Update`에서 매 프레임 호출하여 연결 완료를 감지합니다.

```cpp
void HandleNetworkState()
{
    // ── 방장: 접속 수락 대기 ──────────────────────────────────
    if (m_netState == HOSTING)
    {
        sockaddr_in clientAddr = {};
        int addrLen = sizeof(clientAddr);
        SOCKET incoming = accept(m_listenSock, (sockaddr*)&clientAddr, &addrLen);
        if (incoming != INVALID_SOCKET)
        {
            u_long nonBlock = 1;
            ioctlsocket(incoming, FIONBIO, &nonBlock);

            closesocket(m_listenSock);      // 대기 소켓 불필요
            m_listenSock = INVALID_SOCKET;
            m_sock       = incoming;

            m_myColor  = 1;               // 방장 = 흑돌 (선공)
            m_myTurn   = true;
            m_netState = CONNECTED;
            m_api->SetGameStatusText("연결 성공! 흑돌(당신)부터 시작합니다.", 3.0f);
        }
    }

    // ── 참가자: 연결 완료 감지 ────────────────────────────────
    else if (m_netState == JOINING)
    {
        fd_set writeSet, exceptSet;
        FD_ZERO(&writeSet);   FD_SET(m_sock, &writeSet);
        FD_ZERO(&exceptSet);  FD_SET(m_sock, &exceptSet);
        timeval tv = { 0, 0 };   // 즉시 반환 (논블로킹)

        if (select(0, nullptr, &writeSet, &exceptSet, &tv) > 0)
        {
            if (FD_ISSET(m_sock, &exceptSet))
            {
                closesocket(m_sock);
                m_sock     = INVALID_SOCKET;
                m_netState = IDLE;
                m_api->SetGameStatusText("연결 실패. 방장이 H 를 먼저 눌렀는지 확인하세요.", 4.0f);
            }
            else
            {
                m_myColor  = 2;           // 참가자 = 백돌 (후공)
                m_myTurn   = false;
                m_netState = CONNECTED;
                m_api->SetGameStatusText("연결 성공! 백돌(당신), 방장 차례를 기다리세요.", 3.0f);
            }
        }
    }
}
```

### 3-5. 키 입력 처리 — HandleKeyInput()

```cpp
void HandleKeyInput()
{
    if (m_netState != IDLE) return;  // 이미 연결 시도 중이거나 완료됐으면 무시

    bool keyH = (GetAsyncKeyState('H') & 0x8000) != 0;
    bool keyC = (GetAsyncKeyState('C') & 0x8000) != 0;

    if (keyH && !m_prevKeyH) StartHost();
    if (keyC && !m_prevKeyC) StartClient("127.0.0.1");

    m_prevKeyH = keyH;
    m_prevKeyC = keyC;
}
```

> **다른 컴퓨터로 연결하려면?**
> `StartClient("127.0.0.1")` 의 IP를 방장 PC의 **로컬 IP**로 바꾸세요.
> 방장 PC에서 `ipconfig` 명령어를 실행하면 IPv4 주소를 확인할 수 있습니다.
> (예: `"192.168.0.5"`)

---

## Step 4. 패킷(데이터) 주고받기

### 4-1. 패킷 구조체 정의

`GameLogic.cpp` 상단, 클래스 선언 바깥에 추가합니다.

```cpp
// 돌 하나를 놓을 때 필요한 정보: 행과 열 (총 8바이트)
struct Packet
{
    int row;
    int col;
};
```

이 구조체를 `send()`로 보내고 `recv()`로 받습니다.
단 8바이트짜리 구조체라 네트워크 부하가 거의 없습니다.

### 4-2. 돌 놓기 + 전송 — OnObjectClicked

`OnObjectClicked` 전체를 다음 코드로 교체합니다.

```cpp
void OnObjectClicked(GameObject* obj) override
{
    if (!m_api || !obj) return;
    if (m_gameOver) return;

    // ── 연결 전이면 안내 ──────────────────────────────────────
    if (m_netState != CONNECTED)
    {
        m_api->SetGameStatusText("먼저 H(방장) 또는 C(참가자)를 눌러 연결하세요.", 2.0f);
        return;
    }

    // ── 내 차례가 아니면 무시 ─────────────────────────────────
    if (!m_myTurn)
    {
        m_api->SetGameStatusText("상대방 차례입니다. 기다려 주세요...", 1.0f);
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
        m_api->SetGameStatusText("이미 돌이 있는 칸입니다!", 1.0f);
        return;
    }

    // ── 보드 기록 + 텍스처 교체 ──────────────────────────────
    m_board[row][col] = m_myColor;
    const char* tex = (m_myColor == 1) ? "assets/black.png" : "assets/white.png";
    m_api->SetSpriteTexture(obj, tex);
    m_api->PlayAudio("assets/click.wav");

    // ── 상대방에게 좌표 전송 ──────────────────────────────────
    Packet pkt = { row, col };
    send(m_sock, (char*)&pkt, sizeof(pkt), 0);

    // ── 승리 판정 ─────────────────────────────────────────────
    if (CheckWin(row, col))
    {
        const char* msg = (m_myColor == 1) ? "Black Wins! (You)" : "White Wins! (You)";
        m_api->SetGameStatusText(msg, 10.0f);
        m_gameOver = true;
        return;
    }

    // ── 턴 넘기기 ─────────────────────────────────────────────
    m_myTurn      = false;
    m_currentTurn = (m_myColor == 1) ? 2 : 1;
    m_api->SetGameStatusText("상대방 차례입니다. 기다려 주세요...", 0.0f);
}
```

### 4-3. 상대방 돌 받기 — Update

`Update` 함수 전체를 다음 코드로 교체합니다.

```cpp
void Update(float dt, std::vector<GameObject*>& objects) override
{
    HandleNetworkState();
    HandleKeyInput();

    if (m_netState != CONNECTED) return;

    // ── 매 프레임 recv() 체크 (논블로킹 — 데이터 없으면 즉시 반환) ──
    Packet pkt = {};
    int received = recv(m_sock, (char*)&pkt, sizeof(pkt), 0);
    if (received != sizeof(pkt)) return;

    // ── 상대방 돌을 내 보드에 반영 ───────────────────────────
    int opponentColor = (m_myColor == 1) ? 2 : 1;
    m_board[pkt.row][pkt.col] = opponentColor;

    // objects 리스트에서 해당 타일을 찾아 텍스처 교체
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

    // ── 상대방이 이겼는지 판정 ────────────────────────────────
    if (CheckWin(pkt.row, pkt.col))
    {
        const char* msg = (opponentColor == 1) ? "Black Wins!" : "White Wins!";
        m_api->SetGameStatusText(msg, 10.0f);
        m_gameOver = true;
        return;
    }

    // ── 이제 내 차례 ──────────────────────────────────────────
    m_myTurn      = true;
    m_currentTurn = m_myColor;
    const char* turnMsg = (m_myColor == 1) ? "Black's Turn (You)" : "White's Turn (You)";
    m_api->SetGameStatusText(turnMsg, 0.0f);
}
```

> **왜 `received == sizeof(pkt)` 로 확인하나요?**
> TCP는 스트림 프로토콜이라, 이론적으로 8바이트가 여러 조각으로 올 수 있습니다.
> 이 튜토리얼은 로컬(127.0.0.1) 테스트를 가정하므로 항상 한 번에 도착합니다.
> 실제 인터넷 서비스라면 누적 버퍼(receive buffer)가 필요합니다.

### 4-4. 완성 코드 전체 붙여넣기

기존 `GameLogic.cpp`를 지우고 이 코드 전체를 붙여넣으세요.

```cpp
// ⚠️ winsock2.h 는 windows.h 보다 먼저 포함해야 합니다
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

    // ── 오목 보드 ──────────────────────────────────────────────
    static const int BOARD_SIZE = 15;
    static const int TILE_SIZE  = 40;
    int  m_board[BOARD_SIZE][BOARD_SIZE] = {};
    bool m_gameOver = false;

    // ── 네트워크 ───────────────────────────────────────────────
    static const int PORT = 5000;
    enum NetState { IDLE, HOSTING, JOINING, CONNECTED };
    NetState m_netState   = IDLE;
    SOCKET   m_listenSock = INVALID_SOCKET;
    SOCKET   m_sock       = INVALID_SOCKET;
    int  m_myColor  = 1;
    bool m_myTurn   = false;
    bool m_prevKeyH = false;
    bool m_prevKeyC = false;

    // ── 승리 판정 ──────────────────────────────────────────────
    bool CheckWin(int row, int col)
    {
        int color  = m_board[row][col];
        int dr[]   = { 0, 1, 1,  1 };
        int dc[]   = { 1, 0, 1, -1 };
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

    // ── 방장 소켓 시작 ────────────────────────────────────────
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
        m_api->SetGameStatusText("방장 대기 중 (포트 5000)... 상대방이 C 를 누르면 시작됩니다.", 0.0f);
    }

    // ── 참가자 소켓 연결 ──────────────────────────────────────
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
        m_api->SetGameStatusText("방장에게 연결 중 (127.0.0.1:5000)...", 0.0f);
    }

    // ── 연결 상태 감시 ────────────────────────────────────────
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
                m_api->SetGameStatusText("연결 성공! 흑돌(당신)부터 시작합니다.", 3.0f);
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
                    m_api->SetGameStatusText("연결 실패. 방장이 H 를 먼저 눌렀는지 확인하세요.", 4.0f);
                }
                else
                {
                    m_myColor  = 2;
                    m_myTurn   = false;
                    m_netState = CONNECTED;
                    m_api->SetGameStatusText("연결 성공! 백돌(당신), 방장 차례를 기다리세요.", 3.0f);
                }
            }
        }
    }

    // ── 키 입력 처리 ──────────────────────────────────────────
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
            m_api->SetGameStatusText("Winsock 초기화 실패!", 5.0f);
            return;
        }

        m_api->SetGameStatusText("H = 방장 시작  /  C = 참가자 접속 (127.0.0.1)", 0.0f);
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
            m_api->SetGameStatusText("먼저 H(방장) 또는 C(참가자)를 눌러 연결하세요.", 2.0f);
            return;
        }
        if (!m_myTurn)
        {
            m_api->SetGameStatusText("상대방 차례입니다. 기다려 주세요...", 1.0f);
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
            m_api->SetGameStatusText("이미 돌이 있는 칸입니다!", 1.0f);
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
        m_api->SetGameStatusText("상대방 차례입니다. 기다려 주세요...", 0.0f);
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

// DLL 익스포트
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

## Step 5. 두 개의 창으로 로컬 테스트하기

### 5-1. 빌드

1. `GameLogic/GameLogic_SDK.sln`을 Visual Studio 2022에서 엽니다.
2. **Ctrl+Shift+B** 로 빌드합니다.
3. 빌드 성공 시 `GameLogic.dll`이 `BoardEngine_SDK/` 루트에 생성됩니다.

```
BoardEngine_SDK/
├── Engine.exe
├── GameLogic.dll    ← 방금 빌드된 파일
├── shaders.hlsl
└── assets/
```

### 5-2. 두 개의 Engine.exe 실행

같은 `Engine.exe`를 **탐색기에서 두 번 클릭**해 두 개의 창을 엽니다.

```
[창 1 — 방장]          [창 2 — 참가자]
 Engine.exe              Engine.exe
     │                       │
     ▼                       ▼
  H 키 누름             (잠시 후) C 키 누름
```

> **중요:** 창 1에서 H를 먼저 누른 뒤, 창 2에서 C를 눌러야 합니다.

### 5-3. 게임 시작 순서

| 단계 | 창 1 (방장) | 창 2 (참가자) |
|------|------------|--------------|
| ① 방장 준비 | `H` 키 누름 → "방장 대기 중..." 표시 | — |
| ② 참가자 접속 | — | `C` 키 누름 → "연결 중..." 표시 |
| ③ 연결 완료 | "연결 성공! 흑돌(당신)부터 시작합니다." | "연결 성공! 백돌(당신), 방장 차례를..." |
| ④ 게임 진행 | 타일 클릭 → 흑돌 배치, 상대 창에 동기화 | 내 차례에 타일 클릭 → 백돌 배치 |
| ⑤ 게임 종료 | 5개 이으면 "Black Wins! (You)" | 5개 이으면 "White Wins! (You)" |

### 5-4. 문제 해결

| 증상 | 원인 | 해결책 |
|------|------|--------|
| "연결 실패" 메시지 | 참가자가 방장보다 먼저 C를 눌렀음 | 두 창을 재시작하고 순서대로 다시 시도 |
| 화면이 양쪽 다 멈춤 | 블로킹 소켓으로 잘못 빌드됨 | `ioctlsocket` 설정 누락 여부 확인 |
| 포트 5000 충돌 | 다른 프로그램이 이미 사용 중 | `PORT = 5001` 등 다른 번호로 변경 |
| 상대 돌이 안 보임 | `assets/black.png` 또는 `white.png` 없음 | Tutorial 01 Step 1 에셋 준비 확인 |

---

## 다음 단계

이 튜토리얼에서 구현한 통신 구조는 어떤 턴제 보드게임에도 동일하게 적용됩니다.

- 체스나 장기로 확장하려면 `Packet` 구조체에 `fromRow, fromCol, toRow, toCol`을 추가하세요.
- 실제 LAN 환경에서 플레이하려면 `StartClient("127.0.0.1")` 의 IP를 방장 PC의 IPv4 주소로 바꾸세요.
- 재대국 기능을 추가하려면 `R` 키 입력 시 양쪽 모두 보드를 초기화하는 패킷을 하나 더 정의하세요.

수고하셨습니다! 멀티플레이어 오목이 완성됐습니다. 🎉
