#pragma once



#include "d3dx12.h"

#include <wrl.h>

#include <vector>

/**
 * @brief RootSignature类封装了ID3D12RootSignature和用于创建它的D3D12_ROOT_SIGNATURE_DESC。这提供了DynamicDescriptorHeap在运行时确定根签名布局所需的功能。
 */
class RootSignature
{
public:
    RootSignature();
    RootSignature(
        const D3D12_ROOT_SIGNATURE_DESC1& rootSignatureDesc, 
        D3D_ROOT_SIGNATURE_VERSION rootSignatureVersion
    );

    virtual ~RootSignature();

    void Destroy();

    Microsoft::WRL::ComPtr<ID3D12RootSignature> GetRootSignature() const
    {
        return m_RootSignature;
    }

    void SetRootSignatureDesc(
        const D3D12_ROOT_SIGNATURE_DESC1& rootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION rootSignatureVersion
    );

    const D3D12_ROOT_SIGNATURE_DESC1& GetRootSignatureDesc() const
    {
        return m_RootSignatureDesc;
    }

    uint32_t GetDescriptorTableBitMask(D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapType) const;
    uint32_t GetNumDescriptors(uint32_t rootIndex) const;

protected:

private:
    D3D12_ROOT_SIGNATURE_DESC1 m_RootSignatureDesc;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_RootSignature;
    
    // 需要知道每个描述符表的描述符数量。
    // 支持最多32个描述符表（因为在根签名中使用32位掩码表示描述符表）。
    uint32_t m_NumDescriptorsPerTable[32];

    // 表示作为采样器的根参数索引的位掩码。
    uint32_t m_SamplerTableBitMask;
    // 表示是CBV、UAV和SRV描述符表的根参数索引的位掩码。
    uint32_t m_DescriptorTableBitMask;
};