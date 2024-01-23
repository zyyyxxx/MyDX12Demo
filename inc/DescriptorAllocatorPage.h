#pragma once
#include <DescriptorAllocation.h>

#include <d3d12.h>

#include <wrl.h>

#include <map>
#include <memory>
#include <mutex>
#include <queue>

#include "d3dx12.h"

// 用于分配描述符的Page 包含 以偏移为索引的map 以及一个以大小为索引的multimap

class DescriptorAllocatorPage : public std::enable_shared_from_this<DescriptorAllocatorPage>
{
public:
    DescriptorAllocatorPage( D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors );
    
    D3D12_DESCRIPTOR_HEAP_TYPE GetHeapType() const;

    /**
    *检查此Page是否具有足够的连续大小，满足一定数量描述符块。
    */
    bool HasSpace( uint32_t numDescriptors ) const;

    /**
    * 获取堆中可用句柄的数量。
    */
    uint32_t NumFreeHandles() const;

    /**
    * 从此描述符堆中分配一定数量的描述符。如果无法满足分配，则返回 NULL 描述符。
    */
    DescriptorAllocation Allocate( uint32_t numDescriptors );

    /**
    * 返回描述符到堆中。
    * @param frameNumber 需要释放的描述符不会直接释放，而是放在队列中。使用 DescriptorAllocatorPage::ReleaseStaleAllocations
    */
    void Free( DescriptorAllocation&& descriptorHandle, uint64_t frameNumber );

    /**
    * 将无用的描述符返回到描述符堆。
    */
    void ReleaseStaleDescriptors( uint64_t frameNumber );
    
protected:

    // 计算描述符句柄从堆开始的偏移量
    uint32_t ComputeOffset( D3D12_CPU_DESCRIPTOR_HANDLE handle );

    // 将新块添加到可用列表
    void AddNewBlock( uint32_t offset, uint32_t numDescriptors );

    // 释放描述符块
    // 这也将合并可用列表中的可用块，以形成可以重复使用的更大块
    void FreeBlock( uint32_t offset, uint32_t numDescriptors );
    
private:
    // 描述符堆中的偏移数量类型
    using OffsetType = uint32_t;
    // 空闲列表中描述符的数量类型。
    using SizeType = uint32_t;
    
    struct FreeBlockInfo;
    
    // 按描述符堆中的偏移量（基地址）列出可用块的map类型
    using FreeListByOffset = std::map<OffsetType, FreeBlockInfo>;
    // 按数量列出可用块的map，多映射，因为多个块可以具有相同的数量。
    using FreeListBySize = std::multimap<SizeType, FreeListByOffset::iterator>;

    // 空闲块信息 内含指向按大小索引空闲块的multimap迭代器 与 大小
    struct FreeBlockInfo
    {
        FreeBlockInfo( SizeType size )
            : Size( size )
        {}

        // 数量
        SizeType Size;
        FreeListBySize::iterator FreeListBySizeIt;
    };

    // 等待释放的描述符信息
    struct StaleDescriptorInfo
    {
        StaleDescriptorInfo( OffsetType offset, SizeType size, uint64_t frame )
            : Offset( offset )
            , Size( size )
            , FrameNumber( frame )
        {}

        // 描述符堆中的偏移量
        OffsetType Offset;
        // 描述符数量
        SizeType Size;
        // 释放描述符的帧号
        uint64_t FrameNumber;
    };

    //等待释放的描述符队列类型
    using StaleDescriptorQueue = std::queue<StaleDescriptorInfo>;

    FreeListByOffset m_FreeListByOffset; //map
    FreeListBySize m_FreeListBySize; //multimap
    StaleDescriptorQueue m_StaleDescriptors;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_d3d12DescriptorHeap;
    D3D12_DESCRIPTOR_HEAP_TYPE m_HeapType;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_BaseDescriptor;
    uint32_t m_DescriptorHandleIncrementSize; //由于描述符堆中描述符的增量大小是特定于供应商的，因此必须在运行时查询它
    uint32_t m_NumDescriptorsInHeap;
    uint32_t m_NumFreeHandles;

    std::mutex m_AllocationMutex;
};
