// D3D12 보드게임 엔진의 핵심 클래스 선언 파일
#pragma once
// 빌드 모드 스위치. 0 = 에디터 모드(기본), 1 = 배포용 스탠드얼론 게임 모드.
// 1로 설정하면 ImGui 에디터 UI가 숨겨지고 씬·DLL이 자동으로 로드된다.
#define STANDALONE_MODE 0
#define NOMINMAX   // windows.h 의 min/max 매크로가 std::min/std::max 와 충돌하는 것을 방지
#include <windows.h>
#include <d3d12.h>
#include "d3dx12.h"       // D3D12 헬퍼 (UpdateSubresources, GetRequiredIntermediateSize 등)
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <vector>
#include <string>
#include <filesystem>
#include <chrono>

using Microsoft::WRL::ComPtr;

class GameObject; // 전방 선언 — Engine.h 를 포함하는 파일이 GameObject.h 를 몰라도 컴파일 가능
class IGameLogic; // 전방 선언 — IGameLogic.h 전체를 Engine.h 이용자에게 노출하지 않는다
#include "IEngineAPI.h"

// GPU에 전달할 정점 하나의 데이터 구조체.
// 셰이더 VSInput 의 메모리 레이아웃과 완전히 일치해야 한다.
struct Vertex
{
    DirectX::XMFLOAT3 pos; // 12바이트 (POSITION)
    DirectX::XMFLOAT2 uv;  //  8바이트 (TEXCOORD)
};

// 오브젝트별 Constant Buffer 슬롯.
// D3D12 CBV 주소는 D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT(256바이트) 단위로 정렬해야 한다.
// mvp(64바이트) + color(16바이트) + _pad(176바이트) = 256바이트.
struct ObjectCB
{
    DirectX::XMFLOAT4X4 mvp;    // 64 bytes — MVP 변환 행렬
    DirectX::XMFLOAT4   color;  // 16 bytes — SpriteRenderer::colorTint (tint 곱셈용)
    BYTE _pad[D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT
              - sizeof(DirectX::XMFLOAT4X4)
              - sizeof(DirectX::XMFLOAT4)];
};
static_assert(sizeof(ObjectCB) == D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT,
              "ObjectCB 크기가 256바이트가 아닙니다");

class Engine : public IEngineAPI
{
public:
    Engine(HINSTANCE hInstance);
    ~Engine();

    bool Initialize(int width, int height, const wchar_t* title);
    void Run();

    void SaveScene(const std::string& path);
    void LoadScene(const std::string& path);

    // GameLogic DLL 을 런타임에 로드/언로드한다.
    void LoadGameLogicDLL(const std::string& path);
    void UnloadGameLogicDLL();
    void CheckHotReload();

    // 외부에서 오브젝트를 추가한다. Engine 이 소유권을 가져 소멸자에서 삭제한다.
    void AddGameObject(GameObject* obj);

    // IEngineAPI 구현 — GameLogic DLL 이 호출하는 스크립팅 API
    GameObject* Instantiate(const std::string& name, float x, float y) override;
    void        AddSpriteRenderer(GameObject* obj, const std::string& texturePath) override;
    void        Destroy(GameObject* obj) override;
    GameObject* FindObjectByName(const std::string& name) override;
    void        PlayAudio(const std::string& filePath) override;
    void        SetSpriteTexture(GameObject* obj, const std::string& texturePath) override;
    void        SetGameStatusText(const std::string& text, float duration) override;

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    bool InitWindow(int width, int height, const wchar_t* title);
    bool InitD3D12();
    void Render();

    void CreateDevice();
    void CreateCommandObjects();
    void CreateSwapChain();
    void CreateRTVDescriptorHeap();
    void CreateFence();
    void CreateRootSignature();
    void CreatePipelineState();
    void CreateVertexBuffer();
    void CreateIndexBuffer();
    void CreateConstantBuffer();

    // 창 크기 기반의 직교 투영(Orthographic) View-Projection 행렬을 갱신한다.
    void UpdateCamera();

    // Dear ImGui 컨텍스트와 Win32/DX12 백엔드를 초기화한다.
    void InitImGui();

    // GPU 작업이 끝날 때까지 CPU를 블로킹하는 동기화 유틸리티
    void WaitForPreviousFrame();

    // 창 크기 변경 시 SwapChain 버퍼 및 RTV 재생성
    void OnResize(UINT width, UINT height);

    // 픽셀 스크린 좌표 → 월드 좌표 변환
    // 현재 카메라는 픽셀=월드 1:1 이므로 항등 변환. 카메라 패닝/줌 추가 시 역행렬 적용.
    void ScreenToWorld(int sx, int sy, float& wx, float& wy) const;

    // GetModuleFileNameA 로 구한 exe 디렉터리에 relativePath 를 결합해 절대 경로를 반환한다.
    // SaveScene/LoadScene/PlayAudio 등 모든 파일 I/O 에서 이 함수를 거쳐야
    // VS 디버그 실행 시 작업 디렉터리와 exe 경로 불일치 문제를 피할 수 있다.
    std::string GetAbsolutePath(const std::string& relativePath) const;

    // 마우스 클릭 위치에서 AABB 피킹 수행 (역순 순회 — 위에 그려진 오브젝트 우선)
    void TryPickObject();

    // 선택된 오브젝트의 Transform 을 마우스 델타만큼 이동
    void DragSelectedObject();

private:
    // 더블 버퍼링: 화면에 표시 중인 버퍼와 그리는 중인 버퍼를 분리
    static constexpr UINT FRAME_COUNT  = 2;
    // Constant Buffer 에 미리 할당할 최대 오브젝트 수 (타일 256개 + 기물 256개 여유)
    static constexpr UINT MAX_OBJECTS  = 256;

    // --- Window ---
    HINSTANCE   m_hInstance;
    HWND        m_hwnd;
    int         m_width;
    int         m_height;

    // --- DXGI ---
    // GPU 어댑터 열거와 SwapChain 생성을 담당하는 DXGI 팩토리
    ComPtr<IDXGIFactory6>   m_dxgiFactory;

    // 더블 버퍼링을 관리하는 스왑 체인 (Present 호출 시 버퍼를 교체)
    ComPtr<IDXGISwapChain3> m_swapChain;
    UINT                    m_currentBackBufferIndex = 0;

    // --- D3D12 Core ---
    ComPtr<ID3D12Device>                m_device;
    ComPtr<ID3D12CommandQueue>          m_commandQueue;
    ComPtr<ID3D12CommandAllocator>      m_commandAllocator;
    ComPtr<ID3D12GraphicsCommandList>   m_commandList;

    // --- Render Targets ---
    // RTV 슬롯 FRAME_COUNT 개를 담는 Descriptor Heap (GPU 리소스 주소록)
    ComPtr<ID3D12DescriptorHeap>    m_rtvHeap;

    // SwapChain 버퍼에 대응하는 실제 텍스처 리소스
    ComPtr<ID3D12Resource>          m_renderTargets[FRAME_COUNT];

    // 하드웨어마다 Descriptor 한 칸의 크기(바이트)가 다르므로 미리 조회
    UINT                            m_rtvDescriptorSize = 0;

    // --- 렌더링 파이프라인 ---
    // Root Signature: 셰이더가 접근할 GPU 리소스의 종류와 위치를 정의한 계약서
    ComPtr<ID3D12RootSignature>     m_rootSignature;

    // PSO: 셰이더 + 입력 레이아웃 + 래스터라이저 등 파이프라인 전체를 하나로 묶은 객체
    ComPtr<ID3D12PipelineState>     m_pipelineState;

    // --- Vertex Buffer ---
    ComPtr<ID3D12Resource>      m_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW    m_vertexBufferView = {};

    // --- Index Buffer ---
    // 중복 정점을 재사용해 사각형(삼각형 2개)을 그리기 위한 인덱스 버퍼
    ComPtr<ID3D12Resource>      m_indexBuffer;
    D3D12_INDEX_BUFFER_VIEW     m_indexBufferView = {};

    // --- Constant Buffer ---
    // 오브젝트별 ObjectCB 슬롯을 MAX_OBJECTS 개 연속으로 담는 버퍼.
    // 슬롯 i 의 GPU 주소 = base + i * sizeof(ObjectCB) (256바이트 간격).
    ComPtr<ID3D12Resource>      m_constantBuffer;
    void*                       m_cbMappedData = nullptr;

    // --- Camera ---
    // 2D 직교 투영 View * Projection 행렬. UpdateCamera() 에서 계산하고
    // Render() 에서 각 오브젝트의 MVP 계산에 사용한다.
    DirectX::XMFLOAT4X4         m_viewProj = {};

    // 카메라 상태: (m_cameraX, m_cameraY) = 뷰포트 좌상단의 월드 좌표.
    // m_cameraZoom = 배율 (1.0 = 100%, 2.0 = 200% 확대, 0.5 = 50% 축소).
    float   m_cameraX    = 0.0f;
    float   m_cameraY    = 0.0f;
    float   m_cameraZoom = 1.0f;

    // --- Scene ---
    // Engine 이 소유하는 오브젝트 목록. 소멸자에서 delete 로 해제된다.
    std::vector<GameObject*>    m_gameObjects;

    // --- ImGui ---
    bool   m_imguiInitialized  = false;
    int    m_selectedObjectIdx = 0;    // Inspector 에서 선택된 오브젝트 인덱스

    // --- Game Status Text (인게임 오버레이 알림) ---
    // SetGameStatusText() 로 설정하면 화면 상단 중앙에 m_statusTimer 초 동안 표시된다.
    std::string m_statusText;
    float       m_statusTimer  = 0.0f;

    // --- Play Mode ---
    bool   m_isPlaying         = false; // true: 게임 플레이 모드 (드래그 불가, 클릭 이벤트 전달)

    // --- 피킹 & 드래그 ---
    bool   m_isDragging        = false; // 마우스 드래그 중이면 true

    // --- Board Generator ---
    int    m_boardRows     = 8;
    int    m_boardCols     = 8;
    int    m_boardTileSize = 64;

    // --- Snap to Grid ---
    bool   m_snapToGrid = true;   // 드래그 시 격자 정렬 활성화
    int    m_snapSize   = 64;     // 격자 한 칸 크기(픽셀)

    // --- Deferred Destroy ---
    // Destroy() 요청을 즉시 삭제하지 않고 여기에 쌓아 프레임 끝에 일괄 처리한다.
    std::vector<GameObject*> m_pendingDestroy;

    // --- GameLogic DLL & Hot Reload ---
    HMODULE     m_gameLogicModule  = nullptr;
    IGameLogic* m_gameLogic        = nullptr;
    std::string m_dllPath;                                         // 원본 DLL 경로 (hot reload 추적 대상)
    std::filesystem::file_time_type m_dllLastWriteTime = {};       // 마지막으로 로드한 시점의 타임스탬프
    float       m_hotReloadTimer   = 0.0f;                         // 주기 검사 누적 시간(초)
    std::string m_dllTimestampStr  = "Not loaded";                 // ImGui 표시용 포맷 문자열

    // --- 프레임 타이밍 ---
    UINT64 m_lastFrameTick     = 0;    // GetTickCount64() 기준 이전 프레임 시각(ms)

    // --- CPU / GPU 동기화 ---
    // Fence: GPU가 특정 지점을 통과했는지 CPU가 확인할 수 있는 카운터
    ComPtr<ID3D12Fence>     m_fence;
    UINT64                  m_fenceValue = 0;

    // WaitForSingleObject 로 대기할 Win32 이벤트 핸들 (Auto-reset)
    HANDLE                  m_fenceEvent = nullptr;
};
