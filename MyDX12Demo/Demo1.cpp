#include <Demo1.h>
 
#include <Application.h>
#include <CommandQueue.h>
#include <Helpers.h>
#include <Window.h>
 
#include <wrl.h>
using namespace Microsoft::WRL;
 
#include <d3dx12.h>
#include <d3dcompiler.h>
#pragma comment(lib,"d3dcompiler.lib")
 
#include <algorithm> // For std::min and std::max.
#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif
 
using namespace DirectX;


// cube 的顶点数据
struct VertexPosColor
{
    XMFLOAT3 Position;
    XMFLOAT3 Color;
};

static VertexPosColor g_Vertices[8] = {
    { XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f, 0.0f, 0.0f) }, // 0
    { XMFLOAT3(-1.0f,  1.0f, -1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f) }, // 1
    { XMFLOAT3(1.0f,  1.0f, -1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f) }, // 2
    { XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) }, // 3
    { XMFLOAT3(-1.0f, -1.0f,  1.0f), XMFLOAT3(0.0f, 0.0f, 1.0f) }, // 4
    { XMFLOAT3(-1.0f,  1.0f,  1.0f), XMFLOAT3(0.0f, 1.0f, 1.0f) }, // 5
    { XMFLOAT3(1.0f,  1.0f,  1.0f), XMFLOAT3(1.0f, 1.0f, 1.0f) }, // 6
    { XMFLOAT3(1.0f, -1.0f,  1.0f), XMFLOAT3(1.0f, 0.0f, 1.0f) }  // 7
};
static WORD g_Indicies[36] =
{
    0, 1, 2, 0, 2, 3,
    4, 6, 5, 4, 7, 6,
    4, 5, 1, 4, 1, 0,
    3, 2, 6, 3, 6, 7,
    1, 5, 6, 1, 6, 2,
    4, 0, 3, 4, 3, 7
};



Demo1::Demo1(const std::wstring& name, int width, int height, bool vSync)
    : super(name, width, height, vSync)
    , m_Viewport(CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)))
    , m_ScissorRect(CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX))
    , m_FoV(45.0)
    , m_ContentLoaded(false)
{
}

// 创建一个ID3D12Resource足够大的缓冲区来存储传递给函数的缓冲区数据，并创建一个中间缓冲区，用于将CPU缓冲区数据复制到GPU
void Demo1::UpdateBufferResource(
    ComPtr<ID3D12GraphicsCommandList2> commandList,
    ID3D12Resource** pDestinationResource,
    ID3D12Resource** pIntermediateResource,
    size_t numElements, size_t elementSize, const void* bufferData,
    D3D12_RESOURCE_FLAGS flags)
{
    auto device = Application::Get().GetDevice();

    size_t bufferSize = numElements * elementSize;
    
    {
        CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);
        CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize, flags);

        // Create a committed resource for the GPU resource in a default heap. 默认堆
        ThrowIfFailed(device->CreateCommittedResource( //创建资源和足够大以存储该资源的隐式堆。该资源也映射到隐式堆。
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(pDestinationResource)));
    }
    // Create an committed resource for the upload. 上传堆
    if (bufferData)
    { // 创建了默认资源，但缓冲区数据尚未上传到该资源。
        //下一步是创建另一个资源，用于将 CPU 缓冲区数据传输到 GPU 内存。为了执行内存传输，使用上传堆创建中间缓冲区资源。
		CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);

        ThrowIfFailed(device->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, //在堆中创建的D3D12_HEAP_TYPE_UPLOAD 必须使用D3D12_RESOURCE_STATE_GENERIC_READ创建且不能更改。
            nullptr,
            IID_PPV_ARGS(pIntermediateResource)));

        D3D12_SUBRESOURCE_DATA subresourceData = {};
        subresourceData.pData = bufferData;
        subresourceData.RowPitch = bufferSize;
        subresourceData.SlicePitch = subresourceData.RowPitch;
        //上传堆中的中间缓冲区将CPU缓冲区数据上传到默认堆中的GPU资源。
        UpdateSubresources(commandList.Get(),
            *pDestinationResource, *pIntermediateResource,
            0, 0, 1, &subresourceData);
    }
}


bool Demo1::LoadContent()
{
    auto device = Application::Get().GetDevice();
    auto commandQueue = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);
    auto commandList = commandQueue->GetCommandList();

    // 上传 Upload vertex buffer data.
    ComPtr<ID3D12Resource> intermediateVertexBuffer;
    UpdateBufferResource(commandList,
        &m_VertexBuffer, &intermediateVertexBuffer,
        _countof(g_Vertices), sizeof(VertexPosColor), g_Vertices);

    // 创建 vertex buffer view
    m_VertexBufferView.BufferLocation = m_VertexBuffer->GetGPUVirtualAddress();
    m_VertexBufferView.SizeInBytes = sizeof(g_Vertices);
    m_VertexBufferView.StrideInBytes = sizeof(VertexPosColor);

    // 上传 Upload index buffer data.
    ComPtr<ID3D12Resource> intermediateIndexBuffer;
    UpdateBufferResource(commandList,
        &m_IndexBuffer, &intermediateIndexBuffer,
        _countof(g_Indicies), sizeof(WORD), g_Indicies);

    // 创建 index buffer view.
    m_IndexBufferView.BufferLocation = m_IndexBuffer->GetGPUVirtualAddress();
    m_IndexBufferView.Format = DXGI_FORMAT_R16_UINT;
    m_IndexBufferView.SizeInBytes = sizeof(g_Indicies);

    // 创建DSV的描述符堆
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_DSVHeap)));

    // 加载 vertex shader.
    ComPtr<ID3DBlob> vertexShaderBlob;
    ThrowIfFailed(D3DReadFileToBlob(L"VertexShader.cso", &vertexShaderBlob));

    // 加载 pixel shader.
    ComPtr<ID3DBlob> pixelShaderBlob;
    ThrowIfFailed(D3DReadFileToBlob(L"PixelShader.cso", &pixelShaderBlob));

    // Create the vertex input layout 顶点着色器输入格式
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

#pragma region Root Signature
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
    {
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }

    // Allow input layout and deny unnecessary access to certain pipeline stages. 定义根签名标志
    D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

    // A single 32-bit constant root parameter that is used by the vertex shader. 根参数
    CD3DX12_ROOT_PARAMETER1 rootParameters[1];
    rootParameters[0].InitAsConstants(sizeof(XMMATRIX) / 4, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
    
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
    rootSignatureDescription.Init_1_1(_countof(rootParameters), rootParameters, 0, nullptr, rootSignatureFlags);

    // 序列化根签名
    ComPtr<ID3DBlob> rootSignatureBlob;
    ComPtr<ID3DBlob> errorBlob;
    ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDescription,
        featureData.HighestVersion, &rootSignatureBlob, &errorBlob));
    // 创建根签名
    ThrowIfFailed(device->CreateRootSignature(0, rootSignatureBlob->GetBufferPointer(),
        rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_RootSignature)));
#pragma endregion

#pragma region PSO
    struct PipelineStateStream
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT InputLayout;
        CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
        CD3DX12_PIPELINE_STATE_STREAM_VS VS;
        CD3DX12_PIPELINE_STATE_STREAM_PS PS;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
    } pipelineStateStream;
    //定义渲染目标的数量和渲染目标格式
    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
    rtvFormats.NumRenderTargets = 1;
    rtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    //描述 PSO
    pipelineStateStream.pRootSignature = m_RootSignature.Get();
    pipelineStateStream.InputLayout = { inputLayout, _countof(inputLayout) };
    pipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(vertexShaderBlob.Get());
    pipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(pixelShaderBlob.Get());
    pipelineStateStream.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pipelineStateStream.RTVFormats = rtvFormats;
    //创建实际的 PSO
    D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc = {
        sizeof(PipelineStateStream), &pipelineStateStream
    };
    ThrowIfFailed(device->CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&m_PipelineState)));
#pragma endregion

    //在离开该LoadContent方法之前，必须在命令队列上执行命令列表，以确保索引和顶点缓冲区在渲染之前上传到 GPU 资源。
    auto fenceValue = commandQueue->ExecuteCommandList(commandList);
    commandQueue->WaitForFenceValue(fenceValue);

    m_ContentLoaded = true;

    // Resize/Create the depth buffer. 创建描述符堆后，现在就可以安全地创建深度缓冲区了。
    ResizeDepthBuffer(GetClientWidth(), GetClientHeight());

    return true;
}

void Demo1::ResizeDepthBuffer(int width, int height)
{
    if (m_ContentLoaded)
    {
        // Flush any GPU commands that might be referencing the depth buffer.
        Application::Get().Flush();
 
        width = std::max(1, width);
        height = std::max(1, height);
 
        auto device = Application::Get().GetDevice();

        // Resize screen dependent resources.
        // Create a depth buffer.
        D3D12_CLEAR_VALUE optimizedClearValue = {};
        optimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
        optimizedClearValue.DepthStencil = { 1.0f, 0 };
 
		CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);
		CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, width, height, //此处是创建纹理 UpdateBufferResource中为缓冲区
			1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

        ThrowIfFailed(device->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &optimizedClearValue,
            IID_PPV_ARGS(&m_DepthBuffer)
        ));

        // 更新深度/模板视图
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
        dsv.Format = DXGI_FORMAT_D32_FLOAT;
        dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsv.Texture2D.MipSlice = 0;
        dsv.Flags = D3D12_DSV_FLAG_NONE;
 
        device->CreateDepthStencilView(m_DepthBuffer.Get(), &dsv,
            m_DSVHeap->GetCPUDescriptorHandleForHeapStart());
    }
}

void Demo1::OnResize(ResizeEventArgs& e)
{
    if (e.Width != GetClientWidth() || e.Height != GetClientHeight())
    {
        super::OnResize(e);
 
        m_Viewport = CD3DX12_VIEWPORT(0.0f, 0.0f,
            static_cast<float>(e.Width), static_cast<float>(e.Height));
 
        ResizeDepthBuffer(e.Width, e.Height);
    }
}

void Demo1::UnloadContent()
{
    m_ContentLoaded = false;
}

void Demo1::OnUpdate(UpdateEventArgs& e)
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
    
    // 更新 model matrix.
    float angle = static_cast<float>(e.TotalTime * 90.0);
    const XMVECTOR rotationAxis = XMVectorSet(0, 1, 1, 0);
    m_ModelMatrix = XMMatrixRotationAxis(rotationAxis, XMConvertToRadians(angle));
    
    // 更新 view matrix.
    const XMVECTOR eyePosition = XMVectorSet(0, 0, -10, 1);
    const XMVECTOR focusPoint = XMVectorSet(0, 0, 0, 1);
    const XMVECTOR upDirection = XMVectorSet(0, 1, 0, 0);
    m_ViewMatrix = XMMatrixLookAtLH(eyePosition, focusPoint, upDirection);

    // 更新 projection matrix.
    float aspectRatio = GetClientWidth() / static_cast<float>(GetClientHeight());
    m_ProjectionMatrix = XMMatrixPerspectiveFovLH(XMConvertToRadians(m_FoV), aspectRatio, 0.1f, 100.0f);
}

void Demo1::OnRender(RenderEventArgs& e)
{
    super::OnRender(e);

    //从应用类中获取命令队列和命令列表。 LoadContent中为COPY
    auto commandQueue = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    auto commandList = commandQueue->GetCommandList();
    
    UINT currentBackBufferIndex = m_pWindow->GetCurrentBackBufferIndex();
    auto backBuffer = m_pWindow->GetCurrentBackBuffer();
    auto rtv = m_pWindow->GetCurrentRenderTargetView();
    auto dsv = m_DSVHeap->GetCPUDescriptorHandleForHeapStart();

    // 清除渲染目标
    {
        TransitionResource(commandList, backBuffer,
            D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
 
        FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };
 
        ClearRTV(commandList, rtv, clearColor);
        ClearDepth(commandList, dsv);
    }

    // 准备渲染管线以进行渲染
    //尽管在 PSO 创建期间已经在 PSO 上设置了根签名（请参阅管道状态对象），
    //但在绑定任何资源（CBV、UAV、SRV 或采样器描述符）之前，
    //必须使用 ID3D12GraphicsCommandList::SetGraphicsRootSignature方法
    //在命令列表上显式设置根签名。未在命令列表上显式设置根签名将导致尝试绑定资源时出现运行时错误。
    commandList->SetPipelineState(m_PipelineState.Get());
    commandList->SetGraphicsRootSignature(m_RootSignature.Get());

    // IA Input Assembler 输入装配器
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); //该方法将顶点缓冲区绑定到输入装配器
    commandList->IASetVertexBuffers(0, 1, &m_VertexBufferView);
    commandList->IASetIndexBuffer(&m_IndexBufferView);
    // RS Rasterizer State 光栅化器状态
    commandList->RSSetViewports(1, &m_Viewport);
    commandList->RSSetScissorRects(1, &m_ScissorRect);
    // OM Output Merger 输出合并
    commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    // 更新 MVP matrix  （32 位常量传递到顶点着色器中的常量缓冲区）
    XMMATRIX mvpMatrix = XMMatrixMultiply(m_ModelMatrix, m_ViewMatrix);
    mvpMatrix = XMMatrixMultiply(mvpMatrix, m_ProjectionMatrix);
    commandList->SetGraphicsRoot32BitConstants(0, sizeof(XMMATRIX) / 4, &mvpMatrix, 0);

    // Draw Call
    commandList->DrawIndexedInstanced(_countof(g_Indicies), 1, 0, 0, 0);

    // Present
    {
        TransitionResource(commandList, backBuffer,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
 
        m_FenceValues[currentBackBufferIndex] = commandQueue->ExecuteCommandList(commandList);
 
        currentBackBufferIndex = m_pWindow->Present();
 
        commandQueue->WaitForFenceValue(m_FenceValues[currentBackBufferIndex]);
    }
}

// 转换资源
void Demo1::TransitionResource(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList,
    Microsoft::WRL::ComPtr<ID3D12Resource> resource, D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState)
{
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        resource.Get(),
        beforeState, afterState);
 
    commandList->ResourceBarrier(1, &barrier);
}

// Clear a render target.
void Demo1::ClearRTV(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList,
    D3D12_CPU_DESCRIPTOR_HANDLE rtv, FLOAT* clearColor)
{
    commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
}

void Demo1::ClearDepth(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList,
    D3D12_CPU_DESCRIPTOR_HANDLE dsv, FLOAT depth)
{
    commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, depth, 0, 0, nullptr);
}

void Demo1::OnKeyPressed(KeyEventArgs& e)
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
                m_pWindow->ToggleFullscreen();
            break;
        }
    case KeyCode::V:
        m_pWindow->ToggleVSync();
        break;
    }
}

void Demo1::OnMouseWheel(MouseWheelEventArgs& e)
{
    m_FoV -= e.WheelDelta;
    m_FoV = std::clamp(m_FoV, 12.0f, 90.0f);
 
    char buffer[256];
    sprintf_s(buffer, "FoV: %f\n", m_FoV);
    OutputDebugStringA(buffer);
}