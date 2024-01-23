#pragma once

#include <cstdint>
#include <vector>

#include "Texture.h"

// 不要使用 C++11 引入的作用域枚举（scoped enums）。作用域枚举会将枚举值限制在枚举的作用域内，需要进行显式转换才能用作数组索引
enum AttachmentPoint
{
    Color0,
    Color1,
    Color2,
    Color3,
    Color4,
    Color5,
    Color6,
    Color7,
    DepthStencil,
    NumAttachmentPoints
};

/**
  *  @brief Render Target 存储一组用于渲染目标的纹理。
  * 可绑定到 Render Target 的 Color Texture的最大数量为 8 （0 - 7） 以及 一个Depth Stencil Buffer。
  */
class RenderTarget
{
public:
    // 创建一个空的RT。
    RenderTarget();

    RenderTarget( const RenderTarget& copy ) = default;
    RenderTarget( RenderTarget&& copy ) = default;

    RenderTarget& operator=( const RenderTarget& other ) = default;
    RenderTarget& operator=( RenderTarget&& other ) = default;

    // 将纹理附加到RT。
    // 纹理将被复制到纹理数组中
    void AttachTexture( AttachmentPoint attachmentPoint, const Texture& texture );
    const Texture& GetTexture( AttachmentPoint attachmentPoint ) const;

    // 调整与RT关联的所有纹理的大小。
    void Resize( uint32_t width, uint32_t height );

    // 获取附加到RT的纹理列表。
    // 此方法主要由 CommandList 在将RT绑定到渲染管线的输出合并阶段（Output Merger）时使用。
    const std::vector<Texture>& GetTextures() const;

    // 获取当前附加到此RT对象的纹理的RT格式。
    // 这是配置PSO所必需的。
    D3D12_RT_FORMAT_ARRAY GetRenderTargetFormats() const;

    // 获取附加的深度/模具缓冲区的格式
    DXGI_FORMAT GetDepthStencilFormat() const;

private:
    
    std::vector<Texture> m_Textures;
};
