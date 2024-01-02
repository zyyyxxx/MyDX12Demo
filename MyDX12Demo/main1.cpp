#pragma region inc

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h> // For CommandLineToArgvW

// The min/max macros conflict with like-named member functions.
// Only use std::min and std::max defined in <algorithm>.
#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif

// In order to define a function called CreateWindow, the Windows macro needs to
// be undefined.
#if defined(CreateWindow)
#undef CreateWindow
#endif

// Windows Runtime Library. Needed for Microsoft::WRL::ComPtr<> template class.
#include <wrl.h>
using namespace Microsoft::WRL;


// DirectX 12 specific headers.
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>

// D3D12 extension library.
#include <d3dx12.h>

// STL Headers
#include <algorithm>
#include <cassert>
#include <chrono>

// Helper functions
#include "Helpers.h"

#pragma endregion

#pragma region variables

// The number of swap chain back buffers.
// 交换链的后台缓冲区帧的数量
const uint8_t g_NumFrames = 3;
// Use WARP adapter 是否使用软件光栅化器
bool g_UseWarp = false;

uint32_t g_ClientWidth = 1280;
uint32_t g_ClientHeight = 720;

// Set to true once the DX12 objects have been initialized.
// 初始化标志
bool g_IsInitialized = false;

// Window handle. g_hWnd变量存储操作系统窗口的句柄，该窗口将用于显示渲染的图像。
HWND g_hWnd;
// Window rectangle (used to toggle full screen state). g_WindowRect变量用于存储进入全屏模式之前的先前窗口尺寸
RECT g_WindowRect;


// DirectX 12 Objects --------------------------------------------------
ComPtr<ID3D12Device2> g_Device; //DirectX 12 设备对象
ComPtr<ID3D12CommandQueue> g_CommandQueue; //命令队列
ComPtr<IDXGISwapChain4> g_SwapChain; //交换链
ComPtr<ID3D12Resource> g_BackBuffers[g_NumFrames]; //跟踪指向后台缓冲区资源的指针 所有缓冲区和纹理资源都是使用ID3D12ResourceDirectX 12 中的接口引用的。
ComPtr<ID3D12GraphicsCommandList> g_CommandList; //GPU 命令 ，存储指向ID3D12GraphicsCommandList的指针。
ComPtr<ID3D12CommandAllocator> g_CommandAllocators[g_NumFrames];//ID3D12CommandAllocator用作将 GPU 命令记录到命令列表中的后备存储器

// 存储包含交换链后台缓冲区的渲染目标视图的描述符堆 
// render target view (RTV) 描述了纹理资源在GPU内存中的位置、纹理的尺寸（宽度和高度）以及纹理的格式
ComPtr<ID3D12DescriptorHeap> g_RTVDescriptorHeap;

UINT g_RTVDescriptorSize; //单个 RTV 描述符的大小
UINT g_CurrentBackBufferIndex; //存储交换链当前后台缓冲区的索引
//-----------------------------------------------------------------------

// Synchronization objects GPU同步变量
ComPtr<ID3D12Fence> g_Fence;
uint64_t g_FenceValue = 0; //向命令队列发出信号的下一个栅栏值存储在该g_FenceValue变量中
uint64_t g_FrameFenceValues[g_NumFrames] = {}; //数组g_FrameFenceValues变量用于跟踪用于向特定帧的命令队列发出信号的栅栏值。
HANDLE g_FenceEvent; //g_FenceEvent变量是操作系统事件对象的句柄，用于接收栅栏已达到特定值的通知。

//控制当前交换链方法。
// By default, enable V-Sync.
// Can be toggled with the V key.
bool g_VSync = true;
bool g_TearingSupported = false;
// By default, use windowed mode.
// Can be toggled with the Alt+Enter or F11
bool g_Fullscreen = false;

#pragma endregion

// Window callback function. 窗口回调函数
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

//执行应用程序时通过提供命令行参数来覆盖一些全局定义的变量。
/*
    -w,--width	 指定渲染窗口的宽度（以像素为单位）。
    -h,--height	 指定渲染窗口的高度（以像素为单位）。
    -warp,--warp 使用 Windows 高级光栅化平台 (WARP) 进行设备创建
 */
void ParseCommandLineArguments()
{
    int argc;
    wchar_t** argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
 
    for (size_t i = 0; i < argc; ++i)
    {
        if (::wcscmp(argv[i], L"-w") == 0 || ::wcscmp(argv[i], L"--width") == 0)
        {
            g_ClientWidth = ::wcstol(argv[++i], nullptr, 10);
        }
        if (::wcscmp(argv[i], L"-h") == 0 || ::wcscmp(argv[i], L"--height") == 0)
        {
            g_ClientHeight = ::wcstol(argv[++i], nullptr, 10);
        }
        if (::wcscmp(argv[i], L"-warp") == 0 || ::wcscmp(argv[i], L"--warp") == 0)
        {
            g_UseWarp = true;
        }
    }
 
    // Free memory allocated by CommandLineToArgvW
    ::LocalFree(argv);
}

//启用 Direct3D 12 调试层
void EnableDebugLayer()
{
#if defined(_DEBUG)
    //在执行任何与 DX12 相关的操作之前，请始终启用调试层，以便调试层 捕获创建 DX12 对象时生成的所有可能错误。
    ComPtr<ID3D12Debug> debugInterface;
    //IID_PPV_ARGS用于检索接口指针，根据所使用的接口指针的类型自动提供所请求接口的 IID 值。
    //检索接口指针的方法中的常见语法包括两个参数：
    //一个 [in] 参数，通常为 类型REFIID，用于指定要检索的接口的 IID。
    //一个 [out] 参数，通常为 类型void**，用于接收接口指针。
    //该宏根据接口指针的类型计算IID，这可以防止IID和接口指针类型不匹配的编码错误。
    //Windows 开发人员应始终将此宏与需要单独 IID 和接口指针参数的任何方法一起使用。
    ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface)));
    debugInterface->EnableDebugLayer();
#endif
}

//注册窗口类
void RegisterWindowClass( HINSTANCE hInst, const wchar_t* windowClassName )
{
    // 注册一个窗口类来创建我们的渲染窗口。
    WNDCLASSEXW windowClass = {};

    ////struct大小
    windowClass.cbSize = sizeof(WNDCLASSEX);
    
    //CS_HREDRAW类样式指定如果移动或大小调整更改了客户端区域的宽度，则重新CS_VREDRAW绘制整个窗口，并且类样式指定如果移动或大小调整更改了客户端的高度，则重新绘制整个窗口区域。
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    
    //指向窗口过程的指针，该窗口过程将处理使用此窗口类创建的任何窗口的窗口消息。WndProc在本例中，我们指定之前声明的尚未定义的函数。
    windowClass.lpfnWndProc = &WndProc;
    
    //在窗口类结构之后分配的额外字节数。这里不使用该参数，应设置为0。
    windowClass.cbClsExtra = 0;
    
    //在窗口实例之后分配的额外字节数。这里不使用该参数，应设置为0。
    windowClass.cbWndExtra = 0;
    //包含该类的窗口过程的实例的句柄。该模块实例句柄被传递给WinMain稍后将显示的函数。
    windowClass.hInstance = hInst;
    
    //类图标的句柄。该图标将用于在任务栏和窗口标题栏的左上角表示使用此类创建的窗口。
    //您可以使用该函数从资源文件加载图标LoadIcon。如果该值为NULL（或nullptr），则使用默认应用程序图标。
    windowClass.hIcon = ::LoadIcon(hInst, NULL);

    //类光标的句柄。这必须是有效游标资源的句柄。对于此演示，我们将通过指定 来使用默认箭头图标LoadCursor( nullptr, IDC_ARROW )。
    windowClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);

    //类背景画笔的句柄。该成员可以是用于绘制背景的画笔的句柄，也可以是颜色值。
    //颜色值必须是以下标准系统颜色之一（必须将值 1 添加到所选颜色）。如果给定了颜色值，则必须将其转换为以下HBRUSH类型之一：
    windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    
    //指向以 null 结尾的字符串的指针，该字符串指定类菜单的资源名称，该名称出现在资源文件中。如果该成员为NULL，则属于该类的窗口没有默认菜单。
    windowClass.lpszMenuName = NULL;
    windowClass.lpszClassName = windowClassName;
    windowClass.hIconSm = ::LoadIcon(hInst, NULL);
 
    static ATOM atom = ::RegisterClassExW(&windowClass);
    assert(atom > 0);
}


//创建操作系统窗口的实例
HWND CreateWindow(const wchar_t* windowClassName, HINSTANCE hInst,
    const wchar_t* windowTitle, uint32_t width, uint32_t height)
{
    int screenWidth = ::GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = ::GetSystemMetrics(SM_CYSCREEN);
 
    RECT windowRect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
    ::AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);
 
    int windowWidth = windowRect.right - windowRect.left;
    int windowHeight = windowRect.bottom - windowRect.top;
 
    // Center the window within the screen. Clamp to 0, 0 for the top-left corner.
    int windowX = std::max<int>(0, (screenWidth - windowWidth) / 2);
    int windowY = std::max<int>(0, (screenHeight - windowHeight) / 2);

    // 创建窗口实例
    /*
        HWND WINAPI CreateWindowExW(
        _In_ DWORD dwExStyle,
        _In_opt_ LPCWSTR lpClassName,
        _In_opt_ LPCWSTR lpWindowName,
        _In_ DWORD dwStyle,
        _In_ int X,
        _In_ int Y,
        _In_ int nWidth,
        _In_ int nHeight,
        _In_opt_ HWND hWndParent,
        _In_opt_ HMENU hMenu,
        _In_opt_ HINSTANCE hInstance,
        _In_opt_ LPVOID lpParam
        );
    */
    HWND hWnd = ::CreateWindowExW(
        NULL,
        windowClassName,
        windowTitle,
        WS_OVERLAPPEDWINDOW,
        windowX,
        windowY,
        windowWidth,
        windowHeight,
        NULL,
        NULL,
        hInst,
        nullptr
    );
 
    assert(hWnd && "Failed to create window");
 
    return hWnd;
}

//查询 DirectX 12 适配器
ComPtr<IDXGIAdapter4> GetAdapter(bool useWarp)
{
    //在查询可用适配器之前，必须创建 DXGI 工厂
    ComPtr<IDXGIFactory4> dxgiFactory;
    UINT createFactoryFlags = 0;
#if defined(_DEBUG)
    createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif
 
    ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));
    
    ComPtr<IDXGIAdapter1> dxgiAdapter1;
    ComPtr<IDXGIAdapter4> dxgiAdapter4;
 
    if (useWarp)
    {
        //在需要使用WARP设备的情况下，IDXGIFactory4::EnumWarpAdapter可以使用该方法直接创建WARP适配器。
        //IDXGIFactory4::EnumWarpAdapter方法接受一个指向IDXGIAdapter1接口的指针，但该GetAdapter函数返回一个指向IDXGIAdapter4接口的指针。
        //为了将 COM 对象转换为正确的类型，应使用ComPtr::As
        ThrowIfFailed(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&dxgiAdapter1)));
        ThrowIfFailed(dxgiAdapter1.As(&dxgiAdapter4));
    }else
    {
        //当不使用 WARP 适配器时，DXGI Factory 用于查询硬件适配器
        //IDXGIFactory1::EnumAdapters1方法用于枚举系统中可用的GPU适配器
        SIZE_T maxDedicatedVideoMemory = 0;
        for (UINT i = 0; dxgiFactory->EnumAdapters1(i, &dxgiAdapter1) != DXGI_ERROR_NOT_FOUND; ++i)
        {
            DXGI_ADAPTER_DESC1 dxgiAdapterDesc1;
            dxgiAdapter1->GetDesc1(&dxgiAdapterDesc1);
            
            //检查适配器是否可以创建 D3D12 设备而无需实际创建它。 具有最大显存（不与 CPU 共享）的适配器优先选择。
            if ((dxgiAdapterDesc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
                //D3D12CreateDevice函数创建一个（空）设备。如果此函数返回S_OK，则该函数成功并且它是 DirectX 12 兼容适配器。
                SUCCEEDED(D3D12CreateDevice(dxgiAdapter1.Get(), 
                    D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)) && 
                dxgiAdapterDesc1.DedicatedVideoMemory > maxDedicatedVideoMemory )
            {
                maxDedicatedVideoMemory = dxgiAdapterDesc1.DedicatedVideoMemory;
                ThrowIfFailed(dxgiAdapter1.As(&dxgiAdapter4));
            }
        }
    }
 
    return dxgiAdapter4;
}

//创建 DirectX 12 设备
ComPtr<ID3D12Device2> CreateDevice(ComPtr<IDXGIAdapter4> adapter)
{
    ComPtr<ID3D12Device2> d3d12Device2;
    /*
        HRESULT WINAPI D3D12CreateDevice(
            _In_opt_  IUnknown          *pAdapter,
            D3D_FEATURE_LEVEL MinimumFeatureLevel, 成功创建设备所需的最低要求。
            _In_      REFIID            riid,      设备接口的全局唯一标识符 (GUID)。 该参数和 ppDevice 可以使用单个宏 IID_PPV_ARGS 来寻址。
            _Out_opt_ void              **ppDevice 指向接收设备指针的内存块的指针
        );
    */
    ThrowIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d12Device2)));

    // Enable debug messages in debug mode.
#if defined(_DEBUG)
    ComPtr<ID3D12InfoQueue> pInfoQueue;
    if (SUCCEEDED(d3d12Device2.As(&pInfoQueue)))
    {
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);

        // Suppress whole categories of messages
        //D3D12_MESSAGE_CATEGORY Categories[] = {};
 
        // Suppress messages based on their severity level
        D3D12_MESSAGE_SEVERITY Severities[] =
        {
            D3D12_MESSAGE_SEVERITY_INFO
        };
 
        // Suppress individual messages by their ID
        D3D12_MESSAGE_ID DenyIds[] = {
            D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,   // I'm really not sure how to avoid this message.
            D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,                         // This warning occurs when using capture frame while graphics debugging.
            D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,                       // This warning occurs when using capture frame while graphics debugging.
        };
 
        D3D12_INFO_QUEUE_FILTER NewFilter = {};
        //NewFilter.DenyList.NumCategories = _countof(Categories);
        //NewFilter.DenyList.pCategoryList = Categories;
        NewFilter.DenyList.NumSeverities = _countof(Severities);
        NewFilter.DenyList.pSeverityList = Severities;
        NewFilter.DenyList.NumIDs = _countof(DenyIds);
        NewFilter.DenyList.pIDList = DenyIds;
 
        ThrowIfFailed(pInfoQueue->PushStorageFilter(&NewFilter));
    }
#endif
 
    return d3d12Device2;
}

//创建命令队列
ComPtr<ID3D12CommandQueue> CreateCommandQueue(ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type )
{
    ComPtr<ID3D12CommandQueue> d3d12CommandQueue;
    D3D12_COMMAND_QUEUE_DESC desc = {};
    /* 指定要创建的命令队列的类型，可以是以下类型之一：
   D3D12_COMMAND_LIST_TYPE_DIRECT：命令队列可用于执行绘制、计算和复制命令。这是最通用的命令队列类型，在大多数情况下都会使用。
   D3D12_COMMAND_LIST_TYPE_COMPUTE：命令队列可用于执行计算和复制命令。
   D3D12_COMMAND_LIST_TYPE_COPY：命令队列可用于执行复制命令。*/
    desc.Type =     type;
    /*INT Priority：命令队列的优先级。可以是以下值之一：
D3D12_COMMAND_QUEUE_PRIORITY_NORMAL：命令队列具有普通优先级。
D3D12_COMMAND_QUEUE_PRIORITY_HIGH：命令队列具有高优先级。
D3D12_COMMAND_QUEUE_PRIORITY_GLOBAL_REALTIME：命令队列具有全局实时优先级。*/
    desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    desc.Flags =    D3D12_COMMAND_QUEUE_FLAG_NONE;// 枚举中的附加标志
    desc.NodeMask = 0;//对于单 GPU 操作，请将其设置为零
 
    ThrowIfFailed(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&d3d12CommandQueue)));
 
    return d3d12CommandQueue;
}

//检查防撕裂支持
bool CheckTearingSupport()
{
    BOOL allowTearing = FALSE;
 
    // Rather than create the DXGI 1.5 factory interface directly, we create the
    // DXGI 1.4 interface and query for the 1.5 interface. This is to enable the 
    // graphics debugging tools which will not support the 1.5 factory interface 
    // until a future update.
    ComPtr<IDXGIFactory4> factory4;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory4))))
    {
        ComPtr<IDXGIFactory5> factory5;
        if (SUCCEEDED(factory4.As(&factory5)))
        {
            if (FAILED(factory5->CheckFeatureSupport(
                DXGI_FEATURE_PRESENT_ALLOW_TEARING, 
                &allowTearing, sizeof(allowTearing))))
            {
                allowTearing = FALSE;
            }
        }
    }
 
    return allowTearing == TRUE;
}

//创建交换链
ComPtr<IDXGISwapChain4> CreateSwapChain(HWND hWnd, 
    ComPtr<ID3D12CommandQueue> commandQueue, 
    uint32_t width, uint32_t height, uint32_t bufferCount )
{
    ComPtr<IDXGISwapChain4> dxgiSwapChain4;
    ComPtr<IDXGIFactory4> dxgiFactory4;
    UINT createFactoryFlags = 0;
    
#if defined(_DEBUG)
    createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif
 
    ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory4)));
    //描述交换链是如何创建的结构体
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = width;
    swapChainDesc.Height = height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.Stereo = FALSE;
    swapChainDesc.SampleDesc = { 1, 0 };//描述多重采样参数的结构体 使用翻转模型交换链时，该成员必须指定为{1, 0}
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;//描述后台缓冲区的表面使用情况和 CPU 访问选项。后台缓冲区可用于着色器输入 ( DXGI_USAGE_SHADER_INPUT) 或渲染目标输出 ( DXGI_USAGE_RENDER_TARGET_OUTPUT)。
    swapChainDesc.BufferCount = bufferCount;//描述交换链中缓冲区数量的值。创建全屏交换链时，通常在此值中包含前端缓冲区。当使用翻转呈现模型时，缓冲区的最小数量是两个。
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;//于标识后台缓冲区大小不等于目标输出时的调整大小行为
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;//描述交换链使用的表示模型以及在呈现表面后处理呈现缓冲区内容的选项。
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;//于标识交换链后台缓冲区的透明度行为
    // It is recommended to always allow tearing if tearing support is available.
    //使用按位 OR 运算组合的 DXGI_SWAP_CHAIN_FLAG 类型值的组合。 如果撕裂支持可用，则应始终指定 DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING 标志。
    swapChainDesc.Flags = CheckTearingSupport() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    // 创建交换链
    ComPtr<IDXGISwapChain1> swapChain1;
    ThrowIfFailed(dxgiFactory4->CreateSwapChainForHwnd(
        commandQueue.Get(),//指向直接命令队列的指针
        hWnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain1));
 
    // Disable the Alt+Enter fullscreen toggle feature. Switching to fullscreen
    // will be handled manually.
    ThrowIfFailed(dxgiFactory4->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER));
 
    ThrowIfFailed(swapChain1.As(&dxgiSwapChain4));
 
    return dxgiSwapChain4;
}

//创建描述符堆
/*从 DirectX 12 开始，可以创建资源描述符（例如渲染目标视图( RTV )、着色器资源视图( SRV )、无序访问视图( UAV ) 或常量缓冲区视图( CBV )))，需要创建描述符堆。
可以在同一堆中创建某些类型的资源视图（描述符）。例如，CBV、SRV 和 UAV 可以存储在同一堆中，但 RTV 和 Sampler 视图各自需要单独的描述符堆。*/
ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(ComPtr<ID3D12Device2> device,
    D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors)
{
    ComPtr<ID3D12DescriptorHeap> descriptorHeap;
 
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors = numDescriptors;
    desc.Type = type;
    //Type 成员可以具有以下值之一：
    //D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV：常量缓冲区、着色器资源和无序访问视图组合的描述符堆。
    //D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER：采样器的描述符堆。
    //D3D12_DESCRIPTOR_HEAP_TYPE_RTV：渲染目标视图的描述符堆。
    //D3D12_DESCRIPTOR_HEAP_TYPE_DSV：深度模板视图的描述符堆。
 
    ThrowIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap)));
 
    return descriptorHeap;
}

//创建渲染目标视图 (RTV) 
/*(RTV) 描述了可以附加到输出合并阶段的绑定槽的资源
渲染目标视图描述接收像素着色器阶段计算的最终颜色的资源*/
void UpdateRenderTargetViews(ComPtr<ID3D12Device2> device,
    ComPtr<IDXGISwapChain4> swapChain, ComPtr<ID3D12DescriptorHeap> descriptorHeap)
{
    //查询单个描述符的大小
    auto rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    //为了迭代描述符堆中的描述符 GetCPUDescriptorHandleForHeapStart返回D3D12_CPU_DESCRIPTOR_HANDLE：指向描述符堆中描述符的指针的别名
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(descriptorHeap->GetCPUDescriptorHandleForHeapStart());
 
    for (int i = 0; i < g_NumFrames; ++i)
    {
        ComPtr<ID3D12Resource> backBuffer;
        //查询指向交换链后台缓冲区的指针
        ThrowIfFailed(swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));
        //创建 RTV
        //第一个参数是指向包含渲染目标纹理的资源的指针。
        //第二个参数是指向结构的指针D3D12_RENDER_TARGET_VIEW_DESC。描述NULL用于创建资源的默认描述符
        //第三个参数ID3D12Device::CreateRenderTargetView是放置视图的描述符的句柄。
        device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);
 
        g_BackBuffers[i] = backBuffer;
        //使用结构体的方法将描述符句柄递增到描述符堆中的下一个句柄
        rtvHandle.Offset(rtvDescriptorSize);
    }
}

//创建命令分配器
/*
 * 命令分配器是命令列表使用的后备存储器
 * 须指定分配器将使用的命令列表的类型。
 * 命令分配器不提供任何功能，只能通过命令列表间接访问。
 * 命令分配器一次只能由单个命令列表使用，但可以在记录到命令列表中的命令在 GPU 上完成执行后重新使用。
 */
ComPtr<ID3D12CommandAllocator> CreateCommandAllocator(ComPtr<ID3D12Device2> device,
    D3D12_COMMAND_LIST_TYPE type)
{
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    ThrowIfFailed(device->CreateCommandAllocator(type, IID_PPV_ARGS(&commandAllocator)));
 
    return commandAllocator;
}

//创建命令列表
/*
 * 命令列表用于记录在 GPU 上执行的命令。
 * 与以前版本的 DirectX 不同，记录到命令列表中的命令的执行始终会延迟。
 * 也就是说，在命令列表被发送到命令队列之前，不会执行对命令列表调用绘制或调度命令。
 * 与命令分配器不同，命令列表在命令队列上执行后可以立即重用。唯一的限制是在记录任何新命令之前必须先重置命令列表。
 */
ComPtr<ID3D12GraphicsCommandList> CreateCommandList(ComPtr<ID3D12Device2> device,
    ComPtr<ID3D12CommandAllocator> commandAllocator, D3D12_COMMAND_LIST_TYPE type)
{
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ThrowIfFailed(device->CreateCommandList(0, type, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));
    
    ThrowIfFailed(commandList->Close());
    //命令列表是在记录状态下创建的。
    //为了保持一致性，渲染循环中对命令列表执行的第一个操作（稍后将显示）是ID3D12GraphicsCommandList::Reset.
    //在重置命令列表之前，必须先将其关闭。关闭命令列表，以便可以在渲染循环中记录命令之前重置它。
    return commandList;
}

//创建栅栏Fence
/*
 *  Fence是GPU/CPU 同步对象的接口。栅栏可用于在 CPU 或 GPU 上执行同步。
    在内部，栅栏存储单个 64 位无符号整数值。栅栏的初始值是在创建栅栏时指定的。
    使用该方法在 CPU 上更新栅栏的内部值ID3D12Fence::Signal，并使用该方法在 GPU 上更新栅栏的内部值ID3D12CommandQueue::Signal。
    要等待Fence达到 CPU 上的特定值，请使用该ID3D12Fence::SetEventOnCompletion方法，然后调用该WaitForSingleObject函数。
    要等待栅栏在 GPU 上达到特定值，请使用ID3D12CommandQueue::Wait方法。
 */
ComPtr<ID3D12Fence> CreateFence(ComPtr<ID3D12Device2> device)
{
    ComPtr<ID3D12Fence> fence;
 
    ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
 
    return fence;
}

//创建Event
/*
 *如果栅栏尚未收到特定值的信号，则 CPU 线程将需要阻止任何进一步的处理，直到栅栏收到该值的信号。
 *操作系统事件句柄用于阻塞 CPU 线程，直到栅栏收到信号为止。接下来描述的函数CreateEventHandle用于创建操作系统事件。
*/
HANDLE CreateEventHandle()
{
    HANDLE fenceEvent;
    
    fenceEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
    //BOOL bInitialState 如果该参数为TRUE，则事件对象的初始状态为有信号；否则，它是无信号的。
    assert(fenceEvent && "Failed to create fence event.");
 
    return fenceEvent;
}

//Fence 信号
/*
    该Signal函数用于从 GPU 发出Fence信号。
    应该注意的是，当使用该ID3D12CommandQueue::Signal方法从 GPU 发出栅栏信号时，栅栏不会立即发出信号，而是仅在 GPU 命令队列在执行期间到达该点时才发出信号。
    在调用信号方法之前排队的任何命令都必须在栅栏发出信号之前完成执行。
    当命令队列中排队的所有命令完成执行后，栅栏将收到信号。
    该Signal函数返回 CPU 线程在重用 GPU 上该帧“正在运行”的任何资源之前应等待的栅栏值。
*/
uint64_t Signal(ComPtr<ID3D12CommandQueue> commandQueue, ComPtr<ID3D12Fence> fence,
    uint64_t& fenceValue)
{
    uint64_t fenceValueForSignal = ++fenceValue;
    ThrowIfFailed(commandQueue->Signal(fence.Get(), fenceValueForSignal));
 
    return fenceValueForSignal;
}

//等待Fence值
/*
 *CPU 线程可能需要暂停以等待 GPU 队列完成执行写入资源的命令，然后再重新使用。
 *例如，在重用交换链的后台缓冲区资源之前，使用该资源作为渲染目标的任何命令都必须先完成，然后才能重用该后台缓冲区资源。
 *任何从未用作可写目标的资源（例如材质纹理）不需要双缓冲，也不需要在着色器中作为只读资源重用之前停止 CPU 线程。
 *可写资源（例如渲染目标）确实需要同步，以保护资源不被多个队列同时修改。
 */
void WaitForFenceValue(ComPtr<ID3D12Fence> fence, uint64_t fenceValue, HANDLE fenceEvent,
    std::chrono::milliseconds duration = std::chrono::milliseconds::max() )
{
    //WaitForFenceValue如果栅栏尚未达到（已收到信号）特定值，则用于停止 CPU 线程。该函数将等待duration参数指定的持续时间，默认为584million years
    //如果栅栏尚未达到该值，则向栅栏注册一个事件对象，并在栅栏达到指定值时依次发出信号。
    if (fence->GetCompletedValue() < fenceValue)
    {
        ThrowIfFailed(fence->SetEventOnCompletion(fenceValue, fenceEvent));
        ::WaitForSingleObject(fenceEvent, static_cast<DWORD>(duration.count()));
    }
}

//刷新GPU
/*
 *该Flush函数用于确保之前在 GPU 上执行的任何命令都已完成执行，然后才允许 CPU 线程继续处理。
 *这对于确保当前在 GPU 上“运行中”的命令引用的任何后台缓冲区资源在调整大小之前已完成执行非常有用。
 *强烈建议在释放命令队列上当前“运行中”的命令列表可能引用的任何资源之前（例如，在关闭应用程序之前）刷新 GPU 命令队列。
 *函数内容：The Flush function is simply a Signal followed by a WaitForFenceValue.
 */
void Flush(ComPtr<ID3D12CommandQueue> commandQueue, ComPtr<ID3D12Fence> fence,
    uint64_t& fenceValue, HANDLE fenceEvent )
{
    uint64_t fenceValueForSignal = Signal(commandQueue, fence, fenceValue);
    WaitForFenceValue(fence, fenceValueForSignal, fenceEvent);
}

//Update
void Update()
{
    static uint64_t frameCounter = 0;
    static double elapsedSeconds = 0.0;
    static std::chrono::high_resolution_clock clock;
    static auto t0 = clock.now();
 
    frameCounter++;
    auto t1 = clock.now();
    auto deltaTime = t1 - t0;
    t0 = t1;
    elapsedSeconds += deltaTime.count() * 1e-9;
    if (elapsedSeconds > 1.0)
    {
        char buffer[500];
        auto fps = frameCounter / elapsedSeconds;
        sprintf_s(buffer, 500, "FPS: %f\n", fps);
        OutputDebugString(buffer);
        
        frameCounter = 0;
        elapsedSeconds = 0.0;
    }
}

//Render
/*
 *1.清除后台缓冲区
 *2.呈现渲染帧
 */
void Render()
{
    //根据当前后台缓冲区索引 检索指向命令分配器和后台缓冲区资源的指针。
    auto commandAllocator = g_CommandAllocators[g_CurrentBackBufferIndex];
    auto backBuffer = g_BackBuffers[g_CurrentBackBufferIndex];

    //重置命令分配器和命令列表。这会准备用于记录下一帧的命令列表
    commandAllocator->Reset();
    g_CommandList->Reset(commandAllocator.Get(), nullptr);

    // 清除渲染目标
    {
        //在清除渲染目标之前，必须将其转换到该RENDER_TARGET状态
        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            backBuffer.Get(),
            D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        g_CommandList->ResourceBarrier(1, &barrier);
        //清除后台缓冲区
        FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(g_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
            g_CurrentBackBufferIndex, g_RTVDescriptorSize);
 
        g_CommandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    }

    // 展示
    {
        // 资源转换
        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            backBuffer.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        g_CommandList->ResourceBarrier(1, &barrier);
        
        //ID3D12GraphicsCommandList::Close关闭命令列表。该方法必须先在命令列表上调用，然后才能在命令队列上执行。
        ThrowIfFailed(g_CommandList->Close());
        
        ID3D12CommandList* const commandLists[] = {
            g_CommandList.Get()
        };//_countof，Windows宏，用来计算一个静态分配的数组中的元素的个数
        g_CommandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);
    
        //将交换链的当前后台缓冲区呈现到屏幕上 IDXGISwapChain::Present
        UINT syncInterval = g_VSync ? 1 : 0;
        UINT presentFlags = g_TearingSupported && !g_VSync ? DXGI_PRESENT_ALLOW_TEARING : 0;
        ThrowIfFailed(g_SwapChain->Present(syncInterval, presentFlags));
 
        g_FrameFenceValues[g_CurrentBackBufferIndex] = Signal(g_CommandQueue, g_Fence, g_FenceValue);

        //使用DXGI_SWAP_EFFECT_FLIP_DISCARD翻转模型时，不保证后台缓冲区索引的顺序是连续的。
        //该IDXGISwapChain3::GetCurrentBackBufferIndex方法用于获取交换链当前后台缓冲区的索引。
        g_CurrentBackBufferIndex = g_SwapChain->GetCurrentBackBufferIndex();
        //在用下一帧的内容覆盖当前后台缓冲区的内容之前，使用前面描述的WaitForFenceValue函数停止 CPU 线程
        WaitForFenceValue(g_Fence, g_FrameFenceValues[g_CurrentBackBufferIndex], g_FenceEvent);
    }
}

// 调整大小
void Resize(uint32_t width, uint32_t height)
{
    if (g_ClientWidth != width || g_ClientHeight != height)
    {
        // Don't allow 0 size swap chain back buffers.
        g_ClientWidth = std::max(1u, width );
        g_ClientHeight = std::max( 1u, height);
 
        // Flush the GPU queue to make sure the swap chain's back buffers
        // are not being referenced by an in-flight command list.
        // 刷新 GPU 队列，以确保交换链的后台缓冲区未被正在进行的命令列表引用。
        // 由于 GPU 上可能有一个“运行中”的命令列表引用交换链的后台缓冲区，因此需要使用前面描述的函数 Flush 刷新 GPU 。
        Flush(g_CommandQueue, g_Fence, g_FenceValue, g_FenceEvent);
        
        for (int i = 0; i < g_NumFrames; ++i)
        {
            // Any references to the back buffers must be released
            // before the swap chain can be resized.
            // 必须先释放对后台缓冲区的任何引用，然后才能调整交换链的大小。
            // 释放对交换链后台缓冲区的本地引用。每帧栅栏值也重置为当前后台缓冲区索引的栅栏值。
            g_BackBuffers[i].Reset();
            g_FrameFenceValues[i] = g_FrameFenceValues[g_CurrentBackBufferIndex];
        }
        // 查询当前交换链描述符，以便使用相同的颜色格式和交换链标志 重新创建交换链缓冲区。
        // 由于后台缓冲区的索引可能不相同，因此更新应用程序已知的当前后台缓冲区索引非常重要。
        DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
        ThrowIfFailed(g_SwapChain->GetDesc(&swapChainDesc));
        ThrowIfFailed(g_SwapChain->ResizeBuffers(g_NumFrames, g_ClientWidth, g_ClientHeight,
            swapChainDesc.BufferDesc.Format, swapChainDesc.Flags));
 
        g_CurrentBackBufferIndex = g_SwapChain->GetCurrentBackBufferIndex();

        //调整交换链缓冲区大小后，需要更新引用这些缓冲区的描述符。使用先前描述的方法 UpdateRenderTargetViews 更新 RTV 描述符。
        UpdateRenderTargetViews(g_Device, g_SwapChain, g_RTVDescriptorHeap);
    }
}

// 设置全屏
/*
    *为了解决全屏独占模式的问题，将使用全屏无边框窗口（FSBW）来最大化窗口。

    使用IDXGISwapChain::SetFullscreenState将后台缓冲区切换到全屏独占模式可能很麻烦，并且具有以下缺点：
    创建交换链时需要一个DXGI_SWAP_CHAIN_FULLSCREEN_DESC结构来切换到全屏状态。
    分辨率和刷新率必须与显示器支持的模式之一匹配。提供不正确的分辨率或刷新率设置可能会导致最终用户黑屏。
    切换到全屏独占模式可能会导致多显示器设置中的任何其他显示器变黑。
    鼠标光标锁定为全屏显示。
    如果正在渲染的 GPU 不是直接连接到显示设备，则切换到全屏状态将会失败。这在多 GPU 配置中很常见（例如具有集成 Intel 图形芯片和专用 GPU 的笔记本电脑）。
 */
void SetFullscreen(bool fullscreen)
{
    if (g_Fullscreen != fullscreen)
    {
        g_Fullscreen = fullscreen;
 
        if (g_Fullscreen) // Switching to fullscreen.
            {
            // GetWindowRect函数保存窗口矩形用于恢复
            ::GetWindowRect(g_hWnd, &g_WindowRect);

            // 将窗口样式更改为无边框窗口。SetWindowLong函数用于设置无边框窗口样式
            // Set the window style to a borderless window so the client area fills
            // the entire screen.
            UINT windowStyle = WS_OVERLAPPEDWINDOW & ~(WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
            ::SetWindowLongW(g_hWnd, GWL_STYLE, windowStyle);
        
            // Query the name of the nearest display device for the window.
            // This is required to set the fullscreen dimensions of the window
            // when using a multi-monitor setup.    
            //查询距离应用程序窗口最近的显示器的尺寸
            //查询监视器的属性GetMonitorInfo。从函数返回的结构GetMonitorInfo包含一个矩形结构，用于描述监视器的全屏矩形
            HMONITOR hMonitor = ::MonitorFromWindow(g_hWnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFOEX monitorInfo = {};
            monitorInfo.cbSize = sizeof(MONITORINFOEX);
            ::GetMonitorInfo(hMonitor, &monitorInfo);

            ::SetWindowPos(g_hWnd, HWND_TOP,
               monitorInfo.rcMonitor.left,
               monitorInfo.rcMonitor.top,
               monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
               monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
               SWP_FRAMECHANGED | SWP_NOACTIVATE);
 
            ::ShowWindow(g_hWnd, SW_MAXIMIZE);
            }else{
            // Restore all the window decorators.
            ::SetWindowLong(g_hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);
 
            ::SetWindowPos(g_hWnd, HWND_NOTOPMOST,
                g_WindowRect.left,
                g_WindowRect.top,
                g_WindowRect.right - g_WindowRect.left,
                g_WindowRect.bottom - g_WindowRect.top,
                SWP_FRAMECHANGED | SWP_NOACTIVATE);
 
            ::ShowWindow(g_hWnd, SW_NORMAL);
        }
    }
}


//窗口消息处理
/*
V Toggle V-Sync.
Esc	Exit the application.
Alt+Enter, F11	Toggle fullscreen mode.
 */
LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if ( g_IsInitialized )
    {
        switch (message)
        {
            
        case WM_PAINT: //重新绘制应用程序窗口
            Update();
            Render();
            break;
        //当按住 Alt 键同时按另一个组合键（例如，Alt+Enter）时，WM_SYSKEYDOWN消息将被发送到窗口处理函数。
            
        //当按下任何非系统键时，将发送WM_KEYDOWN消息（按下某个键而不按住 Alt ）。
        case WM_SYSKEYDOWN:
            
        case WM_KEYDOWN:
            {
                bool alt = (::GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
 
                switch (wParam)
                {
                case 'V':
                    g_VSync = !g_VSync;
                    break;
                case VK_ESCAPE:
                    ::PostQuitMessage(0);
                    break;
                case VK_RETURN:
                    if ( alt )
                    {
                        case VK_F11:
                            SetFullscreen(!g_Fullscreen);
                    }
                    break;
                }
            }
            break;
            // 如果未处理此消息，则默认窗口过程将在按 Alt+Enter 键盘组合时播放系统通知声音。
        case WM_SYSCHAR:
            break;
            
        case WM_SIZE:
            {
                //The client area of the window
                //客户端矩形用于计算宽度和高度以调整交换链缓冲区的大小。
                RECT clientRect = {};
                ::GetClientRect(g_hWnd, &clientRect);
 
                int width = clientRect.right - clientRect.left;
                int height = clientRect.bottom - clientRect.top;
 
                Resize(width, height);
            }
            break;
        //右上角叉号
        case WM_DESTROY:
            ::PostQuitMessage(0);
            break;
            
        default:
            return ::DefWindowProcW(hwnd, message, wParam, lParam);
        }
    }
    else
    {
        return ::DefWindowProcW(hwnd, message, wParam, lParam);
    }
 
    return 0;
}

//Main Entry Point
int CALLBACK wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow)
{
    //Windows 10 更新添加了每监视器 V2 DPI 感知上下文。
    //使用这种 DPI 感知模式，应用程序能够实现窗口客户区域的 100% 像素缩放，同时仍然允许非客户区域（例如标题栏和菜单）进行 DPI 缩放
    //例如，如果您有 4K UHD（3840×2160 或 2160p）显示器，并且已将 DPI 缩放配置为 150%，则默认行为是将客户区域大小设置为 2560×1440。
    //在创建窗口之前指定应用程序的 DPI 感知可以修复此问题，同时仍然允许在非客户端区域进行 DPI 缩放（例如，窗口的标题栏仍将根据 DPI 设置进行缩放）。
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 
    // Window class name. Used for registering / creating the window.
    const wchar_t* windowClassName = L"DX12WindowClass";
    ParseCommandLineArguments();//解析命令行参数

    EnableDebugLayer();
    
    //查询应用程序的tearing支持
    g_TearingSupported = CheckTearingSupport();
    //注册创建窗口
    RegisterWindowClass(hInstance, windowClassName);
    g_hWnd = CreateWindow(windowClassName, hInstance, L"Learning DirectX 12",
        g_ClientWidth, g_ClientHeight);
    //查询窗口矩形以准备g_WindowRect用于切换窗口的全屏状态的变量
    // Initialize the global window rect variable.
    ::GetWindowRect(g_hWnd, &g_WindowRect);

    //创建DX12对象
    ComPtr<IDXGIAdapter4> dxgiAdapter4 = GetAdapter(g_UseWarp);
    //适配器被传递给CreateDevice函数以创建ID3D12Device对象
    g_Device = CreateDevice(dxgiAdapter4);
    //命令队列
    g_CommandQueue = CreateCommandQueue(g_Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    //交换链
    g_SwapChain = CreateSwapChain(g_hWnd, g_CommandQueue,
        g_ClientWidth, g_ClientHeight, g_NumFrames);
    //初始化g_CurrentBackBufferIndex 第一个后台缓冲区索引很可能为 0，但要确保它是直接从交换链查询的，而不是对当前后台缓冲区索引做出假设
    g_CurrentBackBufferIndex = g_SwapChain->GetCurrentBackBufferIndex();
    //创建 RTV 描述符堆 并 从设备查询 RTV 描述符增量大小
    g_RTVDescriptorHeap = CreateDescriptorHeap(g_Device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, g_NumFrames);
    g_RTVDescriptorSize = g_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    //将渲染目标视图填充到描述符堆中
    UpdateRenderTargetViews(g_Device, g_SwapChain, g_RTVDescriptorHeap);

    //创建命令列表和命令分配器
    for (int i = 0; i < g_NumFrames; ++i)
    {
        g_CommandAllocators[i] = CreateCommandAllocator(g_Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    }
    g_CommandList = CreateCommandList(g_Device,
        g_CommandAllocators[g_CurrentBackBufferIndex], D3D12_COMMAND_LIST_TYPE_DIRECT);

    //创建栅栏和栅栏事件对象
    g_Fence = CreateFence(g_Device);
    g_FenceEvent = CreateEventHandle();

    //显示窗口并进入应用程序的消息循环
    g_IsInitialized = true;
 
    ::ShowWindow(g_hWnd, SW_SHOW);
    
    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
    }

    // 在关闭之前，请确保命令队列已完成所有命令。
    Flush(g_CommandQueue, g_Fence, g_FenceValue, g_FenceEvent);
    //释放栅栏事件对象的句柄
    ::CloseHandle(g_FenceEvent);
 
    return 0;
}