// D3D12 보드게임 엔진의 초기화 및 메인 루프 구현 파일
#include "Engine.h"
#include <d3dcompiler.h>
#include <cassert>
#include <vector>

#include "GameObject.h"
#include "ResourceManager.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
#include "Transform.h"
#include "SpriteRenderer.h"
#include "Label.h"
#include "LuaManager.h"
#include "InputManager.h"
#include "IGameLogic.h"
#include <fstream>
#include <ctime>
#include <algorithm>
#include <cmath>
#pragma warning(push, 0)
#include "nlohmann/json.hpp"
#pragma warning(pop)
// WndProcHandler 는 헤더 내부에서 #if 0 으로 막혀 있어 직접 전방 선언이 필요하다.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using namespace DirectX;

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "winmm.lib")
#include <mmsystem.h>

// =====================================================================
// ImGui SRV 콜백 — ResourceManager 의 선형 할당기에 위임한다.
// =====================================================================

static void ImGuiSrvAllocFn(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* cpu,
                             D3D12_GPU_DESCRIPTOR_HANDLE* gpu)
{
    ResourceManager::Instance().AllocateDescriptor(cpu, gpu);
}

static void ImGuiSrvFreeFn(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu,
                            D3D12_GPU_DESCRIPTOR_HANDLE gpu)
{
    ResourceManager::Instance().FreeDescriptor(cpu, gpu);
}

// std::filesystem::file_time_type 을 "YYYY-MM-DD HH:MM:SS" 로컬 타임 문자열로 변환한다.
// file_time_type 클럭과 system_clock 의 now() 차를 이용해 C++17 에서 호환 변환한다.
static std::string FormatFileTime(std::filesystem::file_time_type ft)
{
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ft - std::filesystem::file_time_type::clock::now()
        + std::chrono::system_clock::now());
    std::time_t t = std::chrono::system_clock::to_time_t(sctp);
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

// =====================================================================
// 생성자 / 소멸자
// =====================================================================

Engine::Engine(HINSTANCE hInstance)
    : m_hInstance(hInstance)
    , m_hwnd(nullptr)
    , m_width(0)
    , m_height(0)
{
}

Engine::~Engine()
{
    // ── [DLL] GameLogic 언로드 (오브젝트 삭제 전 — OnUnload 에서 오브젝트 접근 가능)
    UnloadGameLogicDLL();

    // ── [0] 씬 오브젝트 해제 ─────────────────────────────────────────
    for (auto* obj : m_gameObjects)
        delete obj;
    m_gameObjects.clear();

    // ── [1] GPU 플러시 ───────────────────────────────────────────────
    // ComPtr 소멸자가 D3D12 리소스를 Release() 하기 전에
    // GPU가 해당 리소스 참조를 완전히 마쳤음을 보장한다.
    // 이를 생략하면 GPU가 아직 쓰는 버퍼가 해제되어 크래시가 발생한다.
    if (m_fence)
        WaitForPreviousFrame();

    // ── [ImGui] 백엔드 종료 ──────────────────────────────────────────
    // GPU 플러시 이후, D3D12 리소스 해제 이전에 호출해야 한다.
    if (m_imguiInitialized)
    {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        m_imguiInitialized = false;
    }

    // ── [Lua] Lua 스테이트 해제 (ScriptComponent 가 이미 삭제된 후) ──
    LuaManager::Instance().Shutdown();

    // ── [ResourceManager] 텍스처·SRV 힙 해제 ────────────────────────
    ResourceManager::Instance().Shutdown();

    // ── [2] Win32 핸들 해제 ──────────────────────────────────────────
    // HANDLE 은 COM 이 아니므로 ComPtr 이 관리하지 않는다. 반드시 수동으로 닫아야 한다.
    if (m_fenceEvent)
    {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }

    // ── [3] D3D12 / DXGI COM 인터페이스 명시적 해제 ─────────────────
    // ComPtr 은 소멸 시 자동 Release() 하지만, 해제 순서를 명시하면
    // D3D12 Debug Layer 의 "live object" 경고를 피할 수 있다.
    // 원칙: 자식(하위) 객체를 부모(상위) 객체보다 먼저 해제한다.
    m_pipelineState.Reset();     // PSO 는 RootSignature 를 참조하므로 먼저 해제
    m_commandList.Reset();
    m_commandAllocator.Reset();
    m_commandQueue.Reset();
    m_fence.Reset();
    for (auto& rt : m_renderTargets)
        rt.Reset();
    m_rtvHeap.Reset();
    m_vertexBuffer.Reset();
    m_indexBuffer.Reset();
    // 영속 Map 을 사용하므로 리소스 해제 전에 반드시 Unmap 호출
    if (m_cbMappedData)
    {
        m_constantBuffer->Unmap(0, nullptr);
        m_cbMappedData = nullptr;
    }
    m_constantBuffer.Reset();
    m_rootSignature.Reset();
    m_device.Reset();
    m_swapChain.Reset();
    m_dxgiFactory.Reset();
}

// =====================================================================
// 공개 메서드
// =====================================================================

bool Engine::Initialize(int width, int height, const wchar_t* title)
{
    m_width  = width;
    m_height = height;

    if (!InitWindow(width, height, title))
        return false;

    if (!InitD3D12())
        return false;

    return true;
}

void Engine::Run()
{
    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            Render();
        }
    }
}

// =====================================================================
// 윈도우 초기화
// =====================================================================

bool Engine::InitWindow(int width, int height, const wchar_t* title)
{
    WNDCLASSEX wc    = {};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = m_hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"D3D12EngineClass";

    if (!RegisterClassEx(&wc))
        return false;

    RECT rect = { 0, 0, width, height };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    m_hwnd = CreateWindowEx(
        0,
        L"D3D12EngineClass",
        title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right  - rect.left,
        rect.bottom - rect.top,
        nullptr,
        nullptr,
        m_hInstance,
        this
    );

    if (!m_hwnd)
        return false;

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
    return true;
}

// =====================================================================
// D3D12 초기화
// =====================================================================

bool Engine::InitD3D12()
{
    CreateDevice();
    CreateCommandObjects();
    CreateSwapChain();
    CreateRTVDescriptorHeap();
    CreateFence();
    CreateRootSignature();
    CreatePipelineState();
    CreateVertexBuffer();
    CreateIndexBuffer();
    CreateConstantBuffer();
    ResourceManager::Instance().Init(m_device.Get(), m_commandQueue.Get());
    {
        // exe 디렉터리를 ResourceManager 에 주입 — 이후 상대 경로 텍스처를 올바르게 해석한다.
        char buf[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, buf, MAX_PATH);
        ResourceManager::Instance().SetBasePath(
            std::filesystem::path(buf).parent_path().string());
    }
    LuaManager::Instance().Init();
    UpdateCamera();
    InitImGui();
#if STANDALONE_MODE
    // 씬을 먼저 로드해 오브젝트를 채운 뒤 DLL OnLoad 가 씬을 볼 수 있게 한다.
    LoadScene("assets/scene.json");
    LoadGameLogicDLL("GameLogic.dll");
    m_isPlaying = true;
#endif
    return true;
}

void Engine::CreateDevice()
{
#ifdef _DEBUG
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        debugController->EnableDebugLayer();
#endif

    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&m_dxgiFactory));
    assert(SUCCEEDED(hr));

    ComPtr<IDXGIAdapter1> adapter;
    m_dxgiFactory->EnumAdapterByGpuPreference(
        0,
        DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
        IID_PPV_ARGS(&adapter)
    );

    hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device));

    if (FAILED(hr))
    {
        ComPtr<IDXGIAdapter> warpAdapter;
        m_dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter));
        hr = D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device));
        assert(SUCCEEDED(hr));
    }
}

void Engine::CreateCommandObjects()
{
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    HRESULT hr = m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue));
    assert(SUCCEEDED(hr));

    hr = m_device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&m_commandAllocator)
    );
    assert(SUCCEEDED(hr));

    hr = m_device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_commandAllocator.Get(),
        nullptr,
        IID_PPV_ARGS(&m_commandList)
    );
    assert(SUCCEEDED(hr));

    m_commandList->Close();
}

void Engine::CreateSwapChain()
{
    // SwapChain: GPU가 그린 결과물을 화면에 교대로 표시하는 더블 버퍼 관리자.
    // CreateSwapChainForHwnd 는 Command Queue 에 직접 연결되어
    // Present() 호출 시 해당 Queue 가 처리를 완료한 뒤 버퍼를 교체한다.
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.BufferCount      = FRAME_COUNT;
    desc.Width            = static_cast<UINT>(m_width);
    desc.Height           = static_cast<UINT>(m_height);
    desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain1;
    HRESULT hr = m_dxgiFactory->CreateSwapChainForHwnd(
        m_commandQueue.Get(),
        m_hwnd,
        &desc,
        nullptr,
        nullptr,
        &swapChain1
    );
    assert(SUCCEEDED(hr));

    // Alt+Enter 전체화면 전환 비활성화
    m_dxgiFactory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);

    swapChain1.As(&m_swapChain);
    m_currentBackBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void Engine::CreateRTVDescriptorHeap()
{
    // Descriptor Heap: GPU 리소스의 "주소록".
    // RTV 타입으로 FRAME_COUNT 개의 슬롯을 예약한다.
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = FRAME_COUNT;
    heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    HRESULT hr = m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_rtvHeap));
    assert(SUCCEEDED(hr));

    // 하드웨어마다 Descriptor 1칸의 크기(바이트)가 다르므로 미리 조회
    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV
    );

    // Heap 의 첫 번째 슬롯 주소에서 시작
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart();

    for (UINT i = 0; i < FRAME_COUNT; i++)
    {
        // SwapChain 의 i 번째 버퍼(텍스처)를 가져온다
        hr = m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]));
        assert(SUCCEEDED(hr));

        // 그 버퍼를 "렌더 타겟으로 쓸 수 있는 뷰" 로 Heap 슬롯에 등록
        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);

        // 다음 슬롯 주소로 이동
        rtvHandle.ptr += m_rtvDescriptorSize;
    }
}

void Engine::CreateFence()
{
    // Fence 초기값 0으로 생성. GPU가 Signal을 보낼 때마다 이 카운터가 증가한다.
    HRESULT hr = m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    assert(SUCCEEDED(hr));

    m_fenceValue = 0;

    // Auto-reset 이벤트: WaitForSingleObject 가 반환되는 순간 자동으로 비신호 상태로 복귀.
    // 수동 리셋(Manual-reset)을 쓰면 매번 ResetEvent를 호출해야 하므로 Auto-reset이 안전하다.
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    assert(m_fenceEvent != nullptr);
}

void Engine::CreateRootSignature()
{
    // Root Signature 는 셰이더가 접근할 GPU 리소스의 종류와 위치를 GPU 드라이버와 "계약"하는 객체.
    //
    // [Root Parameter 0] : CBV — ALL shaders b0 (mvp + colorTint)
    //   VS 는 mvp 를, PS 는 color 를 읽으므로 ShaderVisibility = ALL 이어야 한다.
    //   VERTEX 로 설정하면 PS 가 b0 을 참조할 때 PSO 생성 검증 실패가 발생한다.
    //   Root Descriptor 방식: Descriptor Heap 없이 GPU 가상 주소를 직접 바인딩.
    //
    // [Root Parameter 1] : Descriptor Table — Pixel Shader t0 (SRV, 텍스처)
    //   Descriptor Table 방식: GPU-visible SRV Heap 의 범위를 가리키는 포인터.
    //   CBV/SRV/UAV 는 Root Descriptor 로도 쓸 수 있지만, 텍스처 SRV 는
    //   Descriptor Table 을 통해서만 바인딩할 수 있다.

    // Param 0: CBV (b0, VS + PS 모두 접근)
    D3D12_ROOT_PARAMETER params[2] = {};
    params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0; // b0
    params[0].Descriptor.RegisterSpace  = 0;
    params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

    // Param 1: Descriptor Table (SRV 1개, t0, PS)
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors                    = 1;
    srvRange.BaseShaderRegister                = 0; // t0
    srvRange.RegisterSpace                     = 0;
    srvRange.OffsetInDescriptorsFromTableStart = 0;

    params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges   = &srvRange;
    params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    // Static Sampler (s0, PS): Root Signature 에 직접 내장하는 샘플러.
    // 별도의 Sampler Heap 이 필요 없어 간단하다.
    // POINT 필터: 최근접 픽셀 샘플링 → 픽셀 아트/타일 텍스처에 적합한 선명한 외곽선
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MipLODBias       = 0;
    sampler.MaxAnisotropy    = 1;
    sampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MinLOD           = 0.0f;
    sampler.MaxLOD           = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister   = 0; // s0
    sampler.RegisterSpace    = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters     = 2;
    desc.pParameters       = params;
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers   = &sampler;
    desc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    // Root Signature 는 드라이버에 전달하기 전에 직렬화(바이너리 변환)가 필요하다
    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(
        &desc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &serialized,
        &error
    );
    if (FAILED(hr) && error)
        OutputDebugStringA(static_cast<char*>(error->GetBufferPointer()));
    assert(SUCCEEDED(hr));

    hr = m_device->CreateRootSignature(
        0,
        serialized->GetBufferPointer(),
        serialized->GetBufferSize(),
        IID_PPV_ARGS(&m_rootSignature)
    );
    assert(SUCCEEDED(hr));
}

void Engine::CreatePipelineState()
{
    // ── [1] 셰이더 컴파일 ─────────────────────────────────────────────
    // D3DCompileFromFile 은 런타임에 .hlsl 파일을 컴파일해 ID3DBlob 에 담아준다.
    // exe 기준 절대 경로로 변환해 스탠드얼론 실행 시에도 파일을 찾을 수 있게 한다.
    UINT compileFlags = 0;
#ifdef _DEBUG
    // Debug 빌드에서는 셰이더 디버그 정보 포함, 최적화 끔
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    std::string  shaderPathStr = GetAbsolutePath("shaders.hlsl");
    std::wstring shaderPath(shaderPathStr.begin(), shaderPathStr.end());

    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> pixelShader;
    ComPtr<ID3DBlob> error;

    HRESULT hr = D3DCompileFromFile(
        shaderPath.c_str(),
        nullptr, nullptr,
        "VSMain", "vs_5_0",
        compileFlags, 0,
        &vertexShader, &error
    );
    if (FAILED(hr) && error)
        OutputDebugStringA(static_cast<char*>(error->GetBufferPointer()));
    assert(SUCCEEDED(hr) && "Vertex Shader 컴파일 실패 - shaders.hlsl 이 exe 폴더에 있는지 확인");

    error.Reset();
    hr = D3DCompileFromFile(
        shaderPath.c_str(),
        nullptr, nullptr,
        "PSMain", "ps_5_0",
        compileFlags, 0,
        &pixelShader, &error
    );
    if (FAILED(hr) && error)
        OutputDebugStringA(static_cast<char*>(error->GetBufferPointer()));
    assert(SUCCEEDED(hr) && "Pixel Shader 컴파일 실패");

    // ── [2] Input Layout ──────────────────────────────────────────────
    // GPU 에게 Vertex Buffer 의 메모리 구조를 알려주는 설명서.
    // Semantic 이름("POSITION", "COLOR")은 셰이더 VSInput 구조체의 Semantic 과 일치해야 한다.
    // AlignedByteOffset: 정점 구조체 시작 기준 이 필드의 바이트 오프셋
    //   POSITION (float3) = 12바이트 → 오프셋 0
    //   COLOR    (float4) = 16바이트 → 오프셋 12
    // Vertex 구조체 레이아웃: pos(12) + uv(8) = 20바이트
    D3D12_INPUT_ELEMENT_DESC inputLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    // ── [3] 래스터라이저 상태 ─────────────────────────────────────────
    // 삼각형을 픽셀로 변환할 때의 규칙:
    //   FillMode  SOLID  : 면을 채워서 그림 (WIREFRAME 은 외곽선만)
    //   CullMode  BACK   : 뒷면(법선이 카메라 반대) 삼각형은 그리지 않음
    //   DepthClipEnable  : 카메라 시야 범위(Near/Far) 밖 픽셀은 클리핑
    D3D12_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode        = D3D12_FILL_MODE_SOLID;
    rasterizerDesc.CullMode        = D3D12_CULL_MODE_BACK;
    rasterizerDesc.DepthClipEnable = TRUE;

    // ── [4] 블렌드 상태 ──────────────────────────────────────────────
    // 픽셀의 알파를 이용한 투명 합성 설정.
    // BlendEnable = FALSE : 블렌딩 없이 픽셀 색상을 그대로 렌더 타겟에 덮어쓴다.
    // RenderTargetWriteMask = ALL : RGBA 네 채널 모두 기록 (0이면 아무것도 안 씀!)
    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable           = FALSE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    // ── [5] PSO 생성 ─────────────────────────────────────────────────
    // PSO(Pipeline State Object): 위에서 정의한 모든 상태를 하나로 묶어
    // GPU 드라이버가 파이프라인을 미리 컴파일·최적화할 수 있게 한다.
    // D3D11 에서는 Draw 전마다 상태를 하나씩 바꿨지만, D3D12 는 PSO 교체 한 번으로 끝난다.
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature        = m_rootSignature.Get();
    psoDesc.VS                    = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    psoDesc.PS                    = { pixelShader->GetBufferPointer(),  pixelShader->GetBufferSize() };
    psoDesc.InputLayout           = { inputLayout, _countof(inputLayout) };
    psoDesc.RasterizerState       = rasterizerDesc;
    psoDesc.BlendState            = blendDesc;
    psoDesc.DepthStencilState     = {};             // 깊이/스텐실 비활성화 (DepthEnable = FALSE)
    psoDesc.DSVFormat             = DXGI_FORMAT_UNKNOWN;
    psoDesc.SampleMask            = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets      = 1;
    psoDesc.RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count      = 1;
    psoDesc.SampleDesc.Quality    = 0;

    hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState));
    assert(SUCCEEDED(hr) && "PSO 생성 실패");
}

void Engine::CreateVertexBuffer()
{
    // ── [1] 사각형 정점 데이터 (4개, UV 포함) ─────────────────────────
    // 픽셀 좌표계(Y↓ 증가)에 맞춰 Y=-0.5가 화면 위, Y=+0.5가 화면 아래.
    // 이 방향이 맞아야 투영 후 NDC에서 삼각형이 CW(앞면)로 보인다.
    // UV: V=0이 텍스처 위(stb_image 첫 행), V=1이 아래 — D3D12 표준.
    Vertex vertices[] =
    {
        { { -0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f } }, // [0] 좌상(화면) — UV(0,0)
        { {  0.5f, -0.5f, 0.0f }, { 1.0f, 0.0f } }, // [1] 우상(화면) — UV(1,0)
        { {  0.5f,  0.5f, 0.0f }, { 1.0f, 1.0f } }, // [2] 우하(화면) — UV(1,1)
        { { -0.5f,  0.5f, 0.0f }, { 0.0f, 1.0f } }, // [3] 좌하(화면) — UV(0,1)
    };
    const UINT bufferSize = sizeof(vertices);

    // ── [2] Upload Heap 에 버퍼 생성 ──────────────────────────────────
    // Upload Heap(HEAP_TYPE_UPLOAD): CPU가 쓰고 GPU가 읽는 공유 메모리 영역.
    // CPU 주소와 GPU 주소가 동시에 존재하며 Map/Unmap 으로 CPU에서 데이터를 씁니다.
    // 소규모 정적 버퍼에 적합. 대용량은 Default Heap + 임시 Upload Buffer 를 사용한다.
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufDesc = {};
    bufDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width            = bufferSize;
    bufDesc.Height           = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels        = 1;
    bufDesc.Format           = DXGI_FORMAT_UNKNOWN;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufDesc.Flags            = D3D12_RESOURCE_FLAG_NONE;

    // Upload Heap 리소스의 초기 상태는 반드시 GENERIC_READ 이어야 한다
    HRESULT hr = m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_vertexBuffer)
    );
    assert(SUCCEEDED(hr));

    // ── [3] Map → memcpy → Unmap ──────────────────────────────────────
    // Map: GPU 메모리에 대한 CPU 포인터를 얻는다.
    //   readRange = {0,0} : CPU가 이 메모리를 읽지 않을 것임을 드라이버에 알림(성능 힌트)
    // memcpy: 정점 배열을 Upload Heap 으로 복사한다.
    // Unmap: CPU 매핑을 해제한다. 데이터는 Heap 에 그대로 남으며 GPU가 읽을 수 있다.
    void* pData = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    hr = m_vertexBuffer->Map(0, &readRange, &pData);
    assert(SUCCEEDED(hr));
    memcpy(pData, vertices, bufferSize);
    m_vertexBuffer->Unmap(0, nullptr);

    // ── [4] Vertex Buffer View 설정 ───────────────────────────────────
    // GPU에게 "이 주소에서, 이 간격으로, 이 크기만큼 정점을 읽어라" 를 알려주는 구조체
    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.StrideInBytes  = sizeof(Vertex);   // 정점 하나의 크기(20바이트: pos 12 + uv 8)
    m_vertexBufferView.SizeInBytes    = bufferSize;        // 버퍼 전체 크기
}

void Engine::CreateIndexBuffer()
{
    // ── [1] 인덱스 데이터 ─────────────────────────────────────────────
    // 사각형 = 삼각형 2개.
    //   △ 1: 좌상(0) → 우상(1) → 우하(2)
    //   △ 2: 좌상(0) → 우하(2) → 좌하(3)
    // CW(시계방향) 와인딩 → D3D12 기본 Front Face → CullMode BACK 에서 정상 출력.
    UINT16 indices[] = { 0, 1, 2,  0, 2, 3 };
    const UINT bufferSize = sizeof(indices);

    // ── [2] Upload Heap 에 버퍼 생성 ──────────────────────────────────
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufDesc = {};
    bufDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width            = bufferSize;
    bufDesc.Height           = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels        = 1;
    bufDesc.Format           = DXGI_FORMAT_UNKNOWN;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufDesc.Flags            = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr = m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_indexBuffer)
    );
    assert(SUCCEEDED(hr));

    // ── [3] 인덱스 데이터 업로드 ──────────────────────────────────────
    void* pData = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    hr = m_indexBuffer->Map(0, &readRange, &pData);
    assert(SUCCEEDED(hr));
    memcpy(pData, indices, bufferSize);
    m_indexBuffer->Unmap(0, nullptr);

    // ── [4] Index Buffer View 설정 ────────────────────────────────────
    // Format R16_UINT : 인덱스 하나가 2바이트(UINT16)임을 GPU 에 알린다
    m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_indexBufferView.SizeInBytes    = bufferSize;
    m_indexBufferView.Format         = DXGI_FORMAT_R16_UINT;
}

void Engine::CreateConstantBuffer()
{
    // MAX_OBJECTS 개의 ObjectCB 슬롯을 연속으로 할당한다.
    // 각 슬롯은 D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT(256바이트) 크기로 정렬되어
    // SetGraphicsRootConstantBufferView 에 전달하는 GPU 주소가 항상 유효하다.
    const UINT cbTotalSize = sizeof(ObjectCB) * MAX_OBJECTS; // = 256 * 256 = 65536바이트

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufDesc = {};
    bufDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width            = cbTotalSize;
    bufDesc.Height           = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels        = 1;
    bufDesc.Format           = DXGI_FORMAT_UNKNOWN;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufDesc.Flags            = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr = m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_constantBuffer)
    );
    assert(SUCCEEDED(hr));

    // 영속적 Map: Render() 에서 매 프레임 각 슬롯에 MVP 행렬을 직접 기록한다.
    D3D12_RANGE readRange = { 0, 0 };
    hr = m_constantBuffer->Map(0, &readRange, &m_cbMappedData);
    assert(SUCCEEDED(hr));
}

void Engine::UpdateCamera()
{
    // 카메라 변환 포함 VP 행렬.
    //
    // (m_cameraX, m_cameraY) = 뷰포트 좌상단의 월드 좌표.
    // m_cameraZoom           = 배율. 2.0이면 월드를 절반 크기로 표시(확대).
    //
    // 보이는 월드 영역:
    //   X: [cameraX, cameraX + width  / zoom]
    //   Y: [cameraY, cameraY + height / zoom]  (Y↓: bottom > top)
    //
    // 기본값(cameraX=0, cameraY=0, zoom=1) → 이전과 동일하게 픽셀=월드 1:1.
    const float invZ  = 1.0f / m_cameraZoom;
    const float W     = static_cast<float>(m_width);
    const float H     = static_cast<float>(m_height);

    XMMATRIX vp = XMMatrixOrthographicOffCenterLH(
        m_cameraX,           m_cameraX + W * invZ,   // left, right
        m_cameraY + H * invZ, m_cameraY,              // bottom(Y↓), top
        0.0f, 1.0f
    );
    XMStoreFloat4x4(&m_viewProj, vp);
}

void Engine::InitImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(m_hwnd);

    ImGui_ImplDX12_InitInfo initInfo;
    initInfo.Device               = m_device.Get();
    initInfo.CommandQueue         = m_commandQueue.Get();
    initInfo.NumFramesInFlight    = FRAME_COUNT;
    initInfo.RTVFormat            = DXGI_FORMAT_R8G8B8A8_UNORM;
    initInfo.DSVFormat            = DXGI_FORMAT_UNKNOWN;
    initInfo.SrvDescriptorHeap    = ResourceManager::Instance().GetSrvHeap();
    initInfo.SrvDescriptorAllocFn = ImGuiSrvAllocFn;
    initInfo.SrvDescriptorFreeFn  = ImGuiSrvFreeFn;

    bool ok = ImGui_ImplDX12_Init(&initInfo);
    assert(ok && "ImGui DX12 백엔드 초기화 실패");

    m_imguiInitialized = true;
}

void Engine::AddGameObject(GameObject* obj)
{
    assert(obj && m_gameObjects.size() < MAX_OBJECTS);
    m_gameObjects.push_back(obj);
}

void Engine::WaitForPreviousFrame()
{
    // ---------------------------------------------------------------
    // [단계 1] Signal 예약
    //   Command Queue에 "네가 여기까지 처리하면 Fence를 m_fenceValue로 세팅해" 를 예약.
    //   이 호출은 비동기로 즉시 반환된다. GPU는 아직 명령을 처리 중일 수 있다.
    // ---------------------------------------------------------------
    ++m_fenceValue;
    HRESULT hr = m_commandQueue->Signal(m_fence.Get(), m_fenceValue);
    assert(SUCCEEDED(hr));

    // ---------------------------------------------------------------
    // [단계 2] GPU 완료 확인 및 대기
    //   GetCompletedValue() 가 Signal 한 값보다 작다면 GPU가 아직 못 따라온 것이다.
    //   SetEventOnCompletion 으로 "Fence가 m_fenceValue에 도달하면 이벤트를 발동시켜"
    //   라고 등록하고, WaitForSingleObject 로 CPU를 블로킹한다.
    // ---------------------------------------------------------------
    if (m_fence->GetCompletedValue() < m_fenceValue)
    {
        hr = m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
        assert(SUCCEEDED(hr));
        WaitForSingleObject(m_fenceEvent, INFINITE);
        // 이 줄에 도달했다면 GPU가 m_fenceValue 까지 처리를 마쳤음이 보장된다
    }

    // ---------------------------------------------------------------
    // [단계 3] 백버퍼 인덱스 갱신
    //   Present 이후 SwapChain이 버퍼를 교체했으므로 현재 그려야 할 버퍼를 다시 조회.
    // ---------------------------------------------------------------
    m_currentBackBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void Engine::Render()
{
    // ---------------------------------------------------------------
    // [1] Command Allocator / Command List 초기화
    //     GPU가 이전 프레임 명령을 모두 처리한 뒤에만 Reset 가능.
    //     WaitForPreviousFrame() 이 이를 보장하므로 안전하다.
    // ---------------------------------------------------------------
    HRESULT hr = m_commandAllocator->Reset();
    assert(SUCCEEDED(hr));

    hr = m_commandList->Reset(m_commandAllocator.Get(), nullptr);
    assert(SUCCEEDED(hr));

    // ---------------------------------------------------------------
    // [Update] 델타 타임 → 컴포넌트 Update → 피킹/드래그 → EndFrame
    // ---------------------------------------------------------------
    {
        UINT64 now = GetTickCount64();
        float dt = (m_lastFrameTick == 0)
            ? 0.0f
            : static_cast<float>(now - m_lastFrameTick) * 0.001f;
        m_lastFrameTick = now;

        // 상태 텍스트 타이머 감소 — 0 미만으로 내려가지 않도록 클램프한다.
        if (m_statusTimer > 0.0f)
            m_statusTimer = std::max(0.0f, m_statusTimer - dt);

        for (auto* obj : m_gameObjects)
            for (auto* comp : obj->GetComponents())
                comp->Update(dt);

        // DLL 게임 로직 Update (로드된 경우에만)
        if (m_gameLogic)
            m_gameLogic->Update(dt, m_gameObjects);

        // Hot Reload: 0.5 초마다 DLL 파일 수정 시각 확인
        m_hotReloadTimer += dt;
        if (m_hotReloadTimer >= 0.5f)
        {
            m_hotReloadTimer = 0.0f;
            CheckHotReload();
        }

        // ── 호버 감지 ─────────────────────────────────────────────────
        // [1] 모든 SpriteRenderer 의 isHovered 를 초기화한다.
        for (auto* obj : m_gameObjects)
        {
            auto* sr = obj->GetComponent<SpriteRenderer>();
            if (sr) sr->isHovered = false;
        }
        // [2] ImGui 패널 밖에서만 최상위 오브젝트를 찾아 isHovered = true 로 설정한다.
        if (!ImGui::GetIO().WantCaptureMouse)
        {
            float hx, hy;
            ScreenToWorld(InputManager::Instance().GetMouseX(),
                          InputManager::Instance().GetMouseY(), hx, hy);
            for (int i = static_cast<int>(m_gameObjects.size()) - 1; i >= 0; --i)
            {
                auto* tr = m_gameObjects[i]->GetComponent<Transform>();
                auto* sr = m_gameObjects[i]->GetComponent<SpriteRenderer>();
                if (!tr || !sr || !sr->visible) continue;
                float hw = tr->width  * 0.5f;
                float hh = tr->height * 0.5f;
                if (hx >= tr->x - hw && hx <= tr->x + hw &&
                    hy >= tr->y - hh && hy <= tr->y + hh)
                {
                    sr->isHovered = true;
                    break;
                }
            }
        }

        // ImGui 가 마우스를 소비하지 않을 때만 씬 피킹/드래그/카메라 처리
        if (!ImGui::GetIO().WantCaptureMouse)
        {
            // 좌클릭: 피킹 → Play Mode면 OnObjectClicked, Edit Mode면 드래그
            if (InputManager::Instance().IsLButtonPressed())
            {
                TryPickObject(); // 성공하면 m_isDragging = true, m_selectedObjectIdx 갱신
                if (m_isDragging && m_isPlaying && m_gameLogic)
                {
                    m_gameLogic->OnObjectClicked(m_gameObjects[m_selectedObjectIdx]);
                    m_isDragging = false; // Play Mode 에서는 드래그 하지 않는다
                }
            }
            // 드래그는 Edit Mode 에서만 동작한다
            if (!m_isPlaying && m_isDragging && InputManager::Instance().IsLButtonDown())
                DragSelectedObject();
            if (!InputManager::Instance().IsLButtonDown())
                m_isDragging = false;

            // 우클릭 드래그: 카메라 패닝
            // 화면 픽셀 이동량을 zoom 으로 나눠 월드 이동량으로 변환한 뒤
            // 반대 방향으로 적용 (뷰를 오른쪽으로 드래그 → 카메라는 왼쪽 이동).
            if (InputManager::Instance().IsRButtonDown())
            {
                m_cameraX -= static_cast<float>(InputManager::Instance().GetDeltaX()) / m_cameraZoom;
                m_cameraY -= static_cast<float>(InputManager::Instance().GetDeltaY()) / m_cameraZoom;
            }

            // 마우스 휠: 마우스 커서 위치를 기준으로 줌
            // 마우스 아래의 월드 좌표를 고정한 채 zoom 만 변경한다.
            const int wheel = InputManager::Instance().GetWheelDelta();
            if (wheel != 0)
            {
                const float mx = static_cast<float>(InputManager::Instance().GetMouseX());
                const float my = static_cast<float>(InputManager::Instance().GetMouseY());

                // 마우스 커서의 현재 월드 좌표
                const float wx = m_cameraX + mx / m_cameraZoom;
                const float wy = m_cameraY + my / m_cameraZoom;

                // WHEEL_DELTA(120) 한 눈금당 10% zoom 변화
                const float factor  = std::powf(1.1f, static_cast<float>(wheel) / 120.0f);
                const float newZoom = std::clamp(m_cameraZoom * factor, 0.1f, 5.0f);

                // zoom 변경 후에도 마우스 아래의 월드 좌표가 동일하도록 카메라 재조정
                m_cameraX    = wx - mx / newZoom;
                m_cameraY    = wy - my / newZoom;
                m_cameraZoom = newZoom;
            }
        }
        else
        {
            m_isDragging = false; // ImGui 가 입력을 가져가면 드래그 해제
        }

        // 카메라 상태가 바뀌었을 수 있으므로 VP 행렬을 매 프레임 재계산한다.
        UpdateCamera();
        InputManager::Instance().EndFrame();

        // Deferred Destroy: 이 프레임에 Destroy() 요청된 오브젝트를 일괄 제거
        // Update/Input 순회가 끝난 뒤 삭제하므로 루프 중 포인터 무효화가 발생하지 않는다.
        for (auto* obj : m_pendingDestroy)
        {
            auto it = std::find(m_gameObjects.begin(), m_gameObjects.end(), obj);
            if (it != m_gameObjects.end())
                m_gameObjects.erase(it);
            delete obj;
        }
        m_pendingDestroy.clear();

        // Deferred Restart: RestartGame() 요청 시 씬을 다시 로드하고 게임 로직을 재초기화한다.
        // 입력·순회가 모두 끝난 프레임 끝에서 처리하므로 오브젝트 포인터 무효화가 안전하다.
        // (deferred destroy 와 동일한 이유 — OnObjectClicked 안에서 호출해도 크래시 없음)
        if (m_pendingRestart)
        {
            m_pendingRestart = false;
            LoadScene(m_scenePath);         // 씬을 처음 상태로 복원 (모든 오브젝트 재생성)
            if (m_gameLogic)
                m_gameLogic->OnLoad(m_gameObjects, this); // 게임 로직 상태 초기화 + API 재주입
        }

        if (m_selectedObjectIdx >= static_cast<int>(m_gameObjects.size()))
            m_selectedObjectIdx = std::max(0, static_cast<int>(m_gameObjects.size()) - 1);
    }

    // ---------------------------------------------------------------
    // [ImGui] 새 프레임 시작 + Inspector UI 빌드 (CPU 작업만, GPU 기록은 나중에)
    // ImGui::Render() 는 draw data 를 확정하는 CPU 단계이며 Command List 와 무관하다.
    // ---------------------------------------------------------------
    if (m_imguiInitialized)
    {
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

#if !STANDALONE_MODE
        // ── 메인 메뉴바 ─────────────────────────────────────────────
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
                    SaveScene("assets/scene.json");
                if (ImGui::MenuItem("Load Scene", "Ctrl+O"))
                    LoadScene("assets/scene.json");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("GameLogic"))
            {
                bool loaded = (m_gameLogic != nullptr);
                if (!loaded && ImGui::MenuItem("Load GameLogic.dll"))
                    LoadGameLogicDLL("GameLogic.dll");
                if (loaded && ImGui::MenuItem("Reload Now"))
                    LoadGameLogicDLL(m_dllPath);
                if (loaded && ImGui::MenuItem("Unload GameLogic.dll"))
                    UnloadGameLogicDLL();
                ImGui::Separator();
                ImGui::Text("Loaded: %s", m_dllTimestampStr.c_str());
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("GameObject"))
            {
                if (ImGui::MenuItem("Create Tile"))
                {
                    auto* obj  = new GameObject();
                    obj->name  = "Tile_" + std::to_string(m_gameObjects.size() + 1);

                    auto* tr   = new Transform();
                    tr->x      = 0.0f;
                    tr->y      = 0.0f;
                    tr->width  = 100.0f;
                    tr->height = 100.0f;
                    obj->AddComponent(tr);

                    auto* sr = new SpriteRenderer();
                    sr->SetTexture("assets/tile.png");
                    obj->AddComponent(sr);

                    AddGameObject(obj);
                }
                ImGui::EndMenu();
            }

            // ── Play / Stop 버튼 ─────────────────────────────────────
            // 메뉴바 오른쪽 끝 배치: 총 너비에서 버튼 예상 폭만큼 뒤로 이동
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 80.0f);
            if (!m_isPlaying)
            {
                if (ImGui::MenuItem("[ Play ]"))
                    m_isPlaying = true;
            }
            else
            {
                if (ImGui::MenuItem("[ Stop ]"))
                    m_isPlaying = false;
            }

            ImGui::EndMainMenuBar();
        }

        // ── Hierarchy 창 ─────────────────────────────────────────────
        ImGui::Begin("Hierarchy");
        for (int i = 0; i < static_cast<int>(m_gameObjects.size()); ++i)
        {
            bool sel = (i == m_selectedObjectIdx);
            if (ImGui::Selectable(m_gameObjects[i]->name.c_str(), sel))
                m_selectedObjectIdx = i;
        }
        if (m_selectedObjectIdx >= static_cast<int>(m_gameObjects.size()))
            m_selectedObjectIdx = std::max(0, static_cast<int>(m_gameObjects.size()) - 1);
        ImGui::End();

        // ── Inspector 창 ─────────────────────────────────────────────
        ImGui::Begin("Inspector");
        if (m_gameObjects.empty())
        {
            ImGui::TextDisabled("Select an object");
        }
        else
        {
            auto* obj = m_gameObjects[m_selectedObjectIdx];

            char nameBuf[128] = {};
            strncpy_s(nameBuf, obj->name.c_str(), sizeof(nameBuf) - 1);
            if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
                obj->name = nameBuf;

            char tagBuf[128] = {};
            strncpy_s(tagBuf, obj->tag.c_str(), sizeof(tagBuf) - 1);
            if (ImGui::InputText("Tag", tagBuf, sizeof(tagBuf)))
                obj->tag = tagBuf;

            ImGui::InputInt("Team ID", &obj->teamID);
            ImGui::Separator();

            auto* tr = obj->GetComponent<Transform>();
            if (tr && ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
            {
                float pos[2] = { tr->x, tr->y };
                if (ImGui::DragFloat2("Position", pos, 1.0f))
                { tr->x = pos[0]; tr->y = pos[1]; }
            }

            auto* sr = obj->GetComponent<SpriteRenderer>();
            if (sr && ImGui::CollapsingHeader("SpriteRenderer", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Checkbox("Visible", &sr->visible);
                ImGui::Text("Texture: %s", sr->GetTexturePath().c_str());
            }
        }
        ImGui::End();

        // ── Project (에셋 브라우저) 창 ──────────────────────────────
        ImGui::Begin("Project (Assets)");
        {
            std::string assetsDir = GetAbsolutePath("assets/");
            if (std::filesystem::exists(assetsDir))
            {
                for (const auto& entry : std::filesystem::directory_iterator(assetsDir))
                {
                    if (entry.is_regular_file())
                        ImGui::Text("%s", entry.path().filename().string().c_str());
                }
            }
            else
            {
                ImGui::TextDisabled("assets/ folder not found");
            }
        }
        ImGui::End();

        // ── Board Generator 창 ───────────────────────────────────────
        ImGui::Begin("Board Generator");
        ImGui::InputInt("Rows",      &m_boardRows);
        ImGui::InputInt("Columns",   &m_boardCols);
        ImGui::InputInt("Tile Size", &m_boardTileSize);

        if (m_boardRows     < 1) m_boardRows     = 1;
        if (m_boardCols     < 1) m_boardCols     = 1;
        if (m_boardTileSize < 1) m_boardTileSize = 1;

        ImGui::Separator();
        ImGui::Checkbox("Snap to Grid", &m_snapToGrid);
        if (m_snapToGrid)
        {
            ImGui::InputInt("Snap Size", &m_snapSize);
            if (m_snapSize < 1) m_snapSize = 1;
        }
        ImGui::Separator();

        int total = m_boardRows * m_boardCols;
        if (total > static_cast<int>(MAX_OBJECTS))
            ImGui::TextColored({1,0.4f,0,1}, "Warning: %d tiles > MAX_OBJECTS(%u)", total, MAX_OBJECTS);
        else
            ImGui::Text("Total tiles: %d", total);

        if (ImGui::Button("Generate Board"))
        {
            for (auto* obj : m_gameObjects) delete obj;
            m_gameObjects.clear();
            m_selectedObjectIdx = 0;
            m_isDragging        = false;

            float ts = static_cast<float>(m_boardTileSize);
            for (int row = 0; row < m_boardRows; ++row)
            {
                for (int col = 0; col < m_boardCols; ++col)
                {
                    if (static_cast<UINT>(m_gameObjects.size()) >= MAX_OBJECTS)
                        break;

                    auto* obj  = new GameObject();
                    obj->name  = "Tile_" + std::to_string(row)
                                 + "_"   + std::to_string(col);

                    auto* tr   = new Transform();
                    tr->x      = col * ts + ts * 0.5f;
                    tr->y      = row * ts + ts * 0.5f;
                    tr->width  = ts;
                    tr->height = ts;
                    obj->AddComponent(tr);

                    auto* sr = new SpriteRenderer();
                    sr->SetTexture("assets/tile.png");
                    obj->AddComponent(sr);

                    AddGameObject(obj);
                }
                if (static_cast<UINT>(m_gameObjects.size()) >= MAX_OBJECTS)
                    break;
            }
        }
        ImGui::End();
#endif // !STANDALONE_MODE

        // ── 게임 상태 텍스트 오버레이 (에디터·스탠드얼론 공통) ────────
        // SetGameStatusText() 로 설정된 텍스트를 화면 상단 중앙에 표시한다.
        // STANDALONE_MODE 밖에 위치해야 두 모드 모두에서 동작한다.
        if (m_statusTimer > 0.0f && !m_statusText.empty())
        {
            constexpr ImGuiWindowFlags kOverlayFlags =
                ImGuiWindowFlags_NoDecoration      |
                ImGuiWindowFlags_NoInputs           |
                ImGuiWindowFlags_AlwaysAutoResize   |
                ImGuiWindowFlags_NoNav              |
                ImGuiWindowFlags_NoMove             |
                ImGuiWindowFlags_NoSavedSettings    |
                ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoBringToFrontOnFocus;

            // pivot = {0.5, 0}: 창의 가로 중심을 기준점으로 삼아 정중앙에 배치한다.
            ImGui::SetNextWindowPos(
                { static_cast<float>(m_width) * 0.5f, 12.0f },
                ImGuiCond_Always, { 0.5f, 0.0f });
            ImGui::SetNextWindowBgAlpha(0.78f); // 반투명 배경

            ImGui::Begin("##gamestatus", nullptr, kOverlayFlags);
            ImGui::SetWindowFontScale(1.8f);        // 1.8× 크기로 텍스트 확대
            ImGui::TextUnformatted(m_statusText.c_str());
            ImGui::End();
        }

        // ── 오브젝트 라벨(Label 컴포넌트) 오버레이 — 에디터·스탠드얼론 공통 ──
        RenderObjectLabels();

        ImGui::Render(); // draw data 확정 (CPU only)
    }

    // ---------------------------------------------------------------
    // [2] Resource Barrier: PRESENT → RENDER_TARGET
    //     SwapChain 백버퍼는 기본적으로 PRESENT 상태(화면 출력 전용)이다.
    //     GPU가 이 버퍼에 픽셀을 쓰려면 RENDER_TARGET 상태로 전환해야 한다.
    //     이 Barrier를 빠뜨리면 GPU 검증 레이어 에러 + 화면 깨짐이 발생한다.
    // ---------------------------------------------------------------
    D3D12_RESOURCE_BARRIER barrierToRT = {};
    barrierToRT.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierToRT.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrierToRT.Transition.pResource   = m_renderTargets[m_currentBackBufferIndex].Get();
    barrierToRT.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrierToRT.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrierToRT.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    m_commandList->ResourceBarrier(1, &barrierToRT);

    // ---------------------------------------------------------------
    // [3] 렌더 타겟 지정 및 화면 클리어
    //     현재 백버퍼의 RTV 핸들: Heap 시작 주소 + (인덱스 * Descriptor 크기)
    // ---------------------------------------------------------------
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += static_cast<SIZE_T>(m_currentBackBufferIndex) * m_rtvDescriptorSize;

    m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f }; // R, G, B, A
    m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    // ---------------------------------------------------------------
    // [4] 삼각형 그리기 명령 기록
    // ---------------------------------------------------------------

    // Viewport: NDC 좌표를 화면 픽셀 좌표로 변환하는 영역
    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width    = static_cast<float>(m_width);
    viewport.Height   = static_cast<float>(m_height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    m_commandList->RSSetViewports(1, &viewport);

    // ScissorRect: 이 사각형 밖의 픽셀은 무조건 버린다 (클리핑)
    D3D12_RECT scissorRect = { 0, 0, m_width, m_height };
    m_commandList->RSSetScissorRects(1, &scissorRect);

    // Root Signature / PSO / Heap / IA 설정 — 모든 오브젝트 공통, 루프 밖에서 1회
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->SetPipelineState(m_pipelineState.Get());

    // SRV Heap 등록 (GPU-visible Heap 은 반드시 SetDescriptorHeaps 로 알려야 한다)
    // 텍스처 descriptor table 은 오브젝트별로 루프 내부에서 바인딩한다.
    ID3D12DescriptorHeap* heaps[] = { ResourceManager::Instance().GetSrvHeap() };
    m_commandList->SetDescriptorHeaps(1, heaps);

    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    m_commandList->IASetIndexBuffer(&m_indexBufferView);

    // ── 오브젝트별 렌더링 루프 ───────────────────────────────────────
    // Transform + SpriteRenderer 를 모두 가진 오브젝트만 실제로 그린다.
    // slotIdx: 상수 버퍼의 연속 슬롯 인덱스 (invisible 오브젝트는 슬롯을 차지하지 않음)
    XMMATRIX viewProj = XMLoadFloat4x4(&m_viewProj);
    UINT slotIdx = 0;

    for (auto* obj : m_gameObjects)
    {
        auto* tr = obj->GetComponent<Transform>();
        auto* sr = obj->GetComponent<SpriteRenderer>();

        if (!tr || !sr || !sr->visible)
            continue;
        if (slotIdx >= MAX_OBJECTS)
            break;

        // HLSL cbuffer는 기본 열 우선(column-major) 읽기를 수행하므로
        // DM 행 우선 행렬을 그대로 전달하면 HLSL이 자동으로 전치(transpose)해서 읽는다.
        // → mul(M, v) 셰이더에서 올바른 열벡터 변환이 적용된다.
        // XMMatrixTranspose를 적용하면 이중 전치가 되어 W 왜곡이 발생하므로 절대 호출하지 않는다.
        XMMATRIX mvp = tr->GetMVP(viewProj);

        auto* pSlot = static_cast<BYTE*>(m_cbMappedData) + slotIdx * sizeof(ObjectCB);
        XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(pSlot), mvp);
        // colorTint: MVP 바로 뒤(오프셋 64바이트)에 기록한다.
        // isHovered 가 true 면 RGB 를 +0.3 밝게 보정한다 (셰이더 변경 없이 CPU 단에서만 처리).
        XMFLOAT4 tint = sr->colorTint;
        if (sr->isHovered)
        {
            // 원본 60% + 연한 푸른색(0.3, 0.6, 1.0) 40% 블렌딩
            tint.x = std::min(tint.x * 0.6f + 0.4f * 0.3f, 1.0f);
            tint.y = std::min(tint.y * 0.6f + 0.4f * 0.6f, 1.0f);
            tint.z = std::min(tint.z * 0.6f + 0.4f * 1.0f, 1.0f);
        }
        memcpy(pSlot + sizeof(XMFLOAT4X4), &tint, sizeof(XMFLOAT4));

        D3D12_GPU_VIRTUAL_ADDRESS cbAddr =
            m_constantBuffer->GetGPUVirtualAddress() + slotIdx * sizeof(ObjectCB);
        m_commandList->SetGraphicsRootConstantBufferView(0, cbAddr);

        // 오브젝트마다 텍스처가 다를 수 있으므로 GPU 핸들을 개별 바인딩한다.
        const TextureHandle* th = sr->GetTextureHandle();
        if (!th) continue;
        m_commandList->SetGraphicsRootDescriptorTable(1, th->gpuHandle);

        m_commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
        ++slotIdx;
    }

    // ---------------------------------------------------------------
    // [ImGui] draw data 를 Command List 에 기록 (GPU 명령 기록 단계)
    // 반드시 게임 오브젝트 draw call 이후, PRESENT 배리어 이전에 위치해야 한다.
    // ---------------------------------------------------------------
    if (m_imguiInitialized)
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList.Get());

    // ---------------------------------------------------------------
    // [5] Resource Barrier: RENDER_TARGET → PRESENT
    //     렌더링이 끝났으면 버퍼를 다시 PRESENT 상태로 되돌려야
    //     SwapChain이 이 버퍼를 화면에 출력할 수 있다.
    // ---------------------------------------------------------------
    D3D12_RESOURCE_BARRIER barrierToPresent = {};
    barrierToPresent.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierToPresent.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrierToPresent.Transition.pResource   = m_renderTargets[m_currentBackBufferIndex].Get();
    barrierToPresent.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrierToPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrierToPresent.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
    m_commandList->ResourceBarrier(1, &barrierToPresent);

    // ---------------------------------------------------------------
    // [6] Command List 기록 완료 → Queue 제출 → Present → 동기화
    // ---------------------------------------------------------------
    hr = m_commandList->Close();
    assert(SUCCEEDED(hr));

    ID3D12CommandList* lists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(lists), lists);

    // VSync ON (첫 번째 인수 1 = 수직 동기화 1회 대기)
    hr = m_swapChain->Present(1, 0);
    assert(SUCCEEDED(hr));

    WaitForPreviousFrame();
}

void Engine::OnResize(UINT width, UINT height)
{
    // D3D12 초기화 전 또는 최소화(0x0) 상태에서 호출되면 무시
    if (!m_device || !m_swapChain || width == 0 || height == 0)
        return;

    m_width  = static_cast<int>(width);
    m_height = static_cast<int>(height);

    // ── [1] GPU 플러시 ───────────────────────────────────────────────
    // ResizeBuffers 는 스왑 체인 버퍼에 대한 외부 참조가 모두 없어야 동작한다.
    // GPU 가 이전 프레임 명령을 마칠 때까지 대기해 참조를 안전하게 제거한다.
    WaitForPreviousFrame();

    // ── [2] 렌더 타겟 참조 해제 ──────────────────────────────────────
    // m_renderTargets 가 버퍼를 붙잡고 있으면 ResizeBuffers 가 E_INVALIDARG 를 반환한다.
    for (auto& rt : m_renderTargets)
        rt.Reset();

    // ── [3] 스왑 체인 버퍼 리사이즈 ──────────────────────────────────
    // DXGI_FORMAT_UNKNOWN : 기존 포맷 유지
    // 마지막 인수 0      : 생성 시 SwapChainFlag 와 동일하게 유지
    HRESULT hr = m_swapChain->ResizeBuffers(
        FRAME_COUNT,
        width, height,
        DXGI_FORMAT_UNKNOWN,
        0
    );
    assert(SUCCEEDED(hr));

    // Present 이후 백버퍼 인덱스가 리셋되므로 다시 조회
    m_currentBackBufferIndex = m_swapChain->GetCurrentBackBufferIndex();

    // ── [4] RTV 재생성 ────────────────────────────────────────────────
    // Descriptor Heap 자체는 크기가 변하지 않으므로 재사용한다.
    // 버퍼가 교체됐으므로 뷰(View)만 새로 등록하면 된다.
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart();

    for (UINT i = 0; i < FRAME_COUNT; i++)
    {
        hr = m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]));
        assert(SUCCEEDED(hr));

        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += m_rtvDescriptorSize;
    }

    UpdateCamera();
}

// =====================================================================
// 윈도우 메시지 처리
// =====================================================================

LRESULT CALLBACK Engine::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    // ImGui 가 마우스/키보드 입력을 먼저 소비하게 한다.
    // ImGui 초기화 전에 호출되면 내부에서 context null 체크 후 0 을 반환하므로 안전하다.
    if (ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam))
        return true;

    Engine* pEngine = reinterpret_cast<Engine*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    if (uMsg == WM_NCCREATE)
    {
        auto* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pEngine = reinterpret_cast<Engine*>(pCreate->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pEngine));
    }

    switch (uMsg)
    {
    case WM_SIZE:
        // wParam 이 SIZE_MINIMIZED(1) 이면 창이 최소화된 상태이므로 리사이즈 불필요
        if (pEngine && wParam != SIZE_MINIMIZED)
            pEngine->OnResize(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_LBUTTONDOWN:
        // static_cast<short> 로 캐스트해야 음수 좌표(창 밖)가 올바르게 처리된다
        InputManager::Instance().OnLButtonDown(
            static_cast<short>(LOWORD(lParam)),
            static_cast<short>(HIWORD(lParam)));
        return 0;

    case WM_LBUTTONUP:
        InputManager::Instance().OnLButtonUp(
            static_cast<short>(LOWORD(lParam)),
            static_cast<short>(HIWORD(lParam)));
        return 0;

    case WM_MOUSEMOVE:
        InputManager::Instance().OnMouseMove(
            static_cast<short>(LOWORD(lParam)),
            static_cast<short>(HIWORD(lParam)));
        return 0;

    case WM_RBUTTONDOWN:
        InputManager::Instance().OnRButtonDown(
            static_cast<short>(LOWORD(lParam)),
            static_cast<short>(HIWORD(lParam)));
        return 0;

    case WM_RBUTTONUP:
        InputManager::Instance().OnRButtonUp(
            static_cast<short>(LOWORD(lParam)),
            static_cast<short>(HIWORD(lParam)));
        return 0;

    case WM_MOUSEWHEEL:
        // GET_WHEEL_DELTA_WPARAM: 양수=위로 스크롤(줌인), 음수=아래(줌아웃)
        // WM_MOUSEWHEEL 의 좌표는 클라이언트 좌표가 아닌 스크린 좌표이므로 변환 불필요
        InputManager::Instance().OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
            PostQuitMessage(0);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// =====================================================================
// 피킹 & 드래그
// =====================================================================

void Engine::ScreenToWorld(int sx, int sy, float& wx, float& wy) const
{
    // 카메라 역변환: 화면 픽셀 → 월드 좌표.
    // 뷰포트 좌상단이 (m_cameraX, m_cameraY)이고 배율이 m_cameraZoom이므로:
    //   world = camera_origin + screen_pixel / zoom
    wx = m_cameraX + static_cast<float>(sx) / m_cameraZoom;
    wy = m_cameraY + static_cast<float>(sy) / m_cameraZoom;
}

void Engine::TryPickObject()
{
    float wx, wy;
    ScreenToWorld(InputManager::Instance().GetMouseX(),
                  InputManager::Instance().GetMouseY(), wx, wy);

    // 역순 순회: 배열 뒤쪽(나중에 그려진, 즉 화면 위)부터 검사한다.
    for (int i = static_cast<int>(m_gameObjects.size()) - 1; i >= 0; --i)
    {
        auto* tr = m_gameObjects[i]->GetComponent<Transform>();
        if (!tr) continue;

        float hw = tr->width  * 0.5f;
        float hh = tr->height * 0.5f;
        if (wx >= tr->x - hw && wx <= tr->x + hw &&
            wy >= tr->y - hh && wy <= tr->y + hh)
        {
            m_selectedObjectIdx = i;
            m_isDragging = true;
            return;
        }
    }
}

void Engine::DragSelectedObject()
{
    if (m_selectedObjectIdx < 0 ||
        m_selectedObjectIdx >= static_cast<int>(m_gameObjects.size()))
        return;

    auto* tr = m_gameObjects[m_selectedObjectIdx]->GetComponent<Transform>();
    if (!tr) return;

    tr->x += static_cast<float>(InputManager::Instance().GetDeltaX());
    tr->y += static_cast<float>(InputManager::Instance().GetDeltaY());

    // 스냅 활성화 시 — Board Generator 배치 수식(중심 = col*ts + ts/2)에 맞게 정렬.
    // 격자 셀 중심은 k*snapSize + snapSize/2 (k=0,1,2,...) 이므로
    // 현재 위치에서 가장 가까운 셀 중심으로 반올림한다.
    if (m_snapToGrid && m_snapSize > 0)
    {
        const float half = m_snapSize * 0.5f;
        tr->x = roundf((tr->x - half) / m_snapSize) * m_snapSize + half;
        tr->y = roundf((tr->y - half) / m_snapSize) * m_snapSize + half;
    }
}

// =====================================================================
// Scene 저장 / 로드
// =====================================================================

void Engine::SaveScene(const std::string& path)
{
    nlohmann::json root;
    root["gameObjects"] = nlohmann::json::array();

    for (auto* obj : m_gameObjects)
    {
        nlohmann::json jObj;
        jObj["name"]       = obj->name;
        jObj["components"] = nlohmann::json::array();

        for (auto* comp : obj->GetComponents())
        {
            nlohmann::json jComp = comp->Serialize();
            // Serialize 기본 구현이 {} 를 반환하면 스킵 (ScriptComponent 등 미지원 타입)
            if (!jComp.empty())
                jObj["components"].push_back(jComp);
        }

        root["gameObjects"].push_back(jObj);
    }

    std::ofstream file(GetAbsolutePath(path));
    file << root.dump(4); // 들여쓰기 4칸 pretty-print
}

// =====================================================================
// GameLogic DLL 로드 / 언로드
// =====================================================================

void Engine::LoadGameLogicDLL(const std::string& path)
{
    UnloadGameLogicDLL(); // 기존 DLL 이 있으면 먼저 안전하게 해제 (FreeLibrary 로 _temp.dll 잠금 해제)

    // ── 절대 경로 구성 ────────────────────────────────────────────────────
    // VS 디버그 실행 시 작업 디렉터리(Working Directory)가 프로젝트 소스 폴더로 설정되어
    // 상대 경로 "GameLogic.dll"이 EXE 폴더가 아닌 다른 위치를 가리키는 문제를 방지한다.
    // GetModuleFileNameA(nullptr): 현재 실행 중인 EXE의 완전한 절대 경로를 반환한다.
    char exePathBuf[MAX_PATH] = {};
    if (GetModuleFileNameA(nullptr, exePathBuf, MAX_PATH) == 0)
    {
        MessageBoxA(m_hwnd,
            ("GetModuleFileNameA 실패\nGetLastError: " +
             std::to_string(GetLastError())).c_str(),
            "GameLogic 로드 실패", MB_OK | MB_ICONERROR);
        return;
    }

    namespace fs = std::filesystem;
    // EXE 가 위치한 디렉터리를 기준으로 DLL 파일명만 조합 — 입력 경로의 디렉터리 부분은 무시한다.
    fs::path exeDir = fs::path(exePathBuf).parent_path();
    fs::path srcPath = exeDir / fs::path(path).filename();
    fs::path tmpPath = exeDir / (srcPath.stem().string() + "_temp.dll");
    std::string srcStr = srcPath.string();
    std::string tmpStr = tmpPath.string();

    // ── Shadow Copy ────────────────────────────────────────────────────────
    // 원본은 잠금 없이 유지되므로 VS 빌드 중 링커가 자유롭게 덮어쓸 수 있다 (LNK1104 방지).
    if (!CopyFileA(srcStr.c_str(), tmpStr.c_str(), FALSE))
    {
        MessageBoxA(m_hwnd,
            ("[CopyFileA 실패] Shadow copy 불가\n"
             "원본: " + srcStr + "\n"
             "대상: " + tmpStr + "\n"
             "GetLastError: " + std::to_string(GetLastError())).c_str(),
            "GameLogic 로드 실패", MB_OK | MB_ICONERROR);
        return;
    }

    m_gameLogicModule = LoadLibraryA(tmpStr.c_str());
    if (!m_gameLogicModule)
    {
        MessageBoxA(m_hwnd,
            ("[LoadLibraryA 실패] DLL 로드 불가\n"
             "경로: " + tmpStr + "\n"
             "GetLastError: " + std::to_string(GetLastError())).c_str(),
            "GameLogic 로드 실패", MB_OK | MB_ICONERROR);
        return;
    }

    // GetProcAddress: extern "C" 덕분에 이름 맹글링 없이 "CreateGameLogic" 을 찾는다.
    auto* fn = reinterpret_cast<CreateGameLogicFn>(
        GetProcAddress(m_gameLogicModule, "CreateGameLogic"));
    if (!fn)
    {
        MessageBoxA(m_hwnd,
            ("[GetProcAddress 실패] 심볼을 찾을 수 없음\n"
             "심볼: CreateGameLogic\n"
             "GetLastError: " + std::to_string(GetLastError())).c_str(),
            "GameLogic 로드 실패", MB_OK | MB_ICONERROR);
        FreeLibrary(m_gameLogicModule);
        m_gameLogicModule = nullptr;
        return;
    }

    m_gameLogic = fn();
    if (m_gameLogic)
        m_gameLogic->OnLoad(m_gameObjects, this);

    // 절대 경로와 타임스탬프를 기록해 hot reload 감지에 활용한다.
    m_dllPath = srcStr; // 항상 절대 경로로 저장 — hot reload 시 재사용
    std::error_code ec;
    m_dllLastWriteTime = fs::last_write_time(srcStr, ec);
    m_dllTimestampStr  = ec ? "Unknown" : FormatFileTime(m_dllLastWriteTime);
}

void Engine::UnloadGameLogicDLL()
{
    if (m_gameLogic)
    {
        m_gameLogic->OnUnload();
        delete m_gameLogic; // DLL 힙에서 생성됐으므로 같은 CRT 필수 (/MDd 또는 /MD)
        m_gameLogic = nullptr;
    }
    if (m_gameLogicModule)
    {
        FreeLibrary(m_gameLogicModule); // _temp.dll 잠금 해제 → 다음 shadow copy 가 덮어쓸 수 있다
        m_gameLogicModule = nullptr;
    }
    m_dllPath.clear();
    m_dllTimestampStr = "Not loaded";
}

void Engine::CheckHotReload()
{
    if (m_dllPath.empty()) return;
    std::error_code ec;
    auto newTime = std::filesystem::last_write_time(m_dllPath, ec);
    if (!ec && newTime != m_dllLastWriteTime)
        LoadGameLogicDLL(m_dllPath);
}

// =====================================================================
// 경로 해석 유틸리티
// =====================================================================

std::string Engine::GetAbsolutePath(const std::string& relativePath) const
{
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return (std::filesystem::path(buf).parent_path() / relativePath).string();
}

// =====================================================================
// IEngineAPI 구현 — GameLogic DLL 이 호출하는 스크립팅 API
// =====================================================================

GameObject* Engine::Instantiate(const std::string& name, float x, float y)
{
    if (m_gameObjects.size() >= MAX_OBJECTS)
    {
        assert(false && "Instantiate: MAX_OBJECTS 초과. Engine.h 에서 MAX_OBJECTS 를 늘려주세요.");
        return nullptr;
    }
    auto* obj = new GameObject();
    obj->name  = name;
    auto* tr   = new Transform();
    tr->x      = x;
    tr->y      = y;
    obj->AddComponent(tr);
    m_gameObjects.push_back(obj);
    return obj;
}

void Engine::AddSpriteRenderer(GameObject* obj, const std::string& texturePath)
{
    if (!obj) return;
    auto* sr = new SpriteRenderer();
    sr->SetTexture(texturePath);
    obj->AddComponent(sr);
}

void Engine::Destroy(GameObject* obj)
{
    if (obj)
        m_pendingDestroy.push_back(obj);
}

GameObject* Engine::FindObjectByName(const std::string& name)
{
    for (auto* obj : m_gameObjects)
        if (obj->name == name)
            return obj;
    return nullptr;
}

void Engine::PlayAudio(const std::string& filePath)
{
    std::string absPath = GetAbsolutePath(filePath);

    if (!std::filesystem::exists(absPath))
    {
        MessageBoxA(m_hwnd,
            ("Audio file not found: " + absPath).c_str(),
            "PlayAudio 오류", MB_OK | MB_ICONWARNING);
        return;
    }

    // SND_ASYNC: 논블로킹. SND_NODEFAULT: 파일 없어도 기본음 불사용.
    PlaySoundA(absPath.c_str(), nullptr, SND_ASYNC | SND_FILENAME | SND_NODEFAULT);
}

void Engine::SetSpriteTexture(GameObject* obj, const std::string& texturePath)
{
    if (!obj) return;
    auto* sr = obj->GetComponent<SpriteRenderer>();
    if (!sr) return;
    // GetAbsolutePath 를 거쳐 절대 경로로 변환한다.
    // ResourceManager::LoadTexture 는 이미 절대 경로이면 재변환 없이 그대로 사용한다.
    sr->SetTexture(GetAbsolutePath(texturePath));
}

void Engine::SetGameStatusText(const std::string& text, float duration)
{
    m_statusText  = text;
    // duration <= 0 이면 텍스트를 즉시 숨긴다.
    m_statusTimer = std::max(0.0f, duration);
}

void Engine::RestartGame()
{
    // 즉시 리로드하지 않고 플래그만 세운다. 실제 처리는 Render() Update 블록 끝에서
    // (입력·오브젝트 순회가 모두 끝난 뒤) 이뤄지므로 순회 중 포인터 무효화가 없다.
    m_pendingRestart = true;
}

void Engine::SetObjectText(GameObject* obj, const std::string& text,
                           float r, float g, float b)
{
    if (!obj) return;
    // Label 이 없으면 자동 부착 (AddSpriteRenderer 와 동일한 패턴).
    auto* label = obj->GetComponent<Label>();
    if (!label)
    {
        label = new Label();
        obj->AddComponent(label);
    }
    label->text  = text;
    label->color = { r, g, b, 1.0f };
}

void Engine::RenderObjectLabels()
{
    // ImGui 프레임 안에서만 호출된다. 배경 드로우리스트는 씬 스프라이트 위,
    // ImGui 창 아래에 그려지므로 에디터 패널이 라벨을 가릴 수 있어 자연스럽다.
    ImDrawList* dl   = ImGui::GetBackgroundDrawList();
    ImFont*     font = ImGui::GetFont();

    for (auto* obj : m_gameObjects)
    {
        auto* label = obj->GetComponent<Label>();
        if (!label || label->text.empty()) continue;
        auto* tr = obj->GetComponent<Transform>();
        if (!tr) continue;

        // 월드 → 화면 (ScreenToWorld 의 역변환): screen = (world - camera) * zoom
        const float sx     = (tr->x - m_cameraX) * m_cameraZoom;
        const float sy     = (tr->y - m_cameraY) * m_cameraZoom;
        const float fontPx = label->size * m_cameraZoom;
        if (fontPx < 1.0f) continue;

        const char* txt = label->text.c_str();
        const ImVec2 sz  = font->CalcTextSizeA(fontPx, FLT_MAX, 0.0f, txt);
        const ImVec2 pos = { sx - sz.x * 0.5f, sy - sz.y * 0.5f }; // 중심 정렬

        // 가독성용 아웃라인: 글자 밝기(luminance)에 대비되는 색으로 4방향 그림자를 깐다.
        const float lum = label->color.x * 0.299f + label->color.y * 0.587f + label->color.z * 0.114f;
        const ImU32 outline = (lum > 0.5f) ? IM_COL32(0, 0, 0, 230) : IM_COL32(255, 255, 255, 230);
        const ImU32 main = IM_COL32(
            static_cast<int>(label->color.x * 255.0f),
            static_cast<int>(label->color.y * 255.0f),
            static_cast<int>(label->color.z * 255.0f),
            static_cast<int>(label->color.w * 255.0f));

        const float o = (fontPx >= 20.0f) ? 2.0f : 1.0f;
        dl->AddText(font, fontPx, { pos.x - o, pos.y }, outline, txt);
        dl->AddText(font, fontPx, { pos.x + o, pos.y }, outline, txt);
        dl->AddText(font, fontPx, { pos.x, pos.y - o }, outline, txt);
        dl->AddText(font, fontPx, { pos.x, pos.y + o }, outline, txt);
        dl->AddText(font, fontPx, pos, main, txt);
    }
}

void Engine::LoadScene(const std::string& path)
{
    // RestartGame() 이 다시 로드할 수 있도록 마지막 씬 경로를 기억한다.
    m_scenePath = path;

    std::ifstream file(GetAbsolutePath(path));
    if (!file.is_open())
        return;

    nlohmann::json root = nlohmann::json::parse(file, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded())
        return; // JSON 파싱 실패 시 현재 씬 유지

    // ── 기존 오브젝트 안전 해제 ──────────────────────────────────────
    // GPU는 이미 WaitForPreviousFrame 으로 플러시된 상태이므로
    // 텍스처 핸들 raw ptr 을 들고 있는 SpriteRenderer 를 삭제해도 안전하다.
    // (텍스처 리소스 자체는 ResourceManager 가 소유하므로 삭제되지 않는다)
    for (auto* obj : m_gameObjects)
        delete obj;
    m_gameObjects.clear();
    m_selectedObjectIdx = 0;
    m_isDragging        = false;

    // ── 오브젝트 재생성 ──────────────────────────────────────────────
    for (auto& jObj : root.value("gameObjects", nlohmann::json::array()))
    {
        auto* obj  = new GameObject();
        obj->name  = jObj.value("name", "GameObject");

        for (auto& jComp : jObj.value("components", nlohmann::json::array()))
        {
            std::string type = jComp.value("type", "");

            if (type == "Transform")
            {
                auto* tr = new Transform();
                obj->AddComponent(tr);
                tr->Deserialize(jComp);
            }
            else if (type == "SpriteRenderer")
            {
                auto* sr = new SpriteRenderer();
                obj->AddComponent(sr);
                sr->Deserialize(jComp);
            }
            else if (type == "Label")
            {
                auto* lb = new Label();
                obj->AddComponent(lb);
                lb->Deserialize(jComp);
            }
            // ScriptComponent 는 직렬화 대상 외. 필요 시 "script" 타입 추가 가능.
        }

        AddGameObject(obj);
    }
}
