
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_5.h>

#include <Events.h>
#include <HighResolutionClock.h>
#include <RenderTarget.h>
#include <Texture.h>

#include <memory>

class Game;
class Texture;

/**
* @brief 应用程序的窗口。
*/
class Window : public std::enable_shared_from_this<Window>
{
public:
    // swapchain back buffers 数量
    static const UINT BufferCount = 3;

    /**
    * 获取此窗口实例的句柄
    * @returns 窗口实例的句柄，如果这不是有效的窗口，则为 nullptr
    */
    HWND GetWindowHandle() const;

    /**
      * 初始化窗口
      */
    void Initialize();

    /**
    * 销毁此窗口。
    */
    void Destroy();

    const std::wstring& GetWindowName() const;

    int GetClientWidth() const;
    int GetClientHeight() const;

    /**
    * 此窗口是否应使用VSync。
    */
    bool IsVSync() const;
    void SetVSync(bool vSync);
    void ToggleVSync();

    /**
    * 窗口/全屏？
    */
    bool IsFullScreen() const;

    // 设置窗口的全屏状态
    void SetFullscreen(bool fullscreen);
    void ToggleFullscreen();

    /**
     * 显示此窗口
     */
    void Show();

    /**
      * 隐藏窗口
      */
    void Hide();

    /**
    * 获取窗口的RT。
    * 此方法应在每帧调用一次，因为颜色连接点会根据窗口的当前后台缓冲区而变化.
    */
    const RenderTarget& GetRenderTarget() const;

    /**
     * Return the current back buffer index.
     */
    UINT GetCurrentBackBufferIndex() const;

    /**
      * 显示交换链的当前back buffer到屏幕。
      * 返回back buffer的索引。
      * @param texture 在渲染之前要复制到交换链的后台缓冲区的纹理。
      * 默认情况下，这是一个空纹理。在这种情况下，不会执行任何复制。使用 Window::GetRenderTarget 方法获取窗口颜色缓冲区的RT. 
      */
    UINT Present(const Texture& texture = Texture());


protected:
    // Window 过程需要调用此类的protected方法。
    friend LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    // 只有应用程序可以创建窗口。
    friend class Application;
    // Game 类需要将自身注册到窗口。
    friend class Game;

    Window() = delete;
    Window(HWND hWnd, const std::wstring& windowName, int clientWidth, int clientHeight, bool vSync );
    virtual ~Window();

    // 注册Game类实例 
    // 这允许窗口回调 Game 类中的函数。
    void RegisterCallbacks( std::shared_ptr<Game> pGame );

    // Update 和 Draw 只能由应用程序调用。
    virtual void OnUpdate(UpdateEventArgs& e);
    virtual void OnRender(RenderEventArgs& e);
    
    virtual void OnKeyPressed(KeyEventArgs& e);
    virtual void OnKeyReleased(KeyEventArgs& e);


    virtual void OnMouseMoved(MouseMotionEventArgs& e);
    virtual void OnMouseButtonPressed(MouseButtonEventArgs& e);
    virtual void OnMouseButtonReleased(MouseButtonEventArgs& e);
    virtual void OnMouseWheel(MouseWheelEventArgs& e);
    
    virtual void OnResize(ResizeEventArgs& e);

    // 创建交换链
    Microsoft::WRL::ComPtr<IDXGISwapChain4> CreateSwapChain();

    // 更新交换链后台缓冲区的RTV。
    void UpdateRenderTargetViews();

private:
    // 不应复制 Window
    Window(const Window& copy) = delete;
    Window& operator=(const Window& other) = delete;

    HWND m_hWnd;

    std::wstring m_WindowName;
    
    int m_ClientWidth;
    int m_ClientHeight;
    bool m_VSync;
    bool m_Fullscreen;

    HighResolutionClock m_UpdateClock;
    HighResolutionClock m_RenderClock;

    UINT64 m_FenceValues[BufferCount];
    uint64_t m_FrameValues[BufferCount];

    std::weak_ptr<Game> m_pGame;

    Microsoft::WRL::ComPtr<IDXGISwapChain4> m_dxgiSwapChain;
    Texture m_BackBufferTextures[BufferCount];
    
    // 标记为mutable以允许在 const 函数中进行修改。
    mutable RenderTarget m_RenderTarget;

    UINT m_CurrentBackBufferIndex;

    RECT m_WindowRect;
    bool m_IsTearingSupported;

    int m_PreviousMouseX;
    int m_PreviousMouseY;


};