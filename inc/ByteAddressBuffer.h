#pragma once
#include "Buffer.h"
#include "DescriptorAllocation.h"

#include <d3dx12.h>

class ByteAddressBuffer : public Buffer
{
public:
    ByteAddressBuffer( const std::wstring& name = L"" );
    ByteAddressBuffer( const D3D12_RESOURCE_DESC& resDesc, 
        size_t numElements, size_t elementSize,
        const std::wstring& name = L"");

    size_t GetBufferSize() const
    {
        return m_BufferSize;
    }

    /**
     * 为缓冲区资源创建视图。
     * 由 CommandList 在设置缓冲区内容时使用。
     */
    virtual void CreateViews( size_t numElements, size_t elementSize ) override;

    /**
     * 获取资源的 SRV。
     */
    virtual D3D12_CPU_DESCRIPTOR_HANDLE GetShaderResourceView(const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc = nullptr) const override
    {
        return m_SRV.GetDescriptorHandle();
    }

    /**
     * 获取（子）资源的 UAV。
     */
    virtual D3D12_CPU_DESCRIPTOR_HANDLE GetUnorderedAccessView(const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc = nullptr) const override
    {
        // 缓冲区只有一个子资源。
        return m_UAV.GetDescriptorHandle();
    }

protected:

private:
    size_t m_BufferSize;

    DescriptorAllocation m_SRV;
    DescriptorAllocation m_UAV;
};