
#pragma once

#include "DescriptorAllocation.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

#include <memory>
#include <string>

class CommandQueue;
class DescriptorAllocator;
class Game;
class Window;

/**
* Application 类用于为应用程序创建窗口。
*/
class Application
{
public:

    /**
    *创建应用单例，使用应用程序实例句柄。
    */
    static void Create(HINSTANCE hInst);

    /**
    *销毁应用程序实例和由此应用程序实例创建的所有窗口。
    */
    static void Destroy();
    /**
    * 获取单例。
    */
    static Application& Get();

    /**
     * 检查是否支持VSync-off。
     */
    bool IsTearingSupported() const;

    /**
     * 检查给定格式是否支持请求的多重采样质量。
     */
    DXGI_SAMPLE_DESC GetMultisampleQualityLevels( DXGI_FORMAT format, UINT numSamples, D3D12_MULTISAMPLE_QUALITY_LEVEL_FLAGS flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE ) const;

    
    /**
    * 创建新窗口实例。
    * @param windowName 窗口名称。 此名称将出现在窗口标题栏中。 此名称应唯一。
    * @param clientWidth 宽度（以像素为单位） 
    * @param clientHeight 高度（以像素为单位）
    * @param vSync  是否使用VSync
    * @param windowed If true, the window will be created in windowed mode. If false, the window will be created full-screen.
    * @returns The created window instance. If an error occurred while creating the window an invalid
    * window instance is returned. If a window with the given name already exists, that window will be returned.
    */
    std::shared_ptr<Window> CreateRenderWindow(const std::wstring& windowName, int clientWidth, int clientHeight, bool vSync = true );

    /**
    * 根据窗口名字销毁窗口
    */
    void DestroyWindow(const std::wstring& windowName);
    /**
    * 根据窗口引用销毁窗口
    */
    void DestroyWindow(std::shared_ptr<Window> window);

    /**
    * 通过窗口名称查找窗口。
    */
    std::shared_ptr<Window> GetWindowByName(const std::wstring& windowName);

    /**
    * 运行应用程序循环和消息
    * @return 发生错误时的错误代码。
    */
    int Run(std::shared_ptr<Game> pGame);

    /**
    * 请求退出应用程序并关闭所有窗口。
    * @param exitCode 返回到调用过程的错误代码。
    */
    void Quit(int exitCode = 0);

    /**
     * 获取 D3DX12 设备
     */
    Microsoft::WRL::ComPtr<ID3D12Device2> GetDevice() const;
    
    /**
     * 获取命令队列. Valid types are:
     * - D3D12_COMMAND_LIST_TYPE_DIRECT : Can be used for draw, dispatch, or copy commands.
     * - D3D12_COMMAND_LIST_TYPE_COMPUTE: Can be used for dispatch or copy commands.
     * - D3D12_COMMAND_LIST_TYPE_COPY   : Can be used for copy commands.
     */
    std::shared_ptr<CommandQueue> GetCommandQueue(D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT) const;

    // Flush all command queues.
    void Flush();

    /**
      * 分配多个 CPU 可见描述符。
      */
    DescriptorAllocation AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors = 1);
 
    /**
      * 释放无用的描述符。这只能使用已完成帧计数调用。
      */
    void ReleaseStaleDescriptors( uint64_t finishedFrame );


    //Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(UINT numDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE type);
    UINT GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE type) const;

    static uint64_t GetFrameCount()
    {
        return ms_FrameCount;
    }

protected:

    // 创建应用程序实例。
    Application(HINSTANCE hInst);
    // 销毁应用程序实例以及与此应用程序关联的所有窗口。
    virtual ~Application();

    void Initialize();

    Microsoft::WRL::ComPtr<IDXGIAdapter4> GetAdapter(bool bUseWarp);
    Microsoft::WRL::ComPtr<ID3D12Device2> CreateDevice(Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter);
    bool CheckTearingSupport();

private:
    friend LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    Application(const Application& copy) = delete;
    Application& operator=(const Application& other) = delete;

    // 用于创建此应用程序的应用程序实例句柄。
    HINSTANCE m_hInstance;
    
    Microsoft::WRL::ComPtr<ID3D12Device2> m_d3d12Device;

    std::shared_ptr<CommandQueue> m_DirectCommandQueue;
    std::shared_ptr<CommandQueue> m_ComputeCommandQueue;
    std::shared_ptr<CommandQueue> m_CopyCommandQueue;

    std::unique_ptr<DescriptorAllocator> m_DescriptorAllocators[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];

    bool m_TearingSupported;

    static uint64_t ms_FrameCount;

};