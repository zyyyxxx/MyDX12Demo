#include <DX12LibPCH.h>

#include <DescriptorAllocatorPage.h>
#include <Application.h>

DescriptorAllocatorPage::DescriptorAllocatorPage( D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors )
    : m_HeapType( type )
    , m_NumDescriptorsInHeap( numDescriptors )
{
    auto device = Application::Get().GetDevice();

    // 创建 heap
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = m_HeapType;
    heapDesc.NumDescriptors = m_NumDescriptorsInHeap;

    ThrowIfFailed( device->CreateDescriptorHeap( &heapDesc, IID_PPV_ARGS( &m_d3d12DescriptorHeap ) ) );

    m_BaseDescriptor = m_d3d12DescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    m_DescriptorHandleIncrementSize = device->GetDescriptorHandleIncrementSize( m_HeapType );
    m_NumFreeHandles = m_NumDescriptorsInHeap;

    // 初始化空闲链表
    AddNewBlock( 0, m_NumFreeHandles );
}

D3D12_DESCRIPTOR_HEAP_TYPE DescriptorAllocatorPage::GetHeapType() const
{
    return m_HeapType;
}

uint32_t DescriptorAllocatorPage::NumFreeHandles() const
{
    return m_NumFreeHandles;
}

bool DescriptorAllocatorPage::HasSpace( uint32_t numDescriptors ) const
{
    //查找空闲列表中大于或等于所请求的描述符数量的第一个条目
    return m_FreeListBySize.lower_bound(numDescriptors) != m_FreeListBySize.end();
}

void DescriptorAllocatorPage::AddNewBlock( uint32_t offset, uint32_t numDescriptors )
{
    auto offsetIt = m_FreeListByOffset.emplace( offset, numDescriptors );
    auto sizeIt = m_FreeListBySize.emplace( numDescriptors, offsetIt.first );
    offsetIt.first->second.FreeListBySizeIt = sizeIt;
}

DescriptorAllocation DescriptorAllocatorPage::Allocate( uint32_t numDescriptors )
{
    std::lock_guard<std::mutex> lock( m_AllocationMutex );

    // 堆中剩余的描述符数量少于请求的数量。
    // 返回 NULL 描述符并尝试另一个堆。
    if ( numDescriptors > m_NumFreeHandles )
    {
        return DescriptorAllocation();
    }
 
    // 获取第一个足够数量的块来满足请求
    auto smallestBlockIt = m_FreeListBySize.lower_bound( numDescriptors );
    if ( smallestBlockIt == m_FreeListBySize.end() )
    {
        // 没有可以满足请求的空闲块。
        return DescriptorAllocation();
    }
    
    // 满足请求的最小块的大小。
    auto blockSize = smallestBlockIt->first;

    // 指向 FreeListByOffset map 中同一条目的指针。
    auto offsetIt = smallestBlockIt->second;

    // 描述符堆中的偏移量。
    auto offset = offsetIt->first;

    // 从可用列表中删除请求的可用块。
    m_FreeListBySize.erase( smallestBlockIt );
    m_FreeListByOffset.erase( offsetIt );

    // 计算拆分此块后产生的新可用块。
    auto newOffset = offset + numDescriptors;
    auto newSize = blockSize - numDescriptors;

    if ( newSize > 0 )
    {
        // 如果分配与请求的大小不完全匹配，将剩余部分返回到空闲列表.
        AddNewBlock( newOffset, newSize );
    }

    // 空闲描述符句柄--
    m_NumFreeHandles -= numDescriptors;

    return DescriptorAllocation(
        CD3DX12_CPU_DESCRIPTOR_HANDLE( m_BaseDescriptor, offset, m_DescriptorHandleIncrementSize ),
        numDescriptors, m_DescriptorHandleIncrementSize, shared_from_this() );
}

uint32_t DescriptorAllocatorPage::ComputeOffset( D3D12_CPU_DESCRIPTOR_HANDLE handle )
{
    return static_cast<uint32_t>( handle.ptr - m_BaseDescriptor.ptr ) / m_DescriptorHandleIncrementSize;
}

void DescriptorAllocatorPage::Free( DescriptorAllocation&& descriptor, uint64_t frameNumber )
{
    // 计算描述符堆中描述符的偏移量
    auto offset = ComputeOffset( descriptor.GetDescriptorHandle() );

    std::lock_guard<std::mutex> lock( m_AllocationMutex );

    // 描述符不会立即返回到空闲列表，而是添加到陈旧描述符队列中
    m_StaleDescriptors.emplace( offset, descriptor.GetNumHandles(), frameNumber );
}

void DescriptorAllocatorPage::FreeBlock( uint32_t offset, uint32_t numDescriptors )
{
    // 查找偏移量大于指定偏移量的第一个元素
    // 这是应在要释放的块之后出现的块
    auto nextBlockIt = m_FreeListByOffset.upper_bound( offset );

    // 释放块之前的块
    auto prevBlockIt = nextBlockIt;
    // 如果它不是列表中的第一个块
    if ( prevBlockIt != m_FreeListByOffset.begin() )
    {
        // 设置prevBlockIt
        --prevBlockIt;
    }
    else
    {
        //否则，只需将其设置为列表的末尾，表示在被释放的块之前没有块
        prevBlockIt = m_FreeListByOffset.end();
    }

    // 将可用句柄数重新添加到堆中。
    // 在合并任何块之前完成，因为合并块会修改 numDescriptors 变量。
    m_NumFreeHandles += numDescriptors;

    if ( prevBlockIt != m_FreeListByOffset.end() &&
         offset == prevBlockIt->first + prevBlockIt->second.Size )
    {
        // 放置释放块至末尾后 若前一个块正好位于要释放的块后面
        //
        // PrevBlock.Offset           Offset
        // |                          |
        // |<-----PrevBlock.Size----->|<------Size-------->|
        //

        // 与前一个块合并的大小来增加块大小。
        offset = prevBlockIt->first;
        numDescriptors += prevBlockIt->second.Size;

        // 从空闲列表中删除上一个块
        m_FreeListBySize.erase( prevBlockIt->second.FreeListBySizeIt );
        m_FreeListByOffset.erase( prevBlockIt );
    }

    if ( nextBlockIt != m_FreeListByOffset.end() &&
         offset + numDescriptors == nextBlockIt->first )
    {
        // 下一个块正好位于要释放的块的前面。
        //
        // Offset               NextBlock.Offset 
        // |                    |
        // |<------Size-------->|<-----NextBlock.Size----->|

        // 通过与下一个块合并的大小来增加块大小。
        numDescriptors += nextBlockIt->second.Size;

        // 从可用列表中删除下一个块.
        m_FreeListBySize.erase( nextBlockIt->second.FreeListBySizeIt );
        m_FreeListByOffset.erase( nextBlockIt );
    }

    // 将释放的块添加到可用列表中。
    AddNewBlock( offset, numDescriptors );
}

void DescriptorAllocatorPage::ReleaseStaleDescriptors( uint64_t frameNumber )
{
    std::lock_guard<std::mutex> lock( m_AllocationMutex );

    while ( !m_StaleDescriptors.empty() && m_StaleDescriptors.front().FrameNumber <= frameNumber )
    {
        auto& staleDescriptor = m_StaleDescriptors.front();

        // 堆中描述符的偏移量
        auto offset = staleDescriptor.Offset;
        // 已分配的描述符数量
        auto numDescriptors = staleDescriptor.Size;

        FreeBlock( offset, numDescriptors );

        m_StaleDescriptors.pop();
    }
}