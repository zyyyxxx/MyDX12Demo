#pragma once
#include <DX12LibPCH.h>
#include <d3d12.h>
#include <wrl.h>

#include <string>

/**
 *  @brief DX12资源的包装器。为所有其他资源类型（缓冲区和纹理）提供基类。
 */
class Resource
{
public:
    Resource(const std::wstring& name = L"");
    Resource(const D3D12_RESOURCE_DESC& resourceDesc, 
        const D3D12_CLEAR_VALUE* clearValue = nullptr,
        const std::wstring& name = L"");
    Resource(Microsoft::WRL::ComPtr<ID3D12Resource> resource, const std::wstring& name = L"");
    Resource(const Resource& copy);
    Resource(Resource&& copy) noexcept;

    Resource& operator=( const Resource& other);
    Resource& operator=(Resource&& other) noexcept;

    virtual ~Resource();

    /**
     * 检查基础资源是否有效
     */
    bool IsValid() const
    {
        return ( m_d3d12Resource != nullptr );
    }

    // 获取基础 D3D12 资源
    Microsoft::WRL::ComPtr<ID3D12Resource> GetD3D12Resource() const
    {
        return m_d3d12Resource;
    }

    D3D12_RESOURCE_DESC GetD3D12ResourceDesc() const
    {
        D3D12_RESOURCE_DESC resDesc = {};
        if ( m_d3d12Resource )
        {
            resDesc = m_d3d12Resource->GetDesc();
        }

        return resDesc;
    }

    // 替换 D3D12 资源
    // 只能由 CommandList 调用。
    virtual void SetD3D12Resource(Microsoft::WRL::ComPtr<ID3D12Resource> d3d12Resource, const D3D12_CLEAR_VALUE* clearValue = nullptr );

    /**
     * 获取资源的 SRV
     * 
     * @param srvDesc 要返回的 SRV 的 DESC。默认为 nullptr，返回资源的默认 SRV（在未提供说明时创建的 SRV）。
     */
    virtual D3D12_CPU_DESCRIPTOR_HANDLE GetShaderResourceView( const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc = nullptr ) const = 0;

    /**
     * 获取（子）资源的 UAV。
     * 
     * @param uavDesc 要返回的 UAV 的 DESC。
     */
    virtual D3D12_CPU_DESCRIPTOR_HANDLE GetUnorderedAccessView( const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc = nullptr ) const = 0;

    /**
     * 设置资源的名称 FOR DEBUG
     * 如果基础 D3D12 资源被 SetD3D12Resource 替换，资源的名称将保留。
     */
    void SetName(const std::wstring& name);

    /**
     * 释放基础资源。
     * 可用于调整交换链大小。
     */
    virtual void Reset();

protected:
    // 基础 D3D12 资源
    Microsoft::WRL::ComPtr<ID3D12Resource> m_d3d12Resource;
    std::unique_ptr<D3D12_CLEAR_VALUE> m_d3d12ClearValue; //用于指定资源清除值的结构体
    std::wstring m_ResourceName;
};