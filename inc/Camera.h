#pragma once
#include <DirectXMath.h>

enum class Space
{
    Local,
    World,
};

class Camera
{
public:

    Camera();
    virtual ~Camera();

    // View
    void XM_CALLCONV set_LookAt( DirectX::FXMVECTOR eye, DirectX::FXMVECTOR target, DirectX::FXMVECTOR up );
    DirectX::XMMATRIX get_ViewMatrix() const;
    DirectX::XMMATRIX get_InverseViewMatrix() const;

    // Projection
    void set_Projection( float fovy, float aspect, float zNear, float zFar );
    DirectX::XMMATRIX get_ProjectionMatrix() const;
    DirectX::XMMATRIX get_InverseProjectionMatrix() const;
    
    void set_FoV(float fovy);
    float get_FoV() const;

    /**
     * 设置相机世界空间位置
     */
    void XM_CALLCONV set_Translation( DirectX::FXMVECTOR translation );
    DirectX::XMVECTOR get_Translation() const;
    
    /**
      * 设置相机在世界空间中的旋转
      * @param rotation 旋转四元数
      */
    void XM_CALLCONV set_Rotation( DirectX::FXMVECTOR rotation );
    DirectX::XMVECTOR get_Rotation() const;

    void XM_CALLCONV Translate( DirectX::FXMVECTOR translation, Space space = Space::Local );
    void Rotate( DirectX::FXMVECTOR quaternion );

protected:
    virtual void UpdateViewMatrix() const;
    virtual void UpdateInverseViewMatrix() const;
    virtual void UpdateProjectionMatrix() const;
    virtual void UpdateInverseProjectionMatrix() const;

    // 此数据必须对齐，否则 SSE（Streaming SIMD Extensions） 内部函数将失败并引发异常。
    struct alignas(16) AlignedData
    {
        // 相机的世界空间位置。
        DirectX::XMVECTOR m_Translation;
        // 相机的世界空间旋转（四元数）
        DirectX::XMVECTOR m_Rotation;

        DirectX::XMMATRIX m_ViewMatrix, m_InverseViewMatrix;
        DirectX::XMMATRIX m_ProjectionMatrix, m_InverseProjectionMatrix;
    };
    AlignedData* pData;

    // 投影参数
    float m_vFoV;
    float m_AspectRatio;
    float m_zNear;
    float m_zFar;

    // 如果需要更新view矩阵，则为 True。
    mutable bool m_ViewDirty, m_InverseViewDirty;
    // 如果需要更新projection矩阵，则为 True。
    mutable bool m_ProjectionDirty, m_InverseProjectionDirty;

private:
};
