#pragma once
#include "DescriptorAllocation.h"

#include "d3dx12.h"

#include <cstdint>
#include <mutex>
#include <memory>
#include <set>
#include <vector>

class DescriptorAllocatorPage;

// 主接口
// 使用空闲列表内存分配方案来管理描述符 （首次适应）
// 渲染目标视图 (RTV)
// 深度模板视图 (DSV)
// 常量缓冲区视图 (CBV)
// 着色器资源视图 (SRV)
// 无序访问视图 (UAV)
// 采样器
class DescriptorAllocator 
{
public:
    DescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptorsPerHeap = 256);
    virtual ~DescriptorAllocator();
    
    /**
     * 从一个CPU可见的描述符堆中分配，分配多个连续的描述符，
     * @param numDescriptors 分配数量
     * 无法大于每个描述符堆的描述符数量。
     */
    DescriptorAllocation Allocate(uint32_t numDescriptors = 1);

    /**
    * 当一帧完成时，释放无效的描述符。
    */
    void ReleaseStaleDescriptors(uint64_t frameNumber);

private:
    using DescriptorHeapPool = std::vector<std::shared_ptr<DescriptorAllocatorPage>>;

    // 使用特定数量的描述符创建新堆
    std::shared_ptr<DescriptorAllocatorPage> CreateAllocatorPage();

    D3D12_DESCRIPTOR_HEAP_TYPE m_HeapType;
    uint32_t m_NumDescriptorsPerHeap;

    // 堆池
    DescriptorHeapPool m_HeapPool;
    // 堆池中可用堆的索引
    std::set<size_t> m_AvailableHeaps;

    std::mutex m_AllocationMutex;
};
