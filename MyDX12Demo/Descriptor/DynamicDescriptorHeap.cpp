#include <DX12LibPCH.h>
 
#include <DynamicDescriptorHeap.h>
 
#include <Application.h>
#include <CommandList.h>
#include <RootSignature.h>

DynamicDescriptorHeap::DynamicDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, uint32_t numDescriptorsPerHeap)
    : m_DescriptorHeapType(heapType)
    , m_NumDescriptorsPerHeap(numDescriptorsPerHeap)
    , m_DescriptorTableBitMask(0)
    , m_StaleDescriptorTableBitMask(0)
    , m_CurrentCPUDescriptorHandle(D3D12_DEFAULT)
    , m_CurrentGPUDescriptorHandle(D3D12_DEFAULT)
    , m_NumFreeHandles(0)
{
    m_DescriptorHandleIncrementSize = Application::Get().GetDescriptorHandleIncrementSize(heapType);
 
    // 为暂存 CPU 可见描述符分配空间
    m_DescriptorHandleCache = std::make_unique<D3D12_CPU_DESCRIPTOR_HANDLE[]>(m_NumDescriptorsPerHeap);
}

DynamicDescriptorHeap::~DynamicDescriptorHeap()
{}


void DynamicDescriptorHeap::StageDescriptors(uint32_t rootParameterIndex, uint32_t offset, uint32_t numDescriptors, const D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptor)
{
    // 不能暂存超过每个堆的最大描述符数
    // 不能暂存超过 MaxDescriptorTables 的根参数
    if (numDescriptors > m_NumDescriptorsPerHeap || rootParameterIndex >= MaxDescriptorTables )
    {
        throw std::bad_alloc();
    }

    DescriptorTableCache& descriptorTableCache = m_DescriptorTableCache[rootParameterIndex];

    // 检查要复制的描述符数是否不超过描述符表中预期的描述符数
    if ( (offset + numDescriptors) > descriptorTableCache.NumDescriptors)
    {
        throw std::length_error("Number of descriptors exceeds the number of descriptors in the descriptor table.");
    }

    D3D12_CPU_DESCRIPTOR_HANDLE* dstDescriptor = (descriptorTableCache.BaseDescriptor + offset);
    for (uint32_t i = 0; i < numDescriptors; ++i)
    {
        dstDescriptor[i] = CD3DX12_CPU_DESCRIPTOR_HANDLE(srcDescriptor, i, m_DescriptorHandleIncrementSize);
    }

    // 设置根参数索引位，以确保该索引处的描述符表绑定到命令列表。
    m_StaleDescriptorTableBitMask |= (1 << rootParameterIndex);
}


void DynamicDescriptorHeap::CommitStagedDescriptors(CommandList& commandList,
    std::function<void(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_DESCRIPTOR_HANDLE)> setFunc)
{
    // 计算需要复制的描述符数
    uint32_t numDescriptorsToCommit = ComputeStaleDescriptorCount();

    if ( numDescriptorsToCommit > 0 )
    {
        auto device = Application::Get().GetDevice();
        auto d3d12GraphicsCommandList = commandList.GetGraphicsCommandList().Get();
        assert(d3d12GraphicsCommandList != nullptr);
        
        //如果 m_CurrentDescriptorHeap 为空（这是首次创建或重置后的m_CurrentDescriptorHeap情况）或者没有足够的空闲句柄来提交到描述符堆，则检索新堆
        if ( !m_CurrentDescriptorHeap || m_NumFreeHandles < numDescriptorsToCommit )
        {
            m_CurrentDescriptorHeap = RequestDescriptorHeap();
            m_CurrentCPUDescriptorHandle = m_CurrentDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
            m_CurrentGPUDescriptorHandle = m_CurrentDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
            m_NumFreeHandles = m_NumDescriptorsPerHeap;

            commandList.SetDescriptorHeap(m_DescriptorHeapType, m_CurrentDescriptorHeap.Get()); //确保命令列表具有新的描述符堆绑定

            //在更新命令列表上的描述符堆时，必须将所有描述符表（不仅仅是过时的描述符表）重新复制到新的描述符堆中。
            m_StaleDescriptorTableBitMask = m_DescriptorTableBitMask;
        }

        DWORD rootIndex;
        // 从 LSB 扫描到 MSB 以查找在 m_StaleDescriptorTableBitMask 中设置的位
        while ( _BitScanForward( &rootIndex, m_StaleDescriptorTableBitMask ) )
        {
            UINT numSrcDescriptors = m_DescriptorTableCache[rootIndex].NumDescriptors;
            D3D12_CPU_DESCRIPTOR_HANDLE* pSrcDescriptorHandles = m_DescriptorTableCache[rootIndex].BaseDescriptor;

            D3D12_CPU_DESCRIPTOR_HANDLE pDestDescriptorRangeStarts[] =
            {
                m_CurrentCPUDescriptorHandle
            };
            UINT pDestDescriptorRangeSizes[] =
            {
                numSrcDescriptors
            };

            // 将暂存的 CPU 可见描述符复制到 GPU 可见描述符堆。
            device->CopyDescriptors(1, pDestDescriptorRangeStarts, pDestDescriptorRangeSizes,
                numSrcDescriptors, pSrcDescriptorHandles, nullptr, m_DescriptorHeapType);

            // 使用传入的函数在命令列表中设置描述符。
            setFunc(d3d12GraphicsCommandList, rootIndex, m_CurrentGPUDescriptorHandle);

            // 偏移当前 CPU 和 GPU 描述符句柄。
            m_CurrentCPUDescriptorHandle.Offset(numSrcDescriptors, m_DescriptorHandleIncrementSize);
            m_CurrentGPUDescriptorHandle.Offset(numSrcDescriptors, m_DescriptorHandleIncrementSize);
            m_NumFreeHandles -= numSrcDescriptors;

            // 翻转过时的位，这样就不会再次复制描述符表，除非使用新的描述符对其进行更新。
            m_StaleDescriptorTableBitMask ^= (1 << rootIndex);
        }
    }
}


void DynamicDescriptorHeap::CommitStagedDescriptorsForDraw(CommandList& commandList)
{
    CommitStagedDescriptors(commandList, &ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable);
}

void DynamicDescriptorHeap::CommitStagedDescriptorsForDispatch(CommandList& commandList)
{
    CommitStagedDescriptors(commandList, &ID3D12GraphicsCommandList::SetComputeRootDescriptorTable);
}

D3D12_GPU_DESCRIPTOR_HANDLE DynamicDescriptorHeap::CopyDescriptor(CommandList& comandList,
    D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor)
{
    if (!m_CurrentDescriptorHeap || m_NumFreeHandles < 1)
    {
        m_CurrentDescriptorHeap = RequestDescriptorHeap();
        m_CurrentCPUDescriptorHandle = m_CurrentDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        m_CurrentGPUDescriptorHandle = m_CurrentDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
        m_NumFreeHandles = m_NumDescriptorsPerHeap;

        comandList.SetDescriptorHeap(m_DescriptorHeapType, m_CurrentDescriptorHeap.Get());

        // 在更新命令列表上的描述符堆时，必须将所有描述符表（不仅仅是陈旧的描述符表）重新复制到新的描述符堆中，
        // 而不仅仅是过时的描述符表。
        m_StaleDescriptorTableBitMask = m_DescriptorTableBitMask;
    }

    auto device = Application::Get().GetDevice();

    D3D12_GPU_DESCRIPTOR_HANDLE hGPU = m_CurrentGPUDescriptorHandle;
    device->CopyDescriptorsSimple(1, m_CurrentCPUDescriptorHandle, cpuDescriptor, m_DescriptorHeapType);

    m_CurrentCPUDescriptorHandle.Offset(1, m_DescriptorHandleIncrementSize);
    m_CurrentGPUDescriptorHandle.Offset(1, m_DescriptorHandleIncrementSize);
    m_NumFreeHandles -= 1;

    return hGPU;    
}

void DynamicDescriptorHeap::ParseRootSignature(const RootSignature& rootSignature)
{
    //如果根签名发生更改，则必须将所有描述符（重新）绑定到命令列表。
    //不应将任何描述符复制到 GPU 可见描述符堆，直到新描述符暂存到DynamicDescriptorHea
    m_StaleDescriptorTableBitMask = 0;

    const auto& rootSignatureDesc = rootSignature.GetRootSignatureDesc();

    // 获取一个位掩码，该掩码表示与此动态描述符堆的描述符堆类型匹配的根参数索引。
    m_DescriptorTableBitMask = rootSignature.GetDescriptorTableBitMask(m_DescriptorHeapType);
    uint32_t descriptorTableBitMask = m_DescriptorTableBitMask;

    uint32_t currentOffset = 0;
    DWORD rootIndex;
    while (_BitScanForward(&rootIndex, descriptorTableBitMask) && rootIndex < rootSignatureDesc.NumParameters) //从最低有效位 (LSB) 到最高有效位 (MSB) 扫描位域
    {
        uint32_t numDescriptors = rootSignature.GetNumDescriptors(rootIndex);

        DescriptorTableCache& descriptorTableCache = m_DescriptorTableCache[rootIndex];
        descriptorTableCache.NumDescriptors = numDescriptors;
        descriptorTableCache.BaseDescriptor = m_DescriptorHandleCache.get() + currentOffset;

        currentOffset += numDescriptors;

        // 反转 descriptor table bit 因此不会再次扫描当前索引。
        descriptorTableBitMask ^= (1 << rootIndex);
    }

    //根签名的描述符总数不超过可以复制到GPU可见描述符堆的描述符的最大数量
    assert(currentOffset <= m_NumDescriptorsPerHeap && "The root signature requires more than the maximum number of descriptors per descriptor heap. Consider increasing the maximum number of descriptors per descriptor heap.");
    
    
}

void DynamicDescriptorHeap::Reset()
{
    m_AvailableDescriptorHeaps = m_DescriptorHeapPool;
    m_CurrentDescriptorHeap.Reset();
    m_CurrentCPUDescriptorHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(D3D12_DEFAULT);
    m_CurrentGPUDescriptorHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(D3D12_DEFAULT);
    m_NumFreeHandles = 0;
    m_DescriptorTableBitMask = 0;
    m_StaleDescriptorTableBitMask = 0;
 
    // Reset the table cache
    for (int i = 0; i < MaxDescriptorTables; ++i)
    {
        m_DescriptorTableCache[i].Reset();
    }
}
Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> DynamicDescriptorHeap::RequestDescriptorHeap()
{
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap;
    if (!m_AvailableDescriptorHeaps.empty())
    {
        descriptorHeap = m_AvailableDescriptorHeaps.front();
        m_AvailableDescriptorHeaps.pop();
    }
    else
    {
        descriptorHeap = CreateDescriptorHeap();
        m_DescriptorHeapPool.push(descriptorHeap);
    }
 
    return descriptorHeap;
}

Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> DynamicDescriptorHeap::CreateDescriptorHeap()
{
    auto device = Application::Get().GetDevice();
 
    D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc = {};
    descriptorHeapDesc.Type = m_DescriptorHeapType;
    descriptorHeapDesc.NumDescriptors = m_NumDescriptorsPerHeap;
    descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
 
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap;
    ThrowIfFailed(device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap)));
 
    return descriptorHeap;
}

uint32_t DynamicDescriptorHeap::ComputeStaleDescriptorCount() const
{
    uint32_t numStaleDescriptors = 0;
    DWORD i;
    DWORD staleDescriptorsBitMask = m_StaleDescriptorTableBitMask;
 
    while ( _BitScanForward( &i, staleDescriptorsBitMask ) )
    {
        numStaleDescriptors += m_DescriptorTableCache[i].NumDescriptors;
        staleDescriptorsBitMask ^= ( 1 << i );
    }
 
    return numStaleDescriptors;
}
