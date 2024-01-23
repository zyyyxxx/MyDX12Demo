#pragma once
#include <d3d12.h>
 
#include <cstdint>
#include <memory>
 
class DescriptorAllocatorPage;

// 描述符堆中连续描述符的单个分配
class DescriptorAllocation
{
public:
    // 创建 NULL 描述符.
    DescriptorAllocation();
 
    DescriptorAllocation( D3D12_CPU_DESCRIPTOR_HANDLE descriptor, uint32_t numHandles, uint32_t descriptorSize, std::shared_ptr<DescriptorAllocatorPage> page );
 
    // 析构函数自动释放分配
    ~DescriptorAllocation();

    // 禁用拷贝
    DescriptorAllocation( const DescriptorAllocation& ) = delete;
    DescriptorAllocation& operator=( const DescriptorAllocation& ) = delete;
 
    // 移动构造 移动赋值
    DescriptorAllocation( DescriptorAllocation&& allocation );
    DescriptorAllocation& operator=( DescriptorAllocation&& other );

    // 检查是否是有效的描述符。
    bool IsNull() const;

    // 获取分配中特定偏移量处的描述符。 
    D3D12_CPU_DESCRIPTOR_HANDLE GetDescriptorHandle( uint32_t offset = 0 ) const;

    //获取此分配的（连续）句柄数.
    uint32_t GetNumHandles() const;

    // 获取此分配的来源堆Page
    std::shared_ptr<DescriptorAllocatorPage> GetDescriptorAllocatorPage() const;

private:
    // 将描述符释放回来源堆。
    void Free();

    // 基描述符
    D3D12_CPU_DESCRIPTOR_HANDLE m_Descriptor;
    // 此分配中描述符的数量。
    uint32_t m_NumHandles;
    // 下一个描述符的偏移量。
    uint32_t m_DescriptorSize;

    // 指向此分配来源的原始Page的指针。
    std::shared_ptr<DescriptorAllocatorPage> m_Page;
};