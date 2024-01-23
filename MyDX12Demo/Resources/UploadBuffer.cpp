#include <DX12LibPCH.h>
 
#include <UploadBuffer.h>
 
#include <Application.h>
#include <Helpers.h>
 
UploadBuffer::UploadBuffer(size_t pageSize)
    : m_PageSize(pageSize)
{}


UploadBuffer::~UploadBuffer()
{}


UploadBuffer::Allocation UploadBuffer::Allocate(size_t sizeInBytes, size_t alignment)
{
    if (sizeInBytes > m_PageSize)
    {
        throw std::bad_alloc();
    }
    // 如果没有当前页，或者请求的分配超出当前页剩余空间，则请求新页。
    if (!m_CurrentPage || !m_CurrentPage->HasSpace(sizeInBytes, alignment))
    {
        m_CurrentPage = RequestPage();
    }

    return m_CurrentPage->Allocate(sizeInBytes, alignment);
}

std::shared_ptr<UploadBuffer::Page> UploadBuffer::RequestPage()
{
    std::shared_ptr<Page> page;

    if (!m_AvailablePages.empty())
    {
        page = m_AvailablePages.front();
        m_AvailablePages.pop_front();
    }
    else
    {
        page = std::make_shared<Page>(m_PageSize);
        m_PagePool.push_back(page);
    }

    return page;
}

void UploadBuffer::Reset()
{
    m_CurrentPage = nullptr;
   
    m_AvailablePages = m_PagePool;

    for ( auto page : m_AvailablePages )
    {
        // Reset the page for new allocations.
        page->Reset();
    }
}

UploadBuffer::Page::Page(size_t sizeInBytes)
    : m_CPUPtr(nullptr)
    , m_GPUPtr(D3D12_GPU_VIRTUAL_ADDRESS(0))
    , m_PageSize(sizeInBytes)
    , m_Offset(0)
{
    auto device = Application::Get().GetDevice();

    // 上传堆
    CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(m_PageSize);
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_d3d12Resource)
    ));

    // 检索 GPU 和 CPU 地址
    m_GPUPtr = m_d3d12Resource->GetGPUVirtualAddress();
    m_d3d12Resource->Map(0, nullptr, &m_CPUPtr);
}

UploadBuffer::Page::~Page()
{
    m_d3d12Resource->Unmap(0, nullptr);
    m_CPUPtr = nullptr;
    m_GPUPtr = D3D12_GPU_VIRTUAL_ADDRESS(0);
}

bool UploadBuffer::Page::HasSpace(size_t sizeInBytes, size_t alignment ) const
{
    size_t alignedSize = Math::AlignUp(sizeInBytes, alignment);
    size_t alignedOffset = Math::AlignUp(m_Offset, alignment);

    return alignedOffset + alignedSize <= m_PageSize;
}

UploadBuffer::Allocation UploadBuffer::Page::Allocate(size_t sizeInBytes, size_t alignment)
{
    if (!HasSpace(sizeInBytes, alignment))
    {
        throw std::bad_alloc();
    }
 
    size_t alignedSize = Math::AlignUp(sizeInBytes, alignment);
    m_Offset = Math::AlignUp(m_Offset, alignment);
 
    Allocation allocation;
    allocation.CPU = static_cast<uint8_t*>(m_CPUPtr) + m_Offset;
    allocation.GPU = m_GPUPtr + m_Offset;
 
    m_Offset += alignedSize;
 
    return allocation;
}

void UploadBuffer::Page::Reset()
{
    m_Offset = 0;
}