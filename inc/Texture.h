#pragma once
#include "Resource.h"
#include <DescriptorAllocation.h>
#include <TextureUsage.h>

#include "d3dx12.h"

#include <mutex>
#include <unordered_map>


/**
 *  @brief DX12 Texture 对象包装器
 */
class Texture : public Resource
{
public:
    explicit Texture(TextureUsage textureUsage = TextureUsage::Albedo,
                     const std::wstring& name = L"");
    explicit Texture(const D3D12_RESOURCE_DESC& resourceDesc,
                     const D3D12_CLEAR_VALUE* clearValue = nullptr,
                     TextureUsage textureUsage = TextureUsage::Albedo,
                     const std::wstring& name = L"");
    explicit Texture(Microsoft::WRL::ComPtr<ID3D12Resource> resource,
                     TextureUsage textureUsage = TextureUsage::Albedo,
                     const std::wstring& name = L"");

    Texture(const Texture& copy);
    Texture( Texture&& copy );

    Texture& operator=(const Texture& other);
    Texture& operator=(Texture&& other);

    virtual ~Texture();

    TextureUsage GetTextureUsage() const
    {
        return m_TextureUsage;
    }

    void SetTextureUsage( TextureUsage textureUsage )
    {
        m_TextureUsage = textureUsage;
    }

    /**
    * 调整纹理大小
    */
    void Resize(uint32_t width, uint32_t height, uint32_t depthOrArraySize = 1 );

    /**
     * 为资源创建 SRV 和 UAV
     */
    virtual void CreateViews();

    /**
      * 获取资源的 SRV。
      */
    virtual D3D12_CPU_DESCRIPTOR_HANDLE GetShaderResourceView(const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc = nullptr) const override;


    /**
    * 获取（子）资源的 UAV。
    */
    virtual D3D12_CPU_DESCRIPTOR_HANDLE GetUnorderedAccessView(const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc = nullptr) const override;

    /**
     * 获取纹理的 RTV。
     */
    virtual D3D12_CPU_DESCRIPTOR_HANDLE GetRenderTargetView() const;
     
    /**
     * 获取纹理的 DSV。
     */
    virtual D3D12_CPU_DESCRIPTOR_HANDLE GetDepthStencilView() const;

    // Check static function
    static bool CheckSRVSupport(D3D12_FORMAT_SUPPORT1 formatSupport)
    {
        return ( ( formatSupport & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE ) != 0 ||
            ( formatSupport & D3D12_FORMAT_SUPPORT1_SHADER_LOAD ) != 0 );
    }

    static bool CheckRTVSupport(D3D12_FORMAT_SUPPORT1 formatSupport)
    {
        return ( ( formatSupport & D3D12_FORMAT_SUPPORT1_RENDER_TARGET ) != 0 );
    }

    static bool CheckUAVSupport(D3D12_FORMAT_SUPPORT1 formatSupport )
    {
        return ( ( formatSupport & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) != 0 );
    }

    static bool CheckDSVSupport(D3D12_FORMAT_SUPPORT1 formatSupport)
    {
        return ( ( formatSupport & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL ) != 0 );
    }

    static bool IsUAVCompatibleFormat(DXGI_FORMAT format);
    static bool IsSRGBFormat(DXGI_FORMAT format);
    static bool IsBGRFormat(DXGI_FORMAT format);
    static bool IsDepthFormat(DXGI_FORMAT format);

    // 从给定格式返回无类型格式
    static DXGI_FORMAT GetTypelessFormat(DXGI_FORMAT format);
    
private:
    DescriptorAllocation CreateShaderResourceView(const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc) const;
    DescriptorAllocation CreateUnorderedAccessView(const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc) const;

    mutable std::unordered_map<size_t, DescriptorAllocation> m_ShaderResourceViews; // SRVs
    mutable std::unordered_map<size_t, DescriptorAllocation> m_UnorderedAccessViews; // UAVs

    mutable std::mutex m_ShaderResourceViewsMutex;
    mutable std::mutex m_UnorderedAccessViewsMutex;

    DescriptorAllocation m_RenderTargetView; // RTV
    DescriptorAllocation m_DepthStencilView; // DSV

    TextureUsage m_TextureUsage;
};
