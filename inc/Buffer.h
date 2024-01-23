#pragma once
#include "Resource.h"

class Buffer : public Resource
{
public:
    Buffer(const std::wstring& name = L"");
    
    Buffer( const D3D12_RESOURCE_DESC& resDesc,
        size_t numElements, size_t elementSize,
        const std::wstring& name = L"" );
    
    /**
     * 为缓冲区资源创建视图
     * 由 CommandList 在设置缓冲区内容时使用
     */
    virtual void CreateViews(size_t numElements, size_t elementSize) = 0;

protected:

private:
};
