/**
*   @brief The Game class is the abstract base class for DirecX 12 demos.
 */
#pragma once

#include <Events.h>

#include <memory> // for std::enable_shared_from_this
#include <string> // for std::wstring

class Window;

// 游戏基类
class Game : public std::enable_shared_from_this<Game>
{
public:
    /**
    * 创建一个使用指定窗口的Demo
    */
    Game(const std::wstring& name, int width, int height, bool vSync);
    virtual ~Game();
 
    int GetClientWidth() const
    {
     return m_Width;
    }

    int GetClientHeight() const
    {
     return m_Height;
    }
 
    /**
     *  初始化 DirectX Runtime。
     */
    virtual bool Initialize();

    /**
     *  加载Demo所需的内容。
     */
    virtual bool LoadContent() = 0;

    /**
     *  卸载在 LoadContent 中加载 Demo 内容。
     */
    virtual void UnloadContent() = 0;

    /**
     * 销毁使用的任何资源。
     */
    virtual void Destroy();

protected:
    friend class Window;

    /**
     *  更新游戏逻辑。
     */
    virtual void OnUpdate(UpdateEventArgs& e);

    /**
     *  渲染
     */
    virtual void OnRender(RenderEventArgs& e);

    virtual void OnKeyPressed(KeyEventArgs& e);
 
    virtual void OnKeyReleased(KeyEventArgs& e);
 
    virtual void OnMouseMoved(MouseMotionEventArgs& e);
 
    virtual void OnMouseButtonPressed(MouseButtonEventArgs& e);
 
    virtual void OnMouseButtonReleased(MouseButtonEventArgs& e);
 
    virtual void OnMouseWheel(MouseWheelEventArgs& e);
 
    virtual void OnResize(ResizeEventArgs& e);

    /**
     * 在销毁已注册的窗口实例时调用
     */
    virtual void OnWindowDestroy();
    std::shared_ptr<Window> m_pWindow;

private:
    std::wstring m_Name;
    int m_Width;
    int m_Height;
    bool m_vSync;
};
