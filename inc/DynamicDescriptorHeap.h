#pragma once

#include "d3dx12.h"
 
#include <wrl.h>
 
#include <cstdint>
#include <memory>
#include <queue>
 
class CommandList;
class RootSignature;

/**
 *  @brief DynamicDescriptorHeap GPU 可见描述符堆
 *  允许暂存需要在执行 Draw 或 Dispatch 命令之前上传的 CPU 可见描述符 至 GPU 可见描述符堆
 *  随后由 CommandList 与根签名中对应的 GPU 资源绑定。
 */
class DynamicDescriptorHeap
{
public:
    DynamicDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE heapType,
        uint32_t numDescriptorsPerHeap = 1024);
 
    virtual ~DynamicDescriptorHeap();

    /**
     * 暂存一系列连续的 CPU 可见描述符。
     * 在调用 CommitStagedDescriptors 函数之前，不会将描述符复制到 GPU 可见描述符堆。
     */
    void StageDescriptors(uint32_t rootParameterIndex, uint32_t offset, uint32_t numDescriptors, const D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptors);
    
    /**
     * 将所有暂存描述符复制到 GPU 可见描述符堆，并将描述符堆和描述符表绑定到命令列表
     * 传入的函数对象用于在命令列表中设置 GPU 可见描述符:
     *   * 绘制前   : ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable
     *   * 分发前   : ID3D12GraphicsCommandList::SetComputeRootDescriptorTable
     * 
     * 由于DynamicDescriptorHeap无法知道将使用哪个函数，因此必须将其作为参数传递给函数。
     */
    void CommitStagedDescriptors( CommandList& commandList, std::function<void(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_DESCRIPTOR_HANDLE)> setFunc );
    void CommitStagedDescriptorsForDraw(CommandList& commandList);
    void CommitStagedDescriptorsForDispatch(CommandList& commandList);

    
    /**
     * 将单个CPU可见描述符复制到GPU可见描述符堆。
     * 这对于ID3D12GraphicsCommandList::ClearUnorderedAccessViewFloat和
     * ID3D12GraphicsCommandList::ClearUnorderedAccessViewUint等方法非常有用，
     * 这些方法需要UAV资源的CPU和GPU可见描述符。
     * 
     * @param commandList 在需要在命令列表上更新GPU可见描述符堆时，需要传递命令列表。
     * @param cpuDescriptor 要复制到GPU可见描述符堆的CPU描述符。
     * 
     * @return GPU可见描述符。
     */
    D3D12_GPU_DESCRIPTOR_HANDLE CopyDescriptor( CommandList& comandList, D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor);

    /**
     * 解析根签名以确定哪些根参数包含描述符表，并确定每个表所需的描述符数量。
     * 通知命令列表上当前绑定的根签名的任何更改。此方法更新描述符缓存中描述符的布局，以匹配根签名中的描述符布局
     */
    void ParseRootSignature(const RootSignature& rootSignature);

    /**
     * 重置已使用的描述符。只有在被命令列表引用的任何描述符在命令队列上执行完成时才应执行此操作。
     */
    void Reset();

protected:

private:
    // 请求描述符堆（如果存在）。
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> RequestDescriptorHeap();
    // 创建新描述符堆。
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap();

    // 计算需要复制到 GPU 可见描述符堆的陈旧描述符的数量。
    uint32_t ComputeStaleDescriptorCount() const;

    /**
     * 每个根签名的最大描述符表数量。
     * 使用32位掩码来跟踪根参数索引中的描述符表。
     */
    static constexpr uint32_t MaxDescriptorTables = 32;

    /**
     * 表示根签名中描述符表条目的结构。
     */
    struct DescriptorTableCache
    {
        DescriptorTableCache()
            : NumDescriptors(0)
            , BaseDescriptor(nullptr)
        {}

        // 重置表缓冲
        void Reset()
        {
            NumDescriptors = 0;
            BaseDescriptor = nullptr;
        }

        // 描述符表中的描述符数量.
        uint32_t NumDescriptors;
        // 描述符在描述符句柄缓存中的指针.
        D3D12_CPU_DESCRIPTOR_HANDLE* BaseDescriptor;
    };

    // 描述可以使用此动态描述符堆进行储存的描述符的类型。
    // 有效值为：
    //   * D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
    //   * D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
    // 此参数还确定要创建的GPU可见描述符堆的类型。
    D3D12_DESCRIPTOR_HEAP_TYPE m_DescriptorHeapType;

    // 在新的GPU可见描述符堆中分配的描述符数量。
    uint32_t m_NumDescriptorsPerHeap;

    // 描述符的增量大小。
    uint32_t m_DescriptorHandleIncrementSize;

    // 描述符句柄缓存。
    std::unique_ptr<D3D12_CPU_DESCRIPTOR_HANDLE[]> m_DescriptorHandleCache;

    // 每个描述符表的描述符句柄缓存。
    DescriptorTableCache m_DescriptorTableCache[MaxDescriptorTables];

    // 位掩码中的每个位表示根签名中包含描述符表的索引。
    uint32_t m_DescriptorTableBitMask;
    
    // 在位掩码中每个位的集合表示自上次提交描述符以来已更改的描述符表。
    uint32_t m_StaleDescriptorTableBitMask;

    using DescriptorHeapPool = std::queue<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>>;

    DescriptorHeapPool m_DescriptorHeapPool;
    DescriptorHeapPool m_AvailableDescriptorHeaps;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_CurrentDescriptorHeap; // 绑定到命令列表的当前描述符堆
    CD3DX12_GPU_DESCRIPTOR_HANDLE m_CurrentGPUDescriptorHandle;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_CurrentCPUDescriptorHandle;

    uint32_t m_NumFreeHandles;
    
};