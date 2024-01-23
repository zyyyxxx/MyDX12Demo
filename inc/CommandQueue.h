/**
* Wrapper class for a ID3D12CommandQueue.
 */
 
#pragma once
 
#include <d3d12.h>  // For ID3D12CommandQueue, ID3D12Device2, and ID3D12Fence
#include <wrl.h>    // For Microsoft::WRL::ComPtr
 
#include <atomic>               // For std::atomic_bool
#include <cstdint>              // For uint64_t
#include <condition_variable>   // For std::condition_variable.

#include <ThreadSafeQueue.h>

class CommandList;

class CommandQueue
{
public:
    CommandQueue( D3D12_COMMAND_LIST_TYPE type);
    virtual ~CommandQueue();

    // 获取命令队列中的可用命令列表
    std::shared_ptr<CommandList> GetCommandList();

    // 执行命令队列
    // 返回要等待此命令列表的 fence 值。
    uint64_t ExecuteCommandList(std::shared_ptr<CommandList> commandList);
    uint64_t ExecuteCommandLists(const std::vector<std::shared_ptr<CommandList>>& commandLists);

    uint64_t Signal();
    bool IsFenceComplete(uint64_t fenceValue);
    void WaitForFenceValue(uint64_t fenceValue);
    void Flush();
    
    // 等待另一个命令队列完成
    void Wait( const CommandQueue& other );
    
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> GetD3D12CommandQueue() const;

private:
    // 释放在命令队列上完成处理的任何命令列表
    void ProcessInFlightCommandLists();

    // 跟踪“正在执行中”的命令分配器
    // 第一个成员是等待的栅栏值，第二个是指向“正在执行中”命令列表的共享指针。
    using CommandListEntry = std::tuple<uint64_t, std::shared_ptr<CommandList>>;

    D3D12_COMMAND_LIST_TYPE                         m_CommandListType;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue>      m_d3d12CommandQueue;
    Microsoft::WRL::ComPtr<ID3D12Fence>             m_d3d12Fence;
    std::atomic_uint64_t                            m_FenceValue;

    ThreadSafeQueue<CommandListEntry>               m_InFlightCommandLists;
    ThreadSafeQueue<std::shared_ptr<CommandList>>   m_AvailableCommandLists;

    // 用于处理正在进行的命令列表的线程。
    std::thread                                     m_ProcessInFlightCommandListsThread;
    std::atomic_bool                                m_bProcessInFlightCommandLists;
    std::mutex                                      m_ProcessInFlightCommandListsThreadMutex;
    std::condition_variable                         m_ProcessInFlightCommandListsThreadCV;
};
