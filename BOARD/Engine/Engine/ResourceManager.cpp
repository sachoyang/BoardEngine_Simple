// ResourceManager 구현 — SRV 힙 관리, 텍스처 파일 로딩, 캐시 처리
#include "ResourceManager.h"
#include "d3dx12.h"
#include <cassert>
#include <vector>
#include <filesystem>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// =====================================================================
// 싱글톤 접근자
// =====================================================================

ResourceManager& ResourceManager::Instance()
{
    static ResourceManager s_instance;
    return s_instance;
}

// =====================================================================
// 초기화 / 종료
// =====================================================================

void ResourceManager::Init(ID3D12Device* device, ID3D12CommandQueue* commandQueue)
{
    m_device       = device;
    m_commandQueue = commandQueue;

    // GPU-visible SRV Descriptor Heap 생성
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = MAX_SRV_DESCRIPTORS;
    heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    HRESULT hr = m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_srvHeap));
    assert(SUCCEEDED(hr));

    m_srvIncrementSize = m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
    );

    // 텍스처 업로드 완료 대기용 Fence
    hr = m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    assert(SUCCEEDED(hr));
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    assert(m_fenceEvent);
}

void ResourceManager::Shutdown()
{
    m_cache.clear();
    m_fence.Reset();
    m_srvHeap.Reset();
    if (m_fenceEvent)
    {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }
    m_device       = nullptr;
    m_commandQueue = nullptr;
    m_nextFreeSlot = 0;
    m_fenceValue   = 0;
}

// =====================================================================
// Descriptor Heap 접근
// =====================================================================

ID3D12DescriptorHeap* ResourceManager::GetSrvHeap() const
{
    return m_srvHeap.Get();
}

void ResourceManager::AllocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* outCpu,
                                          D3D12_GPU_DESCRIPTOR_HANDLE* outGpu)
{
    assert(m_nextFreeSlot < MAX_SRV_DESCRIPTORS && "SRV Descriptor Heap 슬롯 초과");
    outCpu->ptr = m_srvHeap->GetCPUDescriptorHandleForHeapStart().ptr
                + static_cast<SIZE_T>(m_nextFreeSlot) * m_srvIncrementSize;
    outGpu->ptr = m_srvHeap->GetGPUDescriptorHandleForHeapStart().ptr
                + static_cast<UINT64>(m_nextFreeSlot) * m_srvIncrementSize;
    ++m_nextFreeSlot;
}

void ResourceManager::FreeDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE,
                                      D3D12_GPU_DESCRIPTOR_HANDLE)
{
    // 선형 할당기이므로 개별 슬롯 반환 없음
}

// =====================================================================
// 경로 해석
// =====================================================================

void ResourceManager::SetBasePath(const std::string& basePath)
{
    m_basePath = basePath;
}

// =====================================================================
// 텍스처 로딩 (캐싱)
// =====================================================================

const TextureHandle* ResourceManager::LoadTexture(const std::string& path)
{
    // ── 절대 경로 변환 ───────────────────────────────────────────────
    // 상대 경로가 들어오면 SetBasePath() 로 주입된 exe 디렉터리를 기준으로 변환한다.
    // 이미 절대 경로이면 그대로 사용한다.
    namespace fs = std::filesystem;
    std::string absPath = path;
    if (!m_basePath.empty() && !fs::path(path).is_absolute())
        absPath = (fs::path(m_basePath) / path).string();

    // ── 캐시 히트 ───────────────────────────────────────────────────
    auto it = m_cache.find(absPath);
    if (it != m_cache.end())
        return it->second.get();

    // ── 파일에서 로드 ───────────────────────────────────────────────
    int w, h, ch;
    BYTE* pixels = stbi_load(absPath.c_str(), &w, &h, &ch, 4);
    if (pixels)
    {
        const TextureHandle* handle = UploadPixels(absPath, pixels,
                                                    static_cast<UINT>(w),
                                                    static_cast<UINT>(h));
        stbi_image_free(pixels);
        return handle;
    }

    // ── 파일 없음 → 64×64 체커보드 폴백 ────────────────────────────
    const UINT fw = 64, fh = 64, cellSize = 8;
    std::vector<BYTE> fallback(fw * fh * 4);
    for (UINT y = 0; y < fh; y++)
    {
        for (UINT x = 0; x < fw; x++)
        {
            UINT idx    = (y * fw + x) * 4;
            bool bright = ((x / cellSize) + (y / cellSize)) % 2 == 0;
            fallback[idx + 0] = bright ? 230 :  60;
            fallback[idx + 1] = bright ? 180 :  40;
            fallback[idx + 2] = bright ?  80 :  20;
            fallback[idx + 3] = 255;
        }
    }
    return UploadPixels(absPath, fallback.data(), fw, fh);
}

// =====================================================================
// 내부: GPU 업로드 + SRV 생성
// =====================================================================

const TextureHandle* ResourceManager::UploadPixels(const std::string& cacheKey,
                                                     const BYTE*        pixels,
                                                     UINT               width,
                                                     UINT               height)
{
    auto handle = std::make_unique<TextureHandle>();

    // ── [1] Default Heap 에 Texture2D 리소스 생성 ────────────────────
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width            = width;
    texDesc.Height           = height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels        = 1;
    texDesc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    HRESULT hr = m_device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&handle->resource)
    );
    assert(SUCCEEDED(hr));

    // ── [2] Upload Buffer 생성 ────────────────────────────────────────
    UINT64 uploadSize = GetRequiredIntermediateSize(handle->resource.Get(), 0, 1);

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC uploadDesc = {};
    uploadDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width            = uploadSize;
    uploadDesc.Height           = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels        = 1;
    uploadDesc.Format           = DXGI_FORMAT_UNKNOWN;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> uploadBuffer;
    hr = m_device->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&uploadBuffer)
    );
    assert(SUCCEEDED(hr));

    // ── [3] 임시 Command List 로 업로드 + 상태 전환 기록 ─────────────
    ComPtr<ID3D12CommandAllocator> cmdAlloc;
    hr = m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           IID_PPV_ARGS(&cmdAlloc));
    assert(SUCCEEDED(hr));

    ComPtr<ID3D12GraphicsCommandList> cmdList;
    hr = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                      cmdAlloc.Get(), nullptr,
                                      IID_PPV_ARGS(&cmdList));
    assert(SUCCEEDED(hr));

    D3D12_SUBRESOURCE_DATA subData = {};
    subData.pData      = pixels;
    subData.RowPitch   = static_cast<LONG_PTR>(width) * 4;
    subData.SlicePitch = static_cast<LONG_PTR>(width) * height * 4;

    UpdateSubresources<1>(cmdList.Get(), handle->resource.Get(),
                          uploadBuffer.Get(), 0, 0, 1, &subData);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = handle->resource.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    cmdList->ResourceBarrier(1, &barrier);

    hr = cmdList->Close();
    assert(SUCCEEDED(hr));

    ID3D12CommandList* lists[] = { cmdList.Get() };
    m_commandQueue->ExecuteCommandLists(1, lists);
    WaitForUpload();
    // uploadBuffer: WaitForUpload 이후 ComPtr 소멸 → 안전

    // ── [4] SRV 슬롯 할당 + 뷰 생성 ─────────────────────────────────
    AllocateDescriptor(&handle->cpuHandle, &handle->gpuHandle);
    handle->srvIndex = m_nextFreeSlot - 1;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels     = 1;
    m_device->CreateShaderResourceView(handle->resource.Get(), &srvDesc,
                                        handle->cpuHandle);

    // ── [5] 캐시에 등록 ──────────────────────────────────────────────
    TextureHandle* raw = handle.get();
    m_cache[cacheKey]  = std::move(handle);
    return raw;
}

void ResourceManager::WaitForUpload()
{
    ++m_fenceValue;
    m_commandQueue->Signal(m_fence.Get(), m_fenceValue);
    if (m_fence->GetCompletedValue() < m_fenceValue)
    {
        m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}
