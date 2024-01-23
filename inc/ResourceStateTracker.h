#pragma once
#include <d3d12.h>

#include <mutex>
#include <map>
#include <unordered_map>
#include <vector>

class CommandList;
class Resource;

/**
 *  @brief 追踪CommandList 资源或子资源 状态
 */
class ResourceStateTracker
{
public:
     ResourceStateTracker();
     virtual ~ResourceStateTracker();

     /**
      * 将资源屏障推入到 ResourceStateTracker
      *
      * @param barrier 推入到 ResourceStateTracker 的资源屏障
      */
     void ResourceBarrier(const D3D12_RESOURCE_BARRIER& barrier);

     /**
      * 推送一个资源状态转换屏障到ResourceStateTracker。
      * 
      * @param resource 要进行状态转换的资源。
      * @param stateAfter 要将资源转换到的状态。
      * @param subResource 要进行转换的子资源。默认情况下，这是 D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES，
      * 表示所有子资源都应该转换到相同的状态。
      */
     void TransitionResource(ID3D12Resource* resource, D3D12_RESOURCE_STATES stateAfter, UINT subResource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
     /**
      * 推送一个资源状态转换屏障到ResourceStateTracker。
      * 
      * @param resource 要进行状态转换的资源。
      * @param stateAfter 要将资源转换到的状态。
      * @param subResource 要进行转换的子资源。默认情况下，这是 D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES，
      * 表示所有子资源都应该转换到相同的状态。
      */
     void TransitionResource(const Resource& resource, D3D12_RESOURCE_STATES stateAfter, UINT subResource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

    
     /**
      * 为给定资源推送UAV资源屏障。
      *
      * @param resource 要为其添加 UAV 屏障的资源。可以为NULL，表示任何 UAV 访问可能需要该屏障
      */
     void UAVBarrier(const Resource *resource = nullptr);

     /**
      * 推送给定资源的别名屏障。
      *
      * @param beforeResource 当前占用堆中空间的资源。
      * @param afterResource 将要占用堆中空间的资源。
      *
      * beforeResource 或 afterResource 参数可以为NULL，表示任何已放置或已保留的资源都可能引起别名。
      */
     void AliasBarrier(const Resource *resourceBefore = nullptr, const Resource *resourceAfter = nullptr);

     /**
      * 将任何待处理的资源屏障刷新到命令列表。（需要转换状态）
      *
      * @return 刷新到命令列表的资源屏障数量。
      */
     uint32_t FlushPendingResourceBarriers(CommandList &commandList);

     /**
      * 刷新已经推送到资源状态跟踪器的任何（非待处理的）资源屏障。
      */
     void FlushResourceBarriers(CommandList& commandList);


     /**
      * 将最终资源状态提交到全局资源状态map
      * 当命令列表关闭时，必须调用此函数。
      */
     void CommitFinalResourceStates();

     /**
      * 重置状态跟踪。在命令列表被重置时必须执行此操作。
      */
     void Reset();

     /**
      * 在刷新待处理资源屏障和将最终资源状态提交到全局资源状态之前，必须锁定全局状态。
      * 这确保了命令列表执行之间全局资源状态的一致性。
      */
     static void Lock();

     /**
      * 在最终状态已提交到全局资源状态数组后，解锁全局资源状态。
      */
     static void Unlock();

     /**
      * 将具有给定状态的资源添加到全局资源状态数组（映射）中。
      * 这应该在首次创建资源时执行。
      */
     static void AddGlobalResourceState(ID3D12Resource *resource, D3D12_RESOURCE_STATES state);

     /**
      * 从全局资源状态数组（映射）中移除一个资源。
      * 这应该仅在资源被销毁时执行。
      */
     static void RemoveGlobalResourceState(ID3D12Resource *resource);

private:
     // 资源屏障的数组。
     using ResourceBarriers = std::vector<D3D12_RESOURCE_BARRIER>;

     // 待处理的资源转换在命令列表在命令队列上执行之前被提交。
     // 这保证了资源在命令列表开始时处于预期的状态。
     ResourceBarriers m_PendingResourceBarriers;

     // 需要提交到命令列表的资源屏障。
     ResourceBarriers m_ResourceBarriers;

     // 跟踪特定资源及其所有子资源的状态。
     struct ResourceState
     {
          // 将资源中的所有子资源初始化为给定的状态。
          explicit ResourceState(D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON) : State(state) {}

          // 将子资源设置为特定状态。
          void SetSubresourceState(UINT subresource, D3D12_RESOURCE_STATES state)
          {
               if (subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
               {
                    State = state;
                    SubresourceState.clear();
               }
               else
               {
                    SubresourceState[subresource] = state;
               }
          }

          // 获取资源中（子）资源的状态。
          // 如果在SubresourceState数组（映射）中找不到指定的子资源，
          // 则返回资源的状态（D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES）。
          D3D12_RESOURCE_STATES GetSubresourceState(UINT subresource) const
          {
               D3D12_RESOURCE_STATES state = State;
               const auto iter = SubresourceState.find(subresource);
               if (iter != SubresourceState.end())
               {
                    state = iter->second;
               }
               return state;
          }

          // 如果SubresourceState数组（映射）为空，则State变量定义所有子资源的状态。
          D3D12_RESOURCE_STATES State;
          std::map<UINT, D3D12_RESOURCE_STATES> SubresourceState;
     };

     using ResourceStateMap = std::unordered_map<ID3D12Resource*, ResourceState>;
     // 命令列表中资源的最终（最后已知的状态）。
     // 在命令列表关闭但在它在命令队列上执行之前，最终资源状态会被提交到全局资源状态。
     ResourceStateMap m_FinalResourceState;
    
    // 全局资源状态map 存储了资源在命令列表执行之间的状态。
    static ResourceStateMap ms_GlobalResourceState;

    // 互斥锁保护对GlobalResourceState映射的共享访问。
    static std::mutex ms_GlobalMutex;
    static bool ms_IsLocked;
    
};
