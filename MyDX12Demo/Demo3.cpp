#include "Demo3.h"

#include <Application.h>
#include <CommandQueue.h>
#include <CommandList.h>
#include <Helpers.h>
#include <Light.h>
#include <Material.h>
#include <Window.h>

#include <wrl.h>

#include "Scene/PBRMaterial.h"
using namespace Microsoft::WRL;

#include <d3dx12.h>
#include <d3dcompiler.h>
#include <DirectXColors.h>
#include <DirectXMath.h>

using namespace DirectX;

#include <algorithm> // For std::min and std::max.
#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif


struct Mat
{
    XMMATRIX ModelMatrix;
    XMMATRIX ModelViewMatrix;
    XMMATRIX InverseTransposeModelViewMatrix;
    XMMATRIX ModelViewProjectionMatrix;
};

struct LightProperties
{
    uint32_t NumPointLights;
};

enum TonemapMethod : uint32_t
{
    TM_Linear,
    TM_Reinhard,
    TM_ReinhardSq,
    TM_ACESFilmic,
};

struct TonemapParameters
{
    TonemapParameters()
        : TonemapMethod(TM_Reinhard)
        , Exposure(0.0f)
        , MaxLuminance(1.0f)
        , K(1.0f)
        , A(0.22f)
        , B(0.3f)
        , C(0.1f)
        , D(0.2f)
        , E(0.01f)
        , F(0.3f)
        , LinearWhite(11.2f)
        , Gamma(2.2f)
    {}

    // 用于执行色调映射的方法。
    TonemapMethod TonemapMethod;
    // Exposure should be expressed as a relative exposure value (-2, -1, 0, +1, +2 )
    float Exposure;

    // 用于线性色调映射的最大亮度.
    float MaxLuminance;

    // 莱因哈德常数。通常是 1.0。
    float K;

    // ACES Filmic parameters
    // See: https://www.slideshare.net/ozlael/hable-john-uncharted2-hdr-lighting/142
    float A; // Shoulder strength
    float B; // Linear strength
    float C; // Linear angle
    float D; // Toe strength
    float E; // Toe Numerator
    float F; // Toe denominator
    // Note E/F = Toe angle.
    float LinearWhite;
    float Gamma;
};

TonemapParameters g_TonemapParameters;

// 根签名参数枚举
enum RootParameters
{
    MatricesCB,             // ConstantBuffer<Mat> MatCB : register(b0);
    MaterialCB,             // ConstantBuffer<Material> MaterialCB : register( b0, space1 );
    LightPropertiesCB,      // ConstantBuffer<LightProperties> LightPropertiesCB : register( b1 );
    CameraDataCB,           // ConstantBuffer<CameraData> CameraDataCB : register(b2);
    PointLights,            // StructuredBuffer<PointLight> PointLights : register( t0 );
    SpotLights,             // StructuredBuffer<SpotLight> SpotLights : register( t1 );
    IrradianceConvolution,  // TextureCube IrradianceConvolution : register(t2);
    Prefilter,              // TextureCube PreFilter : register(t3);
    BRDFLUT,                // Texture2D BRDFLUT : register(t4);
    NumRootParameters
};

template<typename T>
constexpr const T& clamp(const T& val, const T& min = T(0), const T& max = T(1))
{
    return val < min ? min : val > max ? max : val;
}

XMMATRIX XM_CALLCONV LookAtMatrix(FXMVECTOR Position, FXMVECTOR Direction, FXMVECTOR Up)
{
    assert(!XMVector3Equal(Direction, XMVectorZero()));
    assert(!XMVector3IsInfinite(Direction));
    assert(!XMVector3Equal(Up, XMVectorZero()));
    assert(!XMVector3IsInfinite(Up));

    XMVECTOR R2 = XMVector3Normalize(Direction);

    XMVECTOR R0 = XMVector3Cross(Up, R2);
    R0 = XMVector3Normalize(R0);

    XMVECTOR R1 = XMVector3Cross(R2, R0);

    XMMATRIX M(R0, R1, R2, Position);

    return M;
}


Demo3::Demo3(const std::wstring& name, int width, int height, bool vSync)
    : super(name, width, height, vSync)
    , m_ScissorRect(CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX))
    , m_Viewport(CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)))
    , m_Forward(0)
    , m_Backward(0)
    , m_Left(0)
    , m_Right(0)
    , m_Up(0)
    , m_Down(0)
    , m_Pitch(0)
    , m_Yaw(0)
    , m_AnimateLights(false)
    , m_Shift(false)
    , m_Width(0)
    , m_Height(0)
{

    XMVECTOR cameraPos = XMVectorSet(0, 5, -20, 1);
    XMVECTOR cameraTarget = XMVectorSet(0, 5, 0, 1);
    XMVECTOR cameraUp = XMVectorSet(0, 1, 0, 0);

    m_Camera.set_LookAt(cameraPos, cameraTarget, cameraUp);

    m_pAlignedCameraData = static_cast<CameraData*>(_aligned_malloc(sizeof(CameraData), 16));

    m_pAlignedCameraData->m_InitialCamPos = m_Camera.get_Translation();
    m_pAlignedCameraData->m_InitialCamRot = m_Camera.get_Rotation();
}

Demo3::~Demo3()
{
    _aligned_free(m_pAlignedCameraData);
}

bool Demo3::LoadContent()
{
    auto device = Application::Get().GetDevice();
    auto commandQueue = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);
    auto commandList = commandQueue->GetCommandList();

    // 创建网格
    m_CubeMesh = Mesh::CreateCube(*commandList);
    m_SphereMesh = Mesh::CreateSphere(*commandList);
    m_ConeMesh = Mesh::CreateCone(*commandList);
    m_TorusMesh = Mesh::CreateTorus(*commandList);
    m_PlaneMesh = Mesh::CreatePlane(*commandList);
    // 创建一个倒置（反向围绕顺序）立方体，这样内部就不会被剪裁。
    m_SkyboxMesh = Mesh::CreateCube(*commandList, 1.0f, true);

    // 加载纹理
    commandList->LoadTextureFromFile(m_DefaultTexture, L"F:/DX12Projects/MyDX12Demo/Assets/Textures/DefaultWhite.bmp");
    commandList->LoadTextureFromFile(m_DirectXTexture, L"F:/DX12Projects/MyDX12Demo/Assets/Textures/Directx9.png");
    commandList->LoadTextureFromFile(m_EarthTexture, L"F:/DX12Projects/MyDX12Demo/Assets/Textures/earth.dds");
    commandList->LoadTextureFromFile(m_MonaLisaTexture, L"F:/DX12Projects/MyDX12Demo/Assets/Textures/Mona_Lisa.jpg");
    commandList->LoadTextureFromFile(m_GraceCathedralTexture, L"F:/DX12Projects/MyDX12Demo/Assets/HDR/newport_loft.hdr");

    // 为 HDR Pano 创建 CubeMap。
    auto cubemapDesc = m_GraceCathedralTexture.GetD3D12ResourceDesc();
    cubemapDesc.Width = cubemapDesc.Height = 1024;
    cubemapDesc.DepthOrArraySize = 6;
    cubemapDesc.MipLevels = 0;// 0-自动计算
    m_GraceCathedralCubemap = Texture(cubemapDesc, nullptr, TextureUsage::Albedo, L"Grace Cathedral Cubemap");
    commandList->PanoToCubemap(m_GraceCathedralCubemap, m_GraceCathedralTexture);

    // 将 3D cubeMap 转换为 irradiance convolution cubeMap
    auto irradianceDesc = m_GraceCathedralCubemap.GetD3D12ResourceDesc();
    irradianceDesc.Width = irradianceDesc.Height = 1024;
    irradianceDesc.DepthOrArraySize = 6;
    irradianceDesc.MipLevels = 0;// 0-自动计算
    m_IrradianceCubemap = Texture(irradianceDesc, nullptr, TextureUsage::Albedo, L"Irradiance Convolution Texture");
    commandList->CubemapToIrradianceConvolution(m_IrradianceCubemap, m_GraceCathedralCubemap);

    // TODO: 预过滤HDR环境贴图
    auto prefilterDesc = m_GraceCathedralCubemap.GetD3D12ResourceDesc();
    prefilterDesc.Width = prefilterDesc.Height = 1024;
    prefilterDesc.DepthOrArraySize = 6;
    prefilterDesc.MipLevels = 0;// 0-自动计算
    m_PrefilterCubemap = Texture(prefilterDesc, nullptr, TextureUsage::Albedo, L"Prefilter Cubemap Texture");
    commandList->CubemapToPrefilter(m_PrefilterCubemap, m_GraceCathedralCubemap);

    // TODO: BRDF LUT

    // 创建 HDR 中间RT
    DXGI_FORMAT HDRFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    DXGI_FORMAT depthBufferFormat = DXGI_FORMAT_D32_FLOAT;

    // 检查可用于给定纹理格式的最佳多重采样质量级别
    DXGI_SAMPLE_DESC sampleDesc = Application::Get().GetMultisampleQualityLevels(HDRFormat, D3D12_MAX_MULTISAMPLE_SAMPLE_COUNT);

    // 使用单个颜色缓冲区和深度缓冲区创建屏幕外RT
    auto colorDesc = CD3DX12_RESOURCE_DESC::Tex2D(HDRFormat,
        m_Width, m_Height,
        1, 1,
        sampleDesc.Count, sampleDesc.Quality,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    D3D12_CLEAR_VALUE colorClearValue;
    colorClearValue.Format = colorDesc.Format;
    colorClearValue.Color[0] = 0.4f;
    colorClearValue.Color[1] = 0.6f;
    colorClearValue.Color[2] = 0.9f;
    colorClearValue.Color[3] = 1.0f;

    Texture HDRTexture = Texture(colorDesc, &colorClearValue,
        TextureUsage::RenderTarget,
        L"HDR Texture");

    // 为 HDR RT创建深度缓冲区
    auto depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(depthBufferFormat,
        m_Width, m_Height,
        1, 1,
        sampleDesc.Count, sampleDesc.Quality,
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    D3D12_CLEAR_VALUE depthClearValue;
    depthClearValue.Format = depthDesc.Format;
    depthClearValue.DepthStencil = { 1.0f, 0 };

    Texture depthTexture = Texture(depthDesc, &depthClearValue,
        TextureUsage::Depth,
        L"Depth Render Target");

    // 将 HDR 纹理附加到 HDR 渲染目标
    m_HDRRenderTarget.AttachTexture(AttachmentPoint::Color0, HDRTexture);
    m_HDRRenderTarget.AttachTexture(AttachmentPoint::DepthStencil, depthTexture);

    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
    {
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }
    
    
    // 创建 SkyBox 根签名 和 PSO
    {
        // 加载 Skybox 着色器
        ComPtr<ID3DBlob> vs;
        ComPtr<ID3DBlob> ps;
        ThrowIfFailed(D3DReadFileToBlob(L"F:/DX12Projects/MyDX12Demo/x64/Debug/Skybox_VS.cso", &vs));
        ThrowIfFailed(D3DReadFileToBlob(L"F:/DX12Projects/MyDX12Demo/x64/Debug/Skybox_PS.cso", &ps));

        // 设置天空盒顶点着色器的输入布局
        D3D12_INPUT_ELEMENT_DESC inputLayout[1] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        // 允许输入布局并拒绝对某些管道阶段的不必要访问
        D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

        CD3DX12_DESCRIPTOR_RANGE1 descriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

        CD3DX12_ROOT_PARAMETER1 rootParameters[2];
        rootParameters[0].InitAsConstants(sizeof(DirectX::XMMATRIX) / 4, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
        rootParameters[1].InitAsDescriptorTable(1, &descriptorRange, D3D12_SHADER_VISIBILITY_PIXEL);

        CD3DX12_STATIC_SAMPLER_DESC linearClampSampler(0, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
        rootSignatureDescription.Init_1_1(2, rootParameters, 1, &linearClampSampler, rootSignatureFlags);

        m_SkyboxSignature.SetRootSignatureDesc(rootSignatureDescription.Desc_1_1, featureData.HighestVersion);

        // 设置 Skybox PSO。
        struct SkyboxPipelineState
        {
            CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
            CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT InputLayout;
            CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
            CD3DX12_PIPELINE_STATE_STREAM_VS VS;
            CD3DX12_PIPELINE_STATE_STREAM_PS PS;
            CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
            CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_DESC SampleDesc;
        } skyboxPipelineStateStream;

        skyboxPipelineStateStream.pRootSignature = m_SkyboxSignature.GetRootSignature().Get();
        skyboxPipelineStateStream.InputLayout = { inputLayout, 1 };
        skyboxPipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        skyboxPipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(vs.Get());
        skyboxPipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(ps.Get());
        skyboxPipelineStateStream.RTVFormats = m_HDRRenderTarget.GetRenderTargetFormats();
        skyboxPipelineStateStream.SampleDesc = sampleDesc;

        D3D12_PIPELINE_STATE_STREAM_DESC skyboxPipelineStateStreamDesc = {
            sizeof(SkyboxPipelineState), &skyboxPipelineStateStream
        };
        ThrowIfFailed(device->CreatePipelineState(&skyboxPipelineStateStreamDesc, IID_PPV_ARGS(&m_SkyboxPipelineState)));
    }

    // 创建 HDR 根签名 和 PSO
    {
        // 加载 HDR 着色器
        ComPtr<ID3DBlob> vs;
        ComPtr<ID3DBlob> ps;
        ThrowIfFailed(D3DReadFileToBlob(L"F:/DX12Projects/MyDX12Demo/x64/Debug/HDR_VS.cso", &vs));
        ThrowIfFailed(D3DReadFileToBlob(L"F:/DX12Projects/MyDX12Demo/x64/Debug/HDR_PS.cso", &ps));

        // 允许输入布局并拒绝对某些管道阶段的不必要访问
        D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

        CD3DX12_DESCRIPTOR_RANGE1 descriptorRange1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
        CD3DX12_DESCRIPTOR_RANGE1 descriptorRange2(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);
        CD3DX12_DESCRIPTOR_RANGE1 descriptorRange3(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);

        CD3DX12_ROOT_PARAMETER1 rootParameters[RootParameters::NumRootParameters];
        rootParameters[RootParameters::MatricesCB].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_VERTEX);
        rootParameters[RootParameters::MaterialCB].InitAsConstantBufferView(0, 1, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_PIXEL);
        rootParameters[RootParameters::LightPropertiesCB].InitAsConstants(sizeof(LightProperties) / 4, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);
        rootParameters[RootParameters::CameraDataCB].InitAsConstantBufferView(2, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_PIXEL);
        rootParameters[RootParameters::PointLights].InitAsShaderResourceView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_PIXEL);
        rootParameters[RootParameters::SpotLights].InitAsShaderResourceView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_PIXEL);
        rootParameters[RootParameters::IrradianceConvolution].InitAsDescriptorTable(1, &descriptorRange1, D3D12_SHADER_VISIBILITY_PIXEL);
        rootParameters[RootParameters::Prefilter].InitAsDescriptorTable(1, &descriptorRange2, D3D12_SHADER_VISIBILITY_PIXEL);
        rootParameters[RootParameters::BRDFLUT].InitAsDescriptorTable(1, &descriptorRange3, D3D12_SHADER_VISIBILITY_PIXEL);

        CD3DX12_STATIC_SAMPLER_DESC linearRepeatSampler(0, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR);
        CD3DX12_STATIC_SAMPLER_DESC anisotropicSampler(0, D3D12_FILTER_ANISOTROPIC);

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
        rootSignatureDescription.Init_1_1(RootParameters::NumRootParameters, rootParameters, 1, &linearRepeatSampler, rootSignatureFlags);

        m_HDRRootSignature.SetRootSignatureDesc(rootSignatureDescription.Desc_1_1, featureData.HighestVersion);

        // 设置 HDR PSO。
        struct HDRPipelineStateStream
        {
            CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
            CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT InputLayout;
            CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
            CD3DX12_PIPELINE_STATE_STREAM_VS VS;
            CD3DX12_PIPELINE_STATE_STREAM_PS PS;
            CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
            CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
            CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_DESC SampleDesc;
        } hdrPipelineStateStream;

        hdrPipelineStateStream.pRootSignature = m_HDRRootSignature.GetRootSignature().Get();
        hdrPipelineStateStream.InputLayout = { VertexPositionNormalTexture::InputElements, VertexPositionNormalTexture::InputElementCount };
        hdrPipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        hdrPipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(vs.Get());
        hdrPipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(ps.Get());
        hdrPipelineStateStream.DSVFormat = m_HDRRenderTarget.GetDepthStencilFormat();
        hdrPipelineStateStream.RTVFormats = m_HDRRenderTarget.GetRenderTargetFormats();
        hdrPipelineStateStream.SampleDesc = sampleDesc;

        D3D12_PIPELINE_STATE_STREAM_DESC hdrPipelineStateStreamDesc = {
            sizeof(HDRPipelineStateStream), &hdrPipelineStateStream
        };
        ThrowIfFailed(device->CreatePipelineState(&hdrPipelineStateStreamDesc, IID_PPV_ARGS(&m_HDRPipelineState)));
    }

    // 创建 SDR 根签名 和 PSO
    {
        CD3DX12_DESCRIPTOR_RANGE1 descriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

        CD3DX12_ROOT_PARAMETER1 rootParameters[2];
        rootParameters[0].InitAsConstants(sizeof(TonemapParameters) / 4, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
        rootParameters[1].InitAsDescriptorTable(1, &descriptorRange, D3D12_SHADER_VISIBILITY_PIXEL);

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
        rootSignatureDescription.Init_1_1(2, rootParameters);

        m_SDRRootSignature.SetRootSignatureDesc(rootSignatureDescription.Desc_1_1, featureData.HighestVersion);

        // 创建 SDR PSO
        ComPtr<ID3DBlob> vs;
        ComPtr<ID3DBlob> ps;
        ThrowIfFailed(D3DReadFileToBlob(L"F:/DX12Projects/MyDX12Demo/x64/Debug/HDRtoSDR_VS.cso", &vs));
        ThrowIfFailed(D3DReadFileToBlob(L"F:/DX12Projects/MyDX12Demo/x64/Debug/HDRtoSDR_PS.cso", &ps));

        CD3DX12_RASTERIZER_DESC rasterizerDesc(D3D12_DEFAULT);
        rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;

        struct SDRPipelineStateStream
        {
            CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
            CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
            CD3DX12_PIPELINE_STATE_STREAM_VS VS;
            CD3DX12_PIPELINE_STATE_STREAM_PS PS;
            CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER Rasterizer;
            CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
        } sdrPipelineStateStream;

        sdrPipelineStateStream.pRootSignature = m_SDRRootSignature.GetRootSignature().Get();
        sdrPipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        sdrPipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(vs.Get());
        sdrPipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(ps.Get());
        sdrPipelineStateStream.Rasterizer = rasterizerDesc;
        sdrPipelineStateStream.RTVFormats = m_pWindow->GetRenderTarget().GetRenderTargetFormats();

        D3D12_PIPELINE_STATE_STREAM_DESC sdrPipelineStateStreamDesc = {
            sizeof(SDRPipelineStateStream), &sdrPipelineStateStream
        };
        ThrowIfFailed(device->CreatePipelineState(&sdrPipelineStateStreamDesc, IID_PPV_ARGS(&m_SDRPipelineState)));
    }

    auto fenceValue = commandQueue->ExecuteCommandList(commandList);
    commandQueue->WaitForFenceValue(fenceValue);

    return true;
}

void Demo3::OnResize(ResizeEventArgs& e)
{
    super::OnResize(e);

    if (m_Width != e.Width || m_Height != e.Height)
    {
        m_Width = std::max(1, e.Width);
        m_Height = std::max(1, e.Height);

        float aspectRatio = m_Width / (float)m_Height;
        m_Camera.set_Projection(45.0f, aspectRatio, 0.1f, 100.0f);

        m_Viewport = CD3DX12_VIEWPORT(0.0f, 0.0f,
            static_cast<float>(m_Width), static_cast<float>(m_Height));

        m_HDRRenderTarget.Resize(m_Width, m_Height);
    }
}

void Demo3::UnloadContent()
{
}

void Demo3::OnUpdate(UpdateEventArgs& e)
{
    static uint64_t frameCount = 0;
    static double totalTime = 0.0;

    super::OnUpdate(e);

    totalTime += e.ElapsedTime;
    frameCount++;

    if (totalTime > 1.0)
    {
        double fps = frameCount / totalTime;

        char buffer[512];
        sprintf_s(buffer, "FPS: %f\n", fps);
        OutputDebugStringA(buffer);

        frameCount = 0;
        totalTime = 0.0;
    }
    #pragma region OnUpdate: Camera and Lights

    // 更新相机
    float speedMultipler = (m_Shift ? 16.0f : 4.0f);

    XMVECTOR cameraTranslate = XMVectorSet(m_Right - m_Left, 0.0f, m_Forward - m_Backward, 1.0f) * speedMultipler * static_cast<float>(e.ElapsedTime);
    XMVECTOR cameraPan = XMVectorSet(0.0f, m_Up - m_Down, 0.0f, 1.0f) * speedMultipler * static_cast<float>(e.ElapsedTime);
    m_Camera.Translate(cameraTranslate, Space::Local);
    m_Camera.Translate(cameraPan, Space::Local);

    XMVECTOR cameraRotation = XMQuaternionRotationRollPitchYaw(XMConvertToRadians(m_Pitch), XMConvertToRadians(m_Yaw), 0.0f);
    m_Camera.set_Rotation(cameraRotation);

    XMMATRIX viewMatrix = m_Camera.get_ViewMatrix();

    const int numPointLights = 4;

    static const XMVECTORF32 LightColors[] =
    {
        Colors::White, Colors::White, Colors::White, Colors::White
    };

    static float lightAnimTime = 0.0f;
    if (m_AnimateLights)
    {
        lightAnimTime += static_cast<float>(e.ElapsedTime) * 0.5f * XM_PI;
    }

    const float radius = 8.0f;
    const float offset = 2.0f * XM_PI / numPointLights;

    // Setup the light buffers.
    m_PointLights.resize(numPointLights);
    for (int i = 0; i < numPointLights; ++i)
    {
        PointLight& l = m_PointLights[i];

        l.PositionWS = {
            static_cast<float>(std::sin(lightAnimTime + offset * i)) * radius,
            9.0f,
            static_cast<float>(std::cos(lightAnimTime + offset * i)) * radius,
            1.0f
        };
        XMVECTOR positionWS = XMLoadFloat4(&l.PositionWS);
        XMVECTOR positionVS = XMVector3TransformCoord(positionWS, viewMatrix);
        XMStoreFloat4(&l.PositionVS, positionVS);

        l.Color = XMFLOAT4(LightColors[i]);
        l.Intensity = 1.0f;
        l.Attenuation = 0.0f;
    }

    #pragma endregion
}

#pragma region Tonemapping Functions
// Number of values to plot in the tonemapping curves.
static const int VALUES_COUNT = 256;
// Maximum HDR value to normalize the plot samples.
static const float HDR_MAX = 12.0f;

float LinearTonemapping(float HDR, float max)
{
    if (max > 0.0f)
    {
        return clamp(HDR / max);
    }
    return HDR;
}

float LinearTonemappingPlot(void*, int index)
{
    return LinearTonemapping(index / (float)VALUES_COUNT * HDR_MAX, g_TonemapParameters.MaxLuminance);
}

// Reinhard tone mapping.
// See: http://www.cs.utah.edu/~reinhard/cdrom/tonemap.pdf
float ReinhardTonemapping(float HDR, float k)
{
    return HDR / (HDR + k);
}

float ReinhardSqrTonemappingPlot(void*, int index)
{
    float reinhard = ReinhardTonemapping(index / (float)VALUES_COUNT * HDR_MAX, g_TonemapParameters.K);
    return reinhard * reinhard;
}

// ACES Filmic
// See: https://www.slideshare.net/ozlael/hable-john-uncharted2-hdr-lighting/142
float ACESFilmicTonemapping(float x, float A, float B, float C, float D, float E, float F)
{
    return (((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - (E / F));
}

float ACESFilmicTonemappingPlot(void*, int index)
{
    float HDR = index / (float)VALUES_COUNT * HDR_MAX;
    return ACESFilmicTonemapping(HDR, g_TonemapParameters.A, g_TonemapParameters.B, g_TonemapParameters.C, g_TonemapParameters.D, g_TonemapParameters.E, g_TonemapParameters.F) /
        ACESFilmicTonemapping(g_TonemapParameters.LinearWhite, g_TonemapParameters.A, g_TonemapParameters.B, g_TonemapParameters.C, g_TonemapParameters.D, g_TonemapParameters.E, g_TonemapParameters.F);
}


void XM_CALLCONV ComputeMatrices(FXMMATRIX model, CXMMATRIX view, CXMMATRIX viewProjection, Mat& mat)
{
    mat.ModelMatrix = model;
    mat.ModelViewMatrix = model * view;
    mat.InverseTransposeModelViewMatrix = XMMatrixTranspose(XMMatrixInverse(nullptr, mat.ModelMatrix));
    mat.ModelViewProjectionMatrix = model * viewProjection;
}
#pragma endregion  

void Demo3::OnRender(RenderEventArgs& e)
{
    super::OnRender(e);

    auto commandQueue = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    auto commandList = commandQueue->GetCommandList();

    // 清除RT。
    {
        FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };

        commandList->ClearTexture(m_HDRRenderTarget.GetTexture(AttachmentPoint::Color0), clearColor);
        commandList->ClearDepthStencilTexture(m_HDRRenderTarget.GetTexture(AttachmentPoint::DepthStencil), D3D12_CLEAR_FLAG_DEPTH);
    }

    commandList->SetViewport(m_Viewport);
    commandList->SetScissorRect(m_ScissorRect);

    commandList->SetRenderTarget(m_HDRRenderTarget);

    // 渲染天空盒
    {
        // 天空盒的视图矩阵应仅考虑相机的旋转，而不考虑平移
        auto viewMatrix = XMMatrixTranspose(XMMatrixRotationQuaternion(m_Camera.get_Rotation()));
        auto projMatrix = m_Camera.get_ProjectionMatrix();
        auto viewProjMatrix = viewMatrix * projMatrix;

        commandList->SetPipelineState(m_SkyboxPipelineState);
        commandList->SetGraphicsRootSignature(m_SkyboxSignature);

        commandList->SetGraphics32BitConstants(0, viewProjMatrix);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = m_GraceCathedralCubemap.GetD3D12ResourceDesc().Format;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MipLevels = (UINT)-1; // Use all mips.

        // TODO: Need a better way to bind a cubemap.
        commandList->SetShaderResourceView(1, 0, m_GraceCathedralCubemap,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 0, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, &srvDesc);

        m_SkyboxMesh->Draw(*commandList);
    }
    
#pragma region Render Scene
    
    commandList->SetPipelineState(m_HDRPipelineState);
    commandList->SetGraphicsRootSignature(m_HDRRootSignature);

    // 上传灯光数据
    LightProperties lightProps;
    lightProps.NumPointLights = static_cast<uint32_t>(m_PointLights.size());

    commandList->SetGraphics32BitConstants(RootParameters::LightPropertiesCB, lightProps);
    commandList->SetGraphicsDynamicStructuredBuffer(RootParameters::PointLights, m_PointLights);

    // 绘制球体
    XMMATRIX translationMatrix = XMMatrixTranslation(-4.0f, 2.0f, -4.0f);
    XMMATRIX rotationMatrix = XMMatrixIdentity();
    XMMATRIX scaleMatrix = XMMatrixScaling(4.0f, 4.0f, 4.0f);
    XMMATRIX worldMatrix = scaleMatrix * rotationMatrix * translationMatrix;
    XMMATRIX viewMatrix = m_Camera.get_ViewMatrix();
    XMMATRIX viewProjectionMatrix = viewMatrix * m_Camera.get_ProjectionMatrix();

    Mat matrices;
    ComputeMatrices(worldMatrix, viewMatrix, viewProjectionMatrix, matrices);
    
    commandList->SetGraphicsDynamicConstantBuffer(RootParameters::MatricesCB, matrices);
    commandList->SetGraphicsDynamicConstantBuffer(RootParameters::MaterialCB, PBRMaterial::Test);
    commandList->SetGraphicsDynamicConstantBuffer(RootParameters::CameraDataCB, m_Camera.get_Translation());

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = m_IrradianceCubemap.GetD3D12ResourceDesc().Format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.TextureCube.MipLevels = 1;
    commandList->SetShaderResourceView(RootParameters::IrradianceConvolution, 0, m_IrradianceCubemap,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE , 0, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, &srvDesc);


    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc1 = {};
    srvDesc1.Format = m_PrefilterCubemap.GetD3D12ResourceDesc().Format;
    srvDesc1.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc1.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srvDesc1.TextureCube.MipLevels = (UINT)-1; // Use all mips.
    commandList->SetShaderResourceView(RootParameters::Prefilter,  0, m_PrefilterCubemap,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE , 0, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, &srvDesc1);
    
    m_SphereMesh->Draw(*commandList);


    // 可视化场景中灯光的位置。
    Material lightMaterial;
    // No specular
    lightMaterial.Specular = { 0, 0, 0, 1 };
    for (const auto& l : m_PointLights)
    {
        lightMaterial.Emissive = l.Color;
        XMVECTOR lightPos = XMLoadFloat4(&l.PositionWS);
        worldMatrix = XMMatrixTranslationFromVector(lightPos);
        ComputeMatrices(worldMatrix, viewMatrix, viewProjectionMatrix, matrices);

        commandList->SetGraphicsDynamicConstantBuffer(RootParameters::MatricesCB, matrices);

        m_SphereMesh->Draw(*commandList);
    }


#pragma endregion

    
    // 将 HDR -> SDR 色调映射直接执行到窗口的RT。
    commandList->SetRenderTarget(m_pWindow->GetRenderTarget());
    commandList->SetPipelineState(m_SDRPipelineState);
    commandList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->SetGraphicsRootSignature(m_SDRRootSignature);
    commandList->SetGraphics32BitConstants(0, g_TonemapParameters);
    commandList->SetShaderResourceView(1, 0, m_HDRRenderTarget.GetTexture(Color0), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    commandList->Draw(3);

    commandQueue->ExecuteCommandList(commandList);
    
    // Present
    m_pWindow->Present();
}

static bool g_AllowFullscreenToggle = true;

void Demo3::OnKeyPressed(KeyEventArgs& e)
{
    super::OnKeyPressed(e);


    switch (e.Key)
    {
    case KeyCode::Escape:
        Application::Get().Quit(0);
        break;
    case KeyCode::Enter:
        if (e.Alt)
        {
        case KeyCode::F11:
            if (g_AllowFullscreenToggle)
            {
                m_pWindow->ToggleFullscreen();
                g_AllowFullscreenToggle = false;
            }
            break;
        }
    case KeyCode::V:
        m_pWindow->ToggleVSync();
        break;
    case KeyCode::R:
        // Reset camera transform
        m_Camera.set_Translation(m_pAlignedCameraData->m_InitialCamPos);
        m_Camera.set_Rotation(m_pAlignedCameraData->m_InitialCamRot);
        m_Pitch = 0.0f;
        m_Yaw = 0.0f;
        break;
    case KeyCode::Up:
    case KeyCode::W:
        m_Forward = 1.0f;
        break;
    case KeyCode::Left:
    case KeyCode::A:
        m_Left = 1.0f;
        break;
    case KeyCode::Down:
    case KeyCode::S:
        m_Backward = 1.0f;
        break;
    case KeyCode::Right:
    case KeyCode::D:
        m_Right = 1.0f;
        break;
    case KeyCode::Q:
        m_Down = 1.0f;
        break;
    case KeyCode::E:
        m_Up = 1.0f;
        break;
    case KeyCode::Space:
        m_AnimateLights = !m_AnimateLights;
        break;
    case KeyCode::ShiftKey:
        m_Shift = true;
        break;
    }
}

void Demo3::OnKeyReleased(KeyEventArgs& e)
{
    super::OnKeyReleased(e);


    switch (e.Key)
    {
    case KeyCode::Enter:
        if (e.Alt)
        {
        case KeyCode::F11:
            g_AllowFullscreenToggle = true;
        }
        break;
    case KeyCode::Up:
    case KeyCode::W:
        m_Forward = 0.0f;
        break;
    case KeyCode::Left:
    case KeyCode::A:
        m_Left = 0.0f;
        break;
    case KeyCode::Down:
    case KeyCode::S:
        m_Backward = 0.0f;
        break;
    case KeyCode::Right:
    case KeyCode::D:
        m_Right = 0.0f;
        break;
    case KeyCode::Q:
        m_Down = 0.0f;
        break;
    case KeyCode::E:
        m_Up = 0.0f;
        break;
    case KeyCode::ShiftKey:
        m_Shift = false;
        break;
    }
}

void Demo3::OnMouseMoved(MouseMotionEventArgs& e)
{
    super::OnMouseMoved(e);

    const float mouseSpeed = 0.1f;

    if (e.LeftButton)
    {
        m_Pitch -= e.RelY * mouseSpeed;

        m_Pitch = clamp(m_Pitch, -90.0f, 90.0f);

        m_Yaw -= e.RelX * mouseSpeed;
    }
}


void Demo3::OnMouseWheel(MouseWheelEventArgs& e)
{
    auto fov = m_Camera.get_FoV();

    fov -= e.WheelDelta;
    fov = clamp(fov, 12.0f, 90.0f);

    m_Camera.set_FoV(fov);

    char buffer[256];
    sprintf_s(buffer, "FoV: %f\n", fov);
    OutputDebugStringA(buffer);
}
