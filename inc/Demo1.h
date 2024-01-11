#pragma once
 
#include <Game.h>
#include <Window.h>
 
#include <DirectXMath.h>

class Demo1 : public Game
{
public:
    using super = Game;
 
    Demo1(const std::wstring& name, int width, int height, bool vSync = false);
    
    /**
     *  加载内容
     */
    virtual bool LoadContent() override;
 
    /**
     *  传已经加载的内容
     */
    virtual void UnloadContent() override;

protected:
    /**
     *  更新游戏逻辑
     */
    virtual void OnUpdate(UpdateEventArgs& e) override;
    
    /**
     *  Render stuff.
     */
    virtual void OnRender(RenderEventArgs& e) override;
    
    /**
     * Invoked by the registered window when a key is pressed
     * while the window has focus.
     */
    virtual void OnKeyPressed(KeyEventArgs& e) override;
    
    /**
     * Invoked when the mouse wheel is scrolled while the registered window has focus.
     */
    virtual void OnMouseWheel(MouseWheelEventArgs& e) override;
    
    
    virtual void OnResize(ResizeEventArgs& e) override;
private:
     // 辅助函数
     // 转换资源
     void TransitionResource(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList,
         Microsoft::WRL::ComPtr<ID3D12Resource> resource,
         D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState);

     // 清除RTV
    void ClearRTV(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList,
        D3D12_CPU_DESCRIPTOR_HANDLE rtv, FLOAT* clearColor);

     // 清除DSV
    void ClearDepth(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList,
        D3D12_CPU_DESCRIPTOR_HANDLE dsv, FLOAT depth = 1.0f );

     // 创建GPU缓冲区
    void UpdateBufferResource(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList,
        ID3D12Resource** pDestinationResource, ID3D12Resource** pIntermediateResource,
        size_t numElements, size_t elementSize, const void* bufferData, 
        D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE );

    // 调整深度缓冲区的大小。
    void ResizeDepthBuffer(int width, int height);

    uint64_t m_FenceValues[Window::BufferCount] = {};

    // Vertex buffer for the cube.
    Microsoft::WRL::ComPtr<ID3D12Resource> m_VertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView;
    // Index buffer for the cube.
    Microsoft::WRL::ComPtr<ID3D12Resource> m_IndexBuffer;
    D3D12_INDEX_BUFFER_VIEW m_IndexBufferView;
    
    // Depth buffer.
    Microsoft::WRL::ComPtr<ID3D12Resource> m_DepthBuffer;
    // Descriptor heap for depth buffer.
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_DSVHeap;

 
    // 根签名
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_RootSignature;
    
    // PSO
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_PipelineState;
    
    D3D12_VIEWPORT m_Viewport;
    D3D12_RECT m_ScissorRect;

    float m_FoV;
 
    DirectX::XMMATRIX m_ModelMatrix;
    DirectX::XMMATRIX m_ViewMatrix;
    DirectX::XMMATRIX m_ProjectionMatrix;
 
    bool m_ContentLoaded;
};