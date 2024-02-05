#pragma once

#include "RootSignature.h"
#include "DescriptorAllocation.h"

#include <cstdint>

struct PreFilterCB
{
    //当前 mipmap 级别的立方体贴图面的大小（以像素为单位）
    uint32_t CubemapSize;
};

namespace PreFilterRS
{
    enum
    {
        PreFilterCB,
        SrcTexture,
        DstMips,
        NumRootParameters
    };
}
class PreFilterPSO
{
public:
    PreFilterPSO();

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
    // 默认（无资源）UAV 填充未使用的 UAV 描述符。
    //如果生成的 mip 映射级别少于 5 mip，则需要使用默认 UAV 填充未使用的 mip 映射（以保持 DX12 运行时正常运行）
    DescriptorAllocation m_DefaultUAV;
};
