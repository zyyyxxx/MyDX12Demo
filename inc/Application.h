/**
* The application class is used to create windows for our application.
*/
#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

#include <memory>
#include <string>

class Window;
class Game;
class CommandQueue;

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
    * Find a window by the window name.
    */
    std::shared_ptr<Window> GetWindowByName(const std::wstring& windowName);

    /**
    * Run the application loop and message pump.
    * @return The error code if an error occurred.
    */
    int Run(std::shared_ptr<Game> pGame);

    /**
    * Request to quit the application and close all windows.
    * @param exitCode The error code to return to the invoking process.
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

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(UINT numDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE type);
    UINT GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE type) const;

protected:

    // Create an application instance.
    Application(HINSTANCE hInst);
    // Destroy the application instance and all windows associated with this application.
    virtual ~Application();

    Microsoft::WRL::ComPtr<IDXGIAdapter4> GetAdapter(bool bUseWarp);
    Microsoft::WRL::ComPtr<ID3D12Device2> CreateDevice(Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter);
    bool CheckTearingSupport();

private:
    Application(const Application& copy) = delete;
    Application& operator=(const Application& other) = delete;

    // The application instance handle that this application was created with.
    HINSTANCE m_hInstance;

    Microsoft::WRL::ComPtr<IDXGIAdapter4> m_dxgiAdapter;
    Microsoft::WRL::ComPtr<ID3D12Device2> m_d3d12Device;

    std::shared_ptr<CommandQueue> m_DirectCommandQueue;
    std::shared_ptr<CommandQueue> m_ComputeCommandQueue;
    std::shared_ptr<CommandQueue> m_CopyCommandQueue;

    bool m_TearingSupported;

};