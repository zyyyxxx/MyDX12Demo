#pragma once

#include <Camera.h>
#include <Game.h>
#include <IndexBuffer.h>
#include <Light.h>
#include <Window.h>
#include <Mesh.h>
#include <RenderTarget.h>
#include <RootSignature.h>
#include <Texture.h>
#include <VertexBuffer.h>

#include <DirectXMath.h>

class Demo3 : public Game
{
public:
    using super = Game;

    Demo3(const std::wstring& name, int width, int height, bool vSync = false);
    virtual ~Demo3() override;
    
    virtual bool LoadContent() override;

    virtual void UnloadContent() override;

protected:

    virtual void OnUpdate(UpdateEventArgs& e) override;


    virtual void OnRender(RenderEventArgs& e) override;
    
    virtual void OnKeyPressed(KeyEventArgs& e) override;
    
    virtual void OnKeyReleased(KeyEventArgs& e) override;
    
    virtual void OnMouseMoved(MouseMotionEventArgs& e) override;
    
    virtual void OnMouseWheel(MouseWheelEventArgs& e) override;
    
    virtual void OnResize(ResizeEventArgs& e) override;

private:
    // geometry
    std::unique_ptr<Mesh> m_CubeMesh;
    std::unique_ptr<Mesh> m_SphereMesh;
    std::unique_ptr<Mesh> m_ConeMesh;
    std::unique_ptr<Mesh> m_TorusMesh;
    std::unique_ptr<Mesh> m_PlaneMesh;

    std::unique_ptr<Mesh> m_SkyboxMesh;

    Texture m_DefaultTexture;
    Texture m_DirectXTexture;
    Texture m_EarthTexture;
    Texture m_MonaLisaTexture;
    Texture m_GraceCathedralTexture;
    Texture m_GraceCathedralCubemap;
    Texture m_IrradianceCubemap;

    // HDR RT
    RenderTarget m_HDRRenderTarget;

    // 根签名
    RootSignature m_SkyboxSignature;
    RootSignature m_HDRRootSignature;
    RootSignature m_SDRRootSignature;

    // PSO
    // Skybox PSO
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SkyboxPipelineState;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_HDRPipelineState;
    // HDR -> SDR tone mapping PSO.
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SDRPipelineState;

    D3D12_VIEWPORT m_Viewport;
    D3D12_RECT m_ScissorRect;

    Camera m_Camera;
    struct alignas( 16 ) CameraData
    {
        DirectX::XMVECTOR m_InitialCamPos;
        DirectX::XMVECTOR m_InitialCamRot;
    };
    CameraData* m_pAlignedCameraData;

    // Camera controller
    float m_Forward;
    float m_Backward;
    float m_Left;
    float m_Right;
    float m_Up;
    float m_Down;

    float m_Pitch;
    float m_Yaw;

    // 旋转灯光
    bool m_AnimateLights;
    // 如果按下 Shift 键，则设置为 true
    bool m_Shift;

    int m_Width;
    int m_Height;

    // 定义灯光
    std::vector<PointLight> m_PointLights;
    std::vector<SpotLight> m_SpotLights;
};
