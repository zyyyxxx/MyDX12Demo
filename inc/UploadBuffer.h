#pragma once
 
#include <Defines.h>
 
#include <wrl.h>
#include <d3d12.h>
 
#include <memory>
#include <deque>

/**
 *  线性分配器 上传资源至GPU （上传堆Page池）由 CommandList 创建
 *  将动态常量、顶点和索引缓冲区数据（或与此相关的任何缓冲区数据）上传到 GPU 
 */
class UploadBuffer
{
public:
    // 用于上传数据至GPU
    struct Allocation
    {
        void* CPU;
        D3D12_GPU_VIRTUAL_ADDRESS GPU;
    };
    
    /**
    * @param pageSize 分配页面大小
    */
    explicit UploadBuffer(size_t pageSize = _2MB);

    virtual ~UploadBuffer();

    /**
    * 返回分配器的单个页面的大小
    */
    size_t GetPageSize() const { return m_PageSize;  }

    /**
     * 使用指定的分配来分配一块内存
     * 分配必须小于分配器的单个页面大小
     * 使用memcpy或类似方法拷贝数据到CPU指针Allocation结构中返回的指针中。
     */
    Allocation Allocate(size_t sizeInBytes, size_t alignment);

    /**
     * 重置所有分配，以便内存可以重新用于下一帧， 仅能适用于命令队列中的命令列表执行完毕后
     */
    void Reset();
    
private:
    
    struct Page
    {
        Page(size_t sizeInBytes);
        ~Page();
        
        //检查是否有足够页面空间满足分配需求
        bool HasSpace(size_t sizeInBytes, size_t alignment ) const;

        // 分配页面内存
        // 当分配大小大于页面大小或分配大小超过页面可用大小 抛出 std::bad_alloc异常
        // 不线程安全！
        Allocation Allocate(size_t sizeInBytes, size_t alignment);
        
        // 重置页面以重用
        void Reset();
    private:
        Microsoft::WRL::ComPtr<ID3D12Resource> m_d3d12Resource;

        // 基地址指针
        void* m_CPUPtr;
        D3D12_GPU_VIRTUAL_ADDRESS m_GPUPtr;

        // 分配页面大小
        size_t m_PageSize;
        // 当前分配偏移
        size_t m_Offset;
    };

    // 内存页面池
    using PagePool = std::deque< std::shared_ptr<Page>>;

    // 获取一个可用的页面 或者当没有可用页面时创建一个新的页面
    std::shared_ptr<Page> RequestPage();

    PagePool m_PagePool;
    PagePool m_AvailablePages;

    std::shared_ptr<Page> m_CurrentPage;

    // 各页面的大小.
    size_t m_PageSize;
    
};
