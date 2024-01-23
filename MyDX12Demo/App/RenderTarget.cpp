#include <DX12LibPCH.h>

#include <RenderTarget.h>

RenderTarget::RenderTarget()
    : m_Textures(AttachmentPoint::NumAttachmentPoints)
{}


void RenderTarget::AttachTexture( AttachmentPoint attachmentPoint, const Texture& texture )
{
    m_Textures[attachmentPoint] = texture;
}

const Texture& RenderTarget::GetTexture( AttachmentPoint attachmentPoint ) const
{
    return m_Textures[attachmentPoint];
}

void RenderTarget::Resize( uint32_t width, uint32_t height )
{
    for ( auto& texture : m_Textures )
    {
        texture.Resize( width, height );
    }
}

const std::vector<Texture>& RenderTarget::GetTextures() const
{
    return m_Textures;
}

D3D12_RT_FORMAT_ARRAY RenderTarget::GetRenderTargetFormats() const
{
    D3D12_RT_FORMAT_ARRAY rtvFormats = {};


    for ( int i = AttachmentPoint::Color0; i <= AttachmentPoint::Color7; ++i )
    {
        const Texture& texture = m_Textures[i];
        if ( texture.IsValid() )
        {
            rtvFormats.RTFormats[rtvFormats.NumRenderTargets++] = texture.GetD3D12ResourceDesc().Format;
        }
    }

    return rtvFormats;
}

DXGI_FORMAT RenderTarget::GetDepthStencilFormat() const
{
    DXGI_FORMAT dsvFormat = DXGI_FORMAT_UNKNOWN;
    const Texture& depthStencilTexture = m_Textures[AttachmentPoint::DepthStencil];
    if ( depthStencilTexture.IsValid() )
    {
        dsvFormat = depthStencilTexture.GetD3D12ResourceDesc().Format;
    }

    return dsvFormat;
}

