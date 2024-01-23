#pragma once
#include "RootSignature.h"
#include "DescriptorAllocation.h"

#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl.h>

struct alignas( 16 ) GenerateMipsCB // Constant Buffer
{
    uint32_t SrcMipLevel; // 源 mip 的纹理级别
    uint32_t NumMipLevels; // 要写入的 OutMips 数量：[1-4]
    uint32_t SrcDimension; // 源纹理的宽度和高度为偶数或奇数。
    uint32_t Padding; // 16 字节的对齐。
    DirectX::XMFLOAT2 TexelSize; // 1.0 / OutMip1.Dimensions
};

namespace GenerateMips
{
    enum
    {
        GenerateMipsCB,
        SrcMip,
        OutMip,
        NumRootParameters
    };
}

/**
  *  @brief PSO 用于生成 Mip Map.
  */
class GenerateMipsPSO
{
public:
    GenerateMipsPSO();

    const RootSignature& GetRootSignature() const
    {
        return m_RootSignature;
    }

    Microsoft::WRL::ComPtr<ID3D12PipelineState> GetPipelineState() const
    {
        return m_PipelineState;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetDefaultUAV() const
    {
        return m_DefaultUAV.GetDescriptorHandle();
    }

private:
    RootSignature m_RootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_PipelineState;
    
    //默认（无资源）UAV 填充未使用的 UAV 描述符.
    // 如果生成的 mip 映射级别少于 4 mip ，则需要使用默认 UAV 填充未使用的 mip 映射（以保持 DX12 运行时正常运行）。
    DescriptorAllocation m_DefaultUAV;
    
};
