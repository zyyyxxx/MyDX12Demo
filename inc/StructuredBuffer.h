#pragma once

#include "Buffer.h"

#include "ByteAddressBuffer.h"
class StructuredBuffer : public Buffer
{
public:
    StructuredBuffer( const std::wstring& name = L"" );
    StructuredBuffer( const D3D12_RESOURCE_DESC& resDesc, 
        size_t numElements, size_t elementSize,
        const std::wstring& name = L"");

    /**
    * 获取此缓冲区中包含的元素数。
    */
    virtual size_t GetNumElements() const
    {
        return m_NumElements;
    }

    /**
    * 获取此缓冲区中每个元素的大小（以字节为单位）。
    */
    virtual size_t GetElementSize() const
    {
        return m_ElementSize;
    }

    /**
     * 为缓冲区资源创建视图。
     * 由 CommandList 在设置缓冲区内容时使用。
     */
    virtual void CreateViews( size_t numElements, size_t elementSize ) override;

    /**
     * 获取资源的 SRV。
     */
    virtual D3D12_CPU_DESCRIPTOR_HANDLE GetShaderResourceView(const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc = nullptr) const
    {
        return m_SRV.GetDescriptorHandle();
    }

    /**
     * 获取（子）资源的 UAV
     */
    virtual D3D12_CPU_DESCRIPTOR_HANDLE GetUnorderedAccessView(const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc = nullptr) const override
    {
        // 缓冲区没有子资源。
        return m_UAV.GetDescriptorHandle();
    }

    const ByteAddressBuffer& GetCounterBuffer() const
    {
        return m_CounterBuffer;
    }

private:
    size_t m_NumElements;
    size_t m_ElementSize;

    DescriptorAllocation m_SRV;
    DescriptorAllocation m_UAV;

    // 用于存储结构化缓冲区的内部计数器的缓冲区。
    ByteAddressBuffer m_CounterBuffer;
};