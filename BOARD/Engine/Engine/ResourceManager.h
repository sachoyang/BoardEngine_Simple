// 텍스처 리소스의 로딩·캐싱·SRV 슬롯 할당을 담당하는 싱글톤 관리자
#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <string>
#include <unordered_map>
#include <memory>

using Microsoft::WRL::ComPtr;

// 로드된 텍스처 하나에 대한 핸들. SpriteRenderer 가 이 포인터를 저장한다.
struct TextureHandle
{
    ComPtr<ID3D12Resource>      resource;
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = {};
    UINT                        srvIndex  = 0;
};

class ResourceManager
{
public:
    // 텍스처 + ImGui 폰트를 합산한 최대 SRV 슬롯 수
    static constexpr UINT MAX_SRV_DESCRIPTORS = 64;

    static ResourceManager& Instance();

    // device, commandQueue 는 소유권 없이 빌린다. Engine::InitD3D12 에서 호출.
    void Init(ID3D12Device* device, ID3D12CommandQueue* commandQueue);
    void Shutdown();

    // SetDescriptorHeaps / ImGui_ImplDX12_InitInfo::SrvDescriptorHeap 에 전달
    ID3D12DescriptorHeap* GetSrvHeap() const;

    // ImGui SrvDescriptorAllocFn / FreeFn 에서 직접 호출한다.
    void AllocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* outCpu,
                            D3D12_GPU_DESCRIPTOR_HANDLE* outGpu);
    void FreeDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE cpu,
                        D3D12_GPU_DESCRIPTOR_HANDLE gpu);

    // Engine::InitD3D12 에서 1회 호출. 이후 LoadTexture 에 상대 경로가 들어오면
    // 이 디렉터리를 기준으로 절대 경로로 변환한다.
    void SetBasePath(const std::string& basePath);

    // 캐시 히트 → 기존 핸들 반환. 미스 → 파일 로드 후 SRV 등록.
    // 파일이 없으면 체커보드 폴백 텍스처를 생성해 캐싱 후 반환한다.
    const TextureHandle* LoadTexture(const std::string& path);

private:
    ResourceManager() = default;
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    // pixels → Default Heap 업로드 → SRV 생성 → 캐시 등록
    const TextureHandle* UploadPixels(const std::string& cacheKey,
                                       const BYTE* pixels, UINT width, UINT height);
    void WaitForUpload();

    ID3D12Device*       m_device       = nullptr;
    ID3D12CommandQueue* m_commandQueue = nullptr;

    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    UINT   m_srvIncrementSize = 0;
    UINT   m_nextFreeSlot     = 0;   // 다음에 할당할 슬롯 번호

    // Engine 실행 파일이 위치한 디렉터리. SetBasePath() 로 주입된다.
    // 상대 경로 → 절대 경로 변환의 기준점.
    std::string m_basePath;

    ComPtr<ID3D12Fence> m_fence;
    HANDLE              m_fenceEvent = nullptr;
    UINT64              m_fenceValue = 0;

    // unique_ptr 을 값으로 사용하므로 unordered_map 리해시 시에도
    // TextureHandle 의 주소가 변하지 않는다 (이동 시 포인터 값은 유지).
    std::unordered_map<std::string, std::unique_ptr<TextureHandle>> m_cache;
};
