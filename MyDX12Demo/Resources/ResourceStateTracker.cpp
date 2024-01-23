#include <DX12LibPCH.h>
 
#include <ResourceStateTracker.h>
 
#include <CommandList.h>
#include <Resource.h>

// 静态变量初始化
std::mutex ResourceStateTracker::ms_GlobalMutex;
bool ResourceStateTracker::ms_IsLocked = false;
ResourceStateTracker::ResourceStateMap ResourceStateTracker::ms_GlobalResourceState;

ResourceStateTracker::ResourceStateTracker()
{}

ResourceStateTracker::~ResourceStateTracker()
{}

void ResourceStateTracker::ResourceBarrier(const D3D12_RESOURCE_BARRIER& barrier)
{
    if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
    {
        const D3D12_RESOURCE_TRANSITION_BARRIER& transitionBarrier = barrier.Transition;

        // 首先检查给定资源是否已有已知的“最终”状态。
        // 如果有，表示该资源已在命令列表上被使用，并且已在命令列表执行期间具有已知状态。
        const auto iter = m_FinalResourceState.find(transitionBarrier.pResource);
        if (iter != m_FinalResourceState.end())
        {
            auto& resourceState = iter->second;
            // 转换屏障正在转换所有子资源 并且存在处于不同状态的子资源
            if (transitionBarrier.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES &&
                !resourceState.SubresourceState.empty())
            {
                // 如果子资源与StateAfter不同，则首先转换所有子资源。
                for (auto subresourceState : resourceState.SubresourceState)
                {
                    if (transitionBarrier.StateAfter != subresourceState.second)
                    {
                        D3D12_RESOURCE_BARRIER newBarrier = barrier;
                        newBarrier.Transition.Subresource = subresourceState.first;
                        newBarrier.Transition.StateBefore = subresourceState.second;
                        m_ResourceBarriers.push_back(newBarrier);
                    }
                }
            }
            else 
            {//转换单个子资源，或者所有子资源都处于相同状态
                auto finalState = resourceState.GetSubresourceState(transitionBarrier.Subresource);
                if (transitionBarrier.StateAfter != finalState)
                {
                    // 推送一个新的转换屏障，其中包含正确的前状态。
                    D3D12_RESOURCE_BARRIER newBarrier = barrier;
                    newBarrier.Transition.StateBefore = finalState;
                    m_ResourceBarriers.push_back(newBarrier);
                }
            }
        }
        else // 在这种情况下，资源在命令列表上首次被使用。
        {
            // 添加待处理的屏障。这些待处理的屏障将在命令列表在命令队列上执行之前解决。
            m_PendingResourceBarriers.push_back(barrier);
        }

        // 推送已知的最终状态（可能替代之前已知的子资源状态）。无论该资源之前是否在命令列表中出现过，其最终状态都会添加
        m_FinalResourceState[transitionBarrier.pResource].SetSubresourceState(
            transitionBarrier.Subresource, transitionBarrier.StateAfter);
    }
    else
    {
        // 只需将非转换屏障推送到资源屏障数组。D3D12_RESOURCE_BARRIER_TYPE_UAV / D3D12_RESOURCE_BARRIER_TYPE_ALIASING 不需要任何特殊处理
        m_ResourceBarriers.push_back(barrier);
    }
}


#pragma region Different Resource Barrier Helpers

void ResourceStateTracker::TransitionResource( ID3D12Resource* resource, D3D12_RESOURCE_STATES stateAfter, UINT subResource )
{
    if ( resource )
    {
        ResourceBarrier( CD3DX12_RESOURCE_BARRIER::Transition( resource, D3D12_RESOURCE_STATE_COMMON, stateAfter, subResource ) );
    }
}

void ResourceStateTracker::TransitionResource( const Resource& resource, D3D12_RESOURCE_STATES stateAfter, UINT subResource )
{
    TransitionResource( resource.GetD3D12Resource().Get(), stateAfter, subResource );
}


void ResourceStateTracker::UAVBarrier(const Resource* resource )
{
    ID3D12Resource* pResource = resource != nullptr ? resource->GetD3D12Resource().Get() : nullptr;

    ResourceBarrier(CD3DX12_RESOURCE_BARRIER::UAV(pResource));
}

void ResourceStateTracker::AliasBarrier(const Resource* resourceBefore, const Resource* resourceAfter)
{
    ID3D12Resource* pResourceBefore = resourceBefore != nullptr ? resourceBefore->GetD3D12Resource().Get() : nullptr;
    ID3D12Resource* pResourceAfter = resourceAfter != nullptr ? resourceAfter->GetD3D12Resource().Get() : nullptr;

    ResourceBarrier(CD3DX12_RESOURCE_BARRIER::Aliasing(pResourceBefore, pResourceAfter));
}

#pragma endregion

void ResourceStateTracker::FlushResourceBarriers(CommandList& commandList)
{
    UINT numBarriers = static_cast<UINT>(m_ResourceBarriers.size());
    if (numBarriers > 0 )
    {
        auto d3d12CommandList = commandList.GetGraphicsCommandList();
        d3d12CommandList->ResourceBarrier(numBarriers, m_ResourceBarriers.data());
        m_ResourceBarriers.clear();
    }
}


uint32_t ResourceStateTracker::FlushPendingResourceBarriers(CommandList& commandList)
{
    assert(ms_IsLocked);

    // 通过检查（子）资源的全局状态来解决待处理的资源屏障。如果待处理状态和全局状态不匹配，则添加屏障。
    ResourceBarriers resourceBarriers;
    // 预留足够的空间（最坏情况下，所有待处理的屏障）。
    resourceBarriers.reserve(m_PendingResourceBarriers.size());

    for (auto pendingBarrier : m_PendingResourceBarriers)
    {
        if (pendingBarrier.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)  // 只有转换屏障应该是待处理的...
        {
            auto pendingTransition = pendingBarrier.Transition;
            
            const auto& iter = ms_GlobalResourceState.find(pendingTransition.pResource);
            if (iter != ms_GlobalResourceState.end())
            {
                // 如果所有子资源都在进行转换，并且资源有多个子资源处于不同状态...
                auto& resourceState = iter->second;
                if ( pendingTransition.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES &&
                     !resourceState.SubresourceState.empty() )
                {
                    // 转换所有子资源
                    for ( auto subresourceState : resourceState.SubresourceState )
                    {
                        if ( pendingTransition.StateAfter != subresourceState.second )
                        {
                            D3D12_RESOURCE_BARRIER newBarrier = pendingBarrier;
                            newBarrier.Transition.Subresource = subresourceState.first;
                            newBarrier.Transition.StateBefore = subresourceState.second;
                            resourceBarriers.push_back( newBarrier );
                        }
                    }
                }
                else
                {
                    // 没有需要进行转换的（子）资源。只需添加单个转换屏障（如果需要的话）。
                    auto globalState = ( iter->second ).GetSubresourceState( pendingTransition.Subresource );
                    if ( pendingTransition.StateAfter != globalState )
                    {
                        // 根据资源当前的全局状态修正前状态。
                        pendingBarrier.Transition.StateBefore = globalState;
                        resourceBarriers.push_back( pendingBarrier );
                    }
                }
            }
        }
    }

    UINT numBarriers = static_cast<UINT>(resourceBarriers.size());
    if (numBarriers > 0 )
    {
        auto d3d12CommandList = commandList.GetGraphicsCommandList();
        d3d12CommandList->ResourceBarrier(numBarriers, resourceBarriers.data());
    }

    m_PendingResourceBarriers.clear();

    return numBarriers;
}

void ResourceStateTracker::CommitFinalResourceStates()
{
    assert(ms_IsLocked);

    // 将最终资源状态提交到全局资源状态数组 （map）。
    for (const auto& resourceState : m_FinalResourceState)
    {
        ms_GlobalResourceState[resourceState.first] = resourceState.second;
    }

    m_FinalResourceState.clear();
}

void ResourceStateTracker::Reset()
{
    // 重置 pending, current, final 资源状态.
    m_PendingResourceBarriers.clear();
    m_ResourceBarriers.clear();
    m_FinalResourceState.clear();
}

void ResourceStateTracker::Lock()
{
    ms_GlobalMutex.lock();
    ms_IsLocked = true;
}

void ResourceStateTracker::Unlock()
{
    ms_GlobalMutex.unlock();
    ms_IsLocked = false;
}

void ResourceStateTracker::AddGlobalResourceState(ID3D12Resource* resource, D3D12_RESOURCE_STATES state)
{
    if ( resource != nullptr )
    {
        std::lock_guard<std::mutex> lock(ms_GlobalMutex);
        ms_GlobalResourceState[resource].SetSubresourceState(D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, state);
    }
}

void ResourceStateTracker::RemoveGlobalResourceState(ID3D12Resource* resource)
{
    if ( resource != nullptr )
    {
        std::lock_guard<std::mutex> lock(ms_GlobalMutex);
        ms_GlobalResourceState.erase(resource);
    }
}

