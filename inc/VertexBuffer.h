#pragma once

#include "Buffer.h"

class VertexBuffer : public Buffer
{
public:
    VertexBuffer(const std::wstring& name = L"");
    virtual ~VertexBuffer();
    
    virtual void CreateViews(size_t numElements, size_t elementSize) override;

    /**
     * 获取用于绑定到输入装配阶段(IA Input Assembler)的顶点缓冲区视图。
     */
    D3D12_VERTEX_BUFFER_VIEW GetVertexBufferView() const
    {
        return m_VertexBufferView;
    }

    size_t GetNumVertices() const
    {
        return m_NumVertices;
    }

    size_t GetVertexStride() const
    {
        return m_VertexStride;
    }

    /**
      *  获取资源的 SRV。
      */
    virtual D3D12_CPU_DESCRIPTOR_HANDLE GetShaderResourceView(const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc = nullptr) const override;

    /**
      * 获取（子）资源的 UAV。
      */
    virtual D3D12_CPU_DESCRIPTOR_HANDLE GetUnorderedAccessView(const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc = nullptr) const override;

    
private:
    size_t m_NumVertices;
    size_t m_VertexStride;

    D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView;
};
