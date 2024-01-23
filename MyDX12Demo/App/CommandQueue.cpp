#include <DX12LibPCH.h>

#include <CommandQueue.h>

#include <Application.h>
#include <CommandList.h>
#include <ResourceStateTracker.h>

CommandQueue::CommandQueue(D3D12_COMMAND_LIST_TYPE type)
    : m_FenceValue(0)
    , m_CommandListType(type)
    , m_bProcessInFlightCommandLists(true)
{
    auto device = Application::Get().GetDevice();

    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type = type;
    desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    desc.NodeMask = 0;

    ThrowIfFailed(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_d3d12CommandQueue)));
    ThrowIfFailed(device->CreateFence(m_FenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_d3d12Fence)));

    switch ( type )
    {
        case D3D12_COMMAND_LIST_TYPE_COPY:
            m_d3d12CommandQueue->SetName( L"Copy Command Queue" );
            break;
        case D3D12_COMMAND_LIST_TYPE_COMPUTE:
            m_d3d12CommandQueue->SetName( L"Compute Command Queue" );
            break;
        case D3D12_COMMAND_LIST_TYPE_DIRECT:
            m_d3d12CommandQueue->SetName( L"Direct Command Queue" );
            break;
    }

    // open a thread to process in-flight command lists
    m_ProcessInFlightCommandListsThread = std::thread(&CommandQueue::ProcessInFlightCommandLists, this);
}


CommandQueue::~CommandQueue()
{
    m_bProcessInFlightCommandLists = false;
    m_ProcessInFlightCommandListsThread.join();
}

std::shared_ptr<CommandList> CommandQueue::GetCommandList()
{
    std::shared_ptr<CommandList> commandList;

    // 如果队列上有命令列表
    if ( !m_AvailableCommandLists.Empty() )
    {
        m_AvailableCommandLists.TryPop(commandList);
    }
    else
    {
        // 否则，请创建一个新的命令列表。
        commandList = std::make_shared<CommandList>(m_CommandListType);
    }

    return commandList;
}

uint64_t CommandQueue::ExecuteCommandList(std::shared_ptr<CommandList> commandList)
{
    return ExecuteCommandLists( std::vector<std::shared_ptr<CommandList> >( { commandList } ) );
}

uint64_t CommandQueue::ExecuteCommandLists(const std::vector<std::shared_ptr<CommandList> >& commandLists)
{
    ResourceStateTracker::Lock();

    // 需要放入命令队列的命令列表
    std::vector<std::shared_ptr<CommandList> > toBeQueued;
    toBeQueued.reserve(commandLists.size() * 2);        // 2x，因为每个命令列表都有一个待处理列表

    // 生成 mips 命令列表
    std::vector<std::shared_ptr<CommandList> > generateMipsCommandLists;
    generateMipsCommandLists.reserve( commandLists.size() );

    // 需要执行的命令列表
    std::vector<ID3D12CommandList*> d3d12CommandLists;
    d3d12CommandLists.reserve(commandLists.size() * 2); // 2x，因为每个命令列表都有一个待处理的命令列表

    for (auto commandList : commandLists)
    {
        auto pendingCommandList = GetCommandList();
        bool hasPendingBarriers = commandList->Close( *pendingCommandList );
        pendingCommandList->Close();
        // 如果待执行命令列表上没有待执行屏障，则不需要在命令队列上执行空命令列表。
        if ( hasPendingBarriers )
        {
            d3d12CommandLists.push_back( pendingCommandList->GetGraphicsCommandList().Get() );
        }
        d3d12CommandLists.push_back(commandList->GetGraphicsCommandList().Get());

        toBeQueued.push_back(pendingCommandList);
        toBeQueued.push_back(commandList);

        auto generateMipsCommandList = commandList->GetGenerateMipsCommandList();
        if ( generateMipsCommandList )
        {
            generateMipsCommandLists.push_back( generateMipsCommandList );
        }
    }

    UINT numCommandLists = static_cast<UINT>(d3d12CommandLists.size());
    m_d3d12CommandQueue->ExecuteCommandLists(numCommandLists, d3d12CommandLists.data());
    uint64_t fenceValue = Signal();
    
    ResourceStateTracker::Unlock();

    // 命令列表加入队列以供重用
    for (auto commandList : toBeQueued)
    {
        m_InFlightCommandLists.Push({ fenceValue, commandList });
    }

    // 如果有任何生成 mips 的命令列表，则在初始资源命令列表完成后执行这些命令列表
    if ( generateMipsCommandLists.size() > 0 )
    {
        auto computeQueue = Application::Get().GetCommandQueue( D3D12_COMMAND_LIST_TYPE_COMPUTE );
        computeQueue->Wait( *this );
        computeQueue->ExecuteCommandLists( generateMipsCommandLists );
    }

    return fenceValue;
}

uint64_t CommandQueue::Signal()
{
    uint64_t fenceValue = ++m_FenceValue;
    m_d3d12CommandQueue->Signal(m_d3d12Fence.Get(), fenceValue);
    return fenceValue;
}

bool CommandQueue::IsFenceComplete(uint64_t fenceValue)
{
    return m_d3d12Fence->GetCompletedValue() >= fenceValue;
}

void CommandQueue::WaitForFenceValue(uint64_t fenceValue)
{
    if (!IsFenceComplete(fenceValue))
    {
        auto event = ::CreateEvent( NULL, FALSE, FALSE, NULL );
        assert( event && "Failed to create fence event handle." );
        
        m_d3d12Fence->SetEventOnCompletion(fenceValue, event );
        ::WaitForSingleObject( event, DWORD_MAX);

        ::CloseHandle( event );
    }
}

void CommandQueue::Wait( const CommandQueue& other )
{
    m_d3d12CommandQueue->Wait( other.m_d3d12Fence.Get(), other.m_FenceValue );
}

void CommandQueue::Flush()
{
    std::unique_lock<std::mutex> lock(m_ProcessInFlightCommandListsThreadMutex);
    m_ProcessInFlightCommandListsThreadCV.wait(lock, [this] { return m_InFlightCommandLists.Empty(); });
    
    // 如果命令队列直接使用 CommandQueue::Signal 方法被标记为已执行
    // 则该命令队列的栅栏值可能高于任何已执行的命令列表的栅栏值
    WaitForFenceValue(m_FenceValue);
}

Microsoft::WRL::ComPtr<ID3D12CommandQueue> CommandQueue::GetD3D12CommandQueue() const
{
    return m_d3d12CommandQueue;
}

void CommandQueue::ProcessInFlightCommandLists()
{
    std::unique_lock<std::mutex> lock(m_ProcessInFlightCommandListsThreadMutex, std::defer_lock );

    while (m_bProcessInFlightCommandLists)
    {
        CommandListEntry commandListEntry;
        
        lock.lock();
        while (m_InFlightCommandLists.TryPop(commandListEntry))
        {   
            auto fenceValue = std::get<0>(commandListEntry);
            auto commandList = std::get<1>(commandListEntry);

            WaitForFenceValue( fenceValue );
            
            commandList->Reset();

            m_AvailableCommandLists.Push(commandList);
        }
        lock.unlock();
        m_ProcessInFlightCommandListsThreadCV.notify_one();

        std::this_thread::yield();
    }
}