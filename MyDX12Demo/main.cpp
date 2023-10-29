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
// �������ĺ�̨������֡������
const uint8_t g_NumFrames = 3;
// Use WARP adapter �Ƿ�ʹ�������դ����
bool g_UseWarp = false;

uint32_t g_ClientWidth = 1280;
uint32_t g_ClientHeight = 720;

// Set to true once the DX12 objects have been initialized.
// ��ʼ����־
bool g_IsInitialized = false;

// Window handle. g_hWnd�����洢����ϵͳ���ڵľ�����ô��ڽ�������ʾ��Ⱦ��ͼ��
HWND g_hWnd;
// Window rectangle (used to toggle full screen state). g_WindowRect�������ڴ洢����ȫ��ģʽ֮ǰ����ǰ���ڳߴ�
RECT g_WindowRect;


// DirectX 12 Objects --------------------------------------------------
ComPtr<ID3D12Device2> g_Device; //DirectX 12 �豸����
ComPtr<ID3D12CommandQueue> g_CommandQueue; //�������
ComPtr<IDXGISwapChain4> g_SwapChain; //������
ComPtr<ID3D12Resource> g_BackBuffers[g_NumFrames]; //����ָ���̨��������Դ��ָ�� ���л�������������Դ����ʹ��ID3D12ResourceDirectX 12 �еĽӿ����õġ�
ComPtr<ID3D12GraphicsCommandList> g_CommandList; //GPU ���� ���洢ָ��ID3D12GraphicsCommandList��ָ�롣
ComPtr<ID3D12CommandAllocator> g_CommandAllocators[g_NumFrames];//ID3D12CommandAllocator������ GPU �����¼�������б��еĺ󱸴洢��

// �洢������������̨����������ȾĿ����ͼ���������� 
// render target view (RTV) ������������Դ��GPU�ڴ��е�λ�á�����ĳߴ磨��Ⱥ͸߶ȣ��Լ�����ĸ�ʽ
ComPtr<ID3D12DescriptorHeap> g_RTVDescriptorHeap;

UINT g_RTVDescriptorSize; //���� RTV �������Ĵ�С
UINT g_CurrentBackBufferIndex; //�洢��������ǰ��̨������������
//-----------------------------------------------------------------------

// Synchronization objects GPUͬ������
ComPtr<ID3D12Fence> g_Fence;
uint64_t g_FenceValue = 0; //��������з����źŵ���һ��դ��ֵ�洢�ڸ�g_FenceValue������
uint64_t g_FrameFenceValues[g_NumFrames] = {}; //����g_FrameFenceValues�������ڸ����������ض�֡��������з����źŵ�դ��ֵ��
HANDLE g_FenceEvent; //g_FenceEvent�����ǲ���ϵͳ�¼�����ľ�������ڽ���դ���Ѵﵽ�ض�ֵ��֪ͨ��

//���Ƶ�ǰ������������
// By default, enable V-Sync.
// Can be toggled with the V key.
bool g_VSync = true;
bool g_TearingSupported = false;
// By default, use windowed mode.
// Can be toggled with the Alt+Enter or F11
bool g_Fullscreen = false;

#pragma endregion

// Window callback function. ���ڻص�����
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

//ִ��Ӧ�ó���ʱͨ���ṩ�����в���������һЩȫ�ֶ���ı�����
/*
    -w,--width	 ָ����Ⱦ���ڵĿ�ȣ�������Ϊ��λ����
    -h,--height	 ָ����Ⱦ���ڵĸ߶ȣ�������Ϊ��λ����
    -warp,--warp ʹ�� Windows �߼���դ��ƽ̨ (WARP) �����豸����
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

//���� Direct3D 12 ���Բ�
void EnableDebugLayer()
{
#if defined(_DEBUG)
    //��ִ���κ��� DX12 ��صĲ���֮ǰ����ʼ�����õ��Բ㣬�Ա���Բ� ���񴴽� DX12 ����ʱ���ɵ����п��ܴ���
    ComPtr<ID3D12Debug> debugInterface;
    //IID_PPV_ARGS���ڼ����ӿ�ָ�룬������ʹ�õĽӿ�ָ��������Զ��ṩ������ӿڵ� IID ֵ��
    //�����ӿ�ָ��ķ����еĳ����﷨��������������
    //һ�� [in] ������ͨ��Ϊ ����REFIID������ָ��Ҫ�����Ľӿڵ� IID��
    //һ�� [out] ������ͨ��Ϊ ����void**�����ڽ��սӿ�ָ�롣
    //�ú���ݽӿ�ָ������ͼ���IID������Է�ֹIID�ͽӿ�ָ�����Ͳ�ƥ��ı������
    //Windows ������ԱӦʼ�ս��˺�����Ҫ���� IID �ͽӿ�ָ��������κη���һ��ʹ�á�
    ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface)));
    debugInterface->EnableDebugLayer();
#endif
}

//ע�ᴰ����
void RegisterWindowClass( HINSTANCE hInst, const wchar_t* windowClassName )
{
    // ע��һ�����������������ǵ���Ⱦ���ڡ�
    WNDCLASSEXW windowClass = {};

    ////struct��С
    windowClass.cbSize = sizeof(WNDCLASSEX);
    
    //CS_HREDRAW����ʽָ������ƶ����С���������˿ͻ�������Ŀ�ȣ�������CS_VREDRAW�����������ڣ���������ʽָ������ƶ����С���������˿ͻ��˵ĸ߶ȣ������»���������������
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    
    //ָ�򴰿ڹ��̵�ָ�룬�ô��ڹ��̽�����ʹ�ô˴����ഴ�����κδ��ڵĴ�����Ϣ��WndProc�ڱ����У�����ָ��֮ǰ��������δ����ĺ�����
    windowClass.lpfnWndProc = &WndProc;
    
    //�ڴ�����ṹ֮�����Ķ����ֽ��������ﲻʹ�øò�����Ӧ����Ϊ0��
    windowClass.cbClsExtra = 0;
    
    //�ڴ���ʵ��֮�����Ķ����ֽ��������ﲻʹ�øò�����Ӧ����Ϊ0��
    windowClass.cbWndExtra = 0;
    //��������Ĵ��ڹ��̵�ʵ���ľ������ģ��ʵ����������ݸ�WinMain�Ժ���ʾ�ĺ�����
    windowClass.hInstance = hInst;
    
    //��ͼ��ľ������ͼ�꽫�������������ʹ��ڱ����������ϽǱ�ʾʹ�ô��ഴ���Ĵ��ڡ�
    //������ʹ�øú�������Դ�ļ�����ͼ��LoadIcon�������ֵΪNULL����nullptr������ʹ��Ĭ��Ӧ�ó���ͼ�ꡣ
    windowClass.hIcon = ::LoadIcon(hInst, NULL);

    //����ľ�������������Ч�α���Դ�ľ�������ڴ���ʾ�����ǽ�ͨ��ָ�� ��ʹ��Ĭ�ϼ�ͷͼ��LoadCursor( nullptr, IDC_ARROW )��
    windowClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);

    //�౳�����ʵľ�����ó�Ա���������ڻ��Ʊ����Ļ��ʵľ����Ҳ��������ɫֵ��
    //��ɫֵ���������±�׼ϵͳ��ɫ֮һ�����뽫ֵ 1 ��ӵ���ѡ��ɫ���������������ɫֵ������뽫��ת��Ϊ����HBRUSH����֮һ��
    windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    
    //ָ���� null ��β���ַ�����ָ�룬���ַ���ָ����˵�����Դ���ƣ������Ƴ�������Դ�ļ��С�����ó�ԱΪNULL�������ڸ���Ĵ���û��Ĭ�ϲ˵���
    windowClass.lpszMenuName = NULL;
    windowClass.lpszClassName = windowClassName;
    windowClass.hIconSm = ::LoadIcon(hInst, NULL);
 
    static ATOM atom = ::RegisterClassExW(&windowClass);
    assert(atom > 0);
}


//��������ϵͳ���ڵ�ʵ��
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

    // ��������ʵ��
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

//��ѯ DirectX 12 ������
ComPtr<IDXGIAdapter4> GetAdapter(bool useWarp)
{
    //�ڲ�ѯ����������֮ǰ�����봴�� DXGI ����
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
        //����Ҫʹ��WARP�豸������£�IDXGIFactory4::EnumWarpAdapter����ʹ�ø÷���ֱ�Ӵ���WARP��������
        //IDXGIFactory4::EnumWarpAdapter��������һ��ָ��IDXGIAdapter1�ӿڵ�ָ�룬����GetAdapter��������һ��ָ��IDXGIAdapter4�ӿڵ�ָ�롣
        //Ϊ�˽� COM ����ת��Ϊ��ȷ�����ͣ�Ӧʹ��ComPtr::As
        ThrowIfFailed(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&dxgiAdapter1)));
        ThrowIfFailed(dxgiAdapter1.As(&dxgiAdapter4));
    }else
    {
        //����ʹ�� WARP ������ʱ��DXGI Factory ���ڲ�ѯӲ��������
        //IDXGIFactory1::EnumAdapters1��������ö��ϵͳ�п��õ�GPU������
        SIZE_T maxDedicatedVideoMemory = 0;
        for (UINT i = 0; dxgiFactory->EnumAdapters1(i, &dxgiAdapter1) != DXGI_ERROR_NOT_FOUND; ++i)
        {
            DXGI_ADAPTER_DESC1 dxgiAdapterDesc1;
            dxgiAdapter1->GetDesc1(&dxgiAdapterDesc1);
            
            //����������Ƿ���Դ��� D3D12 �豸������ʵ�ʴ������� ��������Դ棨���� CPU ����������������ѡ��
            if ((dxgiAdapterDesc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
                //D3D12CreateDevice��������һ�����գ��豸������˺�������S_OK����ú����ɹ��������� DirectX 12 ������������
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

//���� DirectX 12 �豸
ComPtr<ID3D12Device2> CreateDevice(ComPtr<IDXGIAdapter4> adapter)
{
    ComPtr<ID3D12Device2> d3d12Device2;
    /*
        HRESULT WINAPI D3D12CreateDevice(
            _In_opt_  IUnknown          *pAdapter,
            D3D_FEATURE_LEVEL MinimumFeatureLevel, �ɹ������豸��������Ҫ��
            _In_      REFIID            riid,      �豸�ӿڵ�ȫ��Ψһ��ʶ�� (GUID)�� �ò����� ppDevice ����ʹ�õ����� IID_PPV_ARGS ��Ѱַ��
            _Out_opt_ void              **ppDevice ָ������豸ָ����ڴ���ָ��
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

//�����������
ComPtr<ID3D12CommandQueue> CreateCommandQueue(ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type )
{
    ComPtr<ID3D12CommandQueue> d3d12CommandQueue;
    D3D12_COMMAND_QUEUE_DESC desc = {};
    /* ָ��Ҫ������������е����ͣ���������������֮һ��
   D3D12_COMMAND_LIST_TYPE_DIRECT��������п�����ִ�л��ơ�����͸������������ͨ�õ�����������ͣ��ڴ��������¶���ʹ�á�
   D3D12_COMMAND_LIST_TYPE_COMPUTE��������п�����ִ�м���͸������
   D3D12_COMMAND_LIST_TYPE_COPY��������п�����ִ�и������*/
    desc.Type =     type;
    /*INT Priority��������е����ȼ�������������ֵ֮һ��
D3D12_COMMAND_QUEUE_PRIORITY_NORMAL��������о�����ͨ���ȼ���
D3D12_COMMAND_QUEUE_PRIORITY_HIGH��������о��и����ȼ���
D3D12_COMMAND_QUEUE_PRIORITY_GLOBAL_REALTIME��������о���ȫ��ʵʱ���ȼ���*/
    desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    desc.Flags =    D3D12_COMMAND_QUEUE_FLAG_NONE;// ö���еĸ��ӱ�־
    desc.NodeMask = 0;//���ڵ� GPU �������뽫������Ϊ��
 
    ThrowIfFailed(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&d3d12CommandQueue)));
 
    return d3d12CommandQueue;
}

//����˺��֧��
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

//����������
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
    //��������������δ����Ľṹ��
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = width;
    swapChainDesc.Height = height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.Stereo = FALSE;
    swapChainDesc.SampleDesc = { 1, 0 };//�������ز��������Ľṹ�� ʹ�÷�תģ�ͽ�����ʱ���ó�Ա����ָ��Ϊ{1, 0}
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;//������̨�������ı���ʹ������� CPU ����ѡ���̨��������������ɫ������ ( DXGI_USAGE_SHADER_INPUT) ����ȾĿ����� ( DXGI_USAGE_RENDER_TARGET_OUTPUT)��
    swapChainDesc.BufferCount = bufferCount;//�����������л�����������ֵ������ȫ��������ʱ��ͨ���ڴ�ֵ�а���ǰ�˻���������ʹ�÷�ת����ģ��ʱ������������С������������
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;//�ڱ�ʶ��̨��������С������Ŀ�����ʱ�ĵ�����С��Ϊ
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;//����������ʹ�õı�ʾģ���Լ��ڳ��ֱ��������ֻ��������ݵ�ѡ�
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;//�ڱ�ʶ��������̨��������͸������Ϊ
    // It is recommended to always allow tearing if tearing support is available.
    //ʹ�ð�λ OR ������ϵ� DXGI_SWAP_CHAIN_FLAG ����ֵ����ϡ� ���˺��֧�ֿ��ã���Ӧʼ��ָ�� DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING ��־��
    swapChainDesc.Flags = CheckTearingSupport() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    // ����������
    ComPtr<IDXGISwapChain1> swapChain1;
    ThrowIfFailed(dxgiFactory4->CreateSwapChainForHwnd(
        commandQueue.Get(),//ָ��ֱ��������е�ָ��
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

//������������
/*�� DirectX 12 ��ʼ�����Դ�����Դ��������������ȾĿ����ͼ( RTV )����ɫ����Դ��ͼ( SRV )�����������ͼ( UAV ) ������������ͼ( CBV )))����Ҫ�����������ѡ�
������ͬһ���д���ĳЩ���͵���Դ��ͼ���������������磬CBV��SRV �� UAV ���Դ洢��ͬһ���У��� RTV �� Sampler ��ͼ������Ҫ�������������ѡ�*/
ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(ComPtr<ID3D12Device2> device,
    D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors)
{
    ComPtr<ID3D12DescriptorHeap> descriptorHeap;
 
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors = numDescriptors;
    desc.Type = type;
    //Type ��Ա���Ծ�������ֵ֮һ��
    //D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV����������������ɫ����Դ�����������ͼ��ϵ��������ѡ�
    //D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER�����������������ѡ�
    //D3D12_DESCRIPTOR_HEAP_TYPE_RTV����ȾĿ����ͼ���������ѡ�
    //D3D12_DESCRIPTOR_HEAP_TYPE_DSV�����ģ����ͼ���������ѡ�
 
    ThrowIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap)));
 
    return descriptorHeap;
}

//������ȾĿ����ͼ (RTV) 
/*(RTV) �����˿��Ը��ӵ�����ϲ��׶εİ󶨲۵���Դ
��ȾĿ����ͼ��������������ɫ���׶μ����������ɫ����Դ*/
void UpdateRenderTargetViews(ComPtr<ID3D12Device2> device,
    ComPtr<IDXGISwapChain4> swapChain, ComPtr<ID3D12DescriptorHeap> descriptorHeap)
{
    //��ѯ�����������Ĵ�С
    auto rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    //Ϊ�˵������������е������� GetCPUDescriptorHandleForHeapStart����D3D12_CPU_DESCRIPTOR_HANDLE��ָ��������������������ָ��ı���
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(descriptorHeap->GetCPUDescriptorHandleForHeapStart());
 
    for (int i = 0; i < g_NumFrames; ++i)
    {
        ComPtr<ID3D12Resource> backBuffer;
        //��ѯָ�򽻻�����̨��������ָ��
        ThrowIfFailed(swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));
        //���� RTV
        //��һ��������ָ�������ȾĿ���������Դ��ָ�롣
        //�ڶ���������ָ��ṹ��ָ��D3D12_RENDER_TARGET_VIEW_DESC������NULL���ڴ�����Դ��Ĭ��������
        //����������ID3D12Device::CreateRenderTargetView�Ƿ�����ͼ���������ľ����
        device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);
 
        g_BackBuffers[i] = backBuffer;
        //ʹ�ýṹ��ķ�����������������������������е���һ�����
        rtvHandle.Offset(rtvDescriptorSize);
    }
}

//�������������
/*
 * ����������������б�ʹ�õĺ󱸴洢��
 * ��ָ����������ʹ�õ������б�����͡�
 * ������������ṩ�κι��ܣ�ֻ��ͨ�������б��ӷ��ʡ�
 * ���������һ��ֻ���ɵ��������б�ʹ�ã��������ڼ�¼�������б��е������� GPU �����ִ�к�����ʹ�á�
 */
ComPtr<ID3D12CommandAllocator> CreateCommandAllocator(ComPtr<ID3D12Device2> device,
    D3D12_COMMAND_LIST_TYPE type)
{
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    ThrowIfFailed(device->CreateCommandAllocator(type, IID_PPV_ARGS(&commandAllocator)));
 
    return commandAllocator;
}

//���������б�
/*
 * �����б����ڼ�¼�� GPU ��ִ�е����
 * ����ǰ�汾�� DirectX ��ͬ����¼�������б��е������ִ��ʼ�ջ��ӳ١�
 * Ҳ����˵���������б����͵��������֮ǰ������ִ�ж������б���û��ƻ�������
 * �������������ͬ�������б������������ִ�к�����������á�Ψһ���������ڼ�¼�κ�������֮ǰ���������������б�
 */
ComPtr<ID3D12GraphicsCommandList> CreateCommandList(ComPtr<ID3D12Device2> device,
    ComPtr<ID3D12CommandAllocator> commandAllocator, D3D12_COMMAND_LIST_TYPE type)
{
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ThrowIfFailed(device->CreateCommandList(0, type, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));
    
    ThrowIfFailed(commandList->Close());
    //�����б����ڼ�¼״̬�´����ġ�
    //Ϊ�˱���һ���ԣ���Ⱦѭ���ж������б�ִ�еĵ�һ���������Ժ���ʾ����ID3D12GraphicsCommandList::Reset.
    //�����������б�֮ǰ�������Ƚ���رա��ر������б��Ա��������Ⱦѭ���м�¼����֮ǰ��������
    return commandList;
}

//����դ��Fence
/*
 *  Fence��GPU/CPU ͬ������Ľӿڡ�դ���������� CPU �� GPU ��ִ��ͬ����
    ���ڲ���դ���洢���� 64 λ�޷�������ֵ��դ���ĳ�ʼֵ���ڴ���դ��ʱָ���ġ�
    ʹ�ø÷����� CPU �ϸ���դ�����ڲ�ֵID3D12Fence::Signal����ʹ�ø÷����� GPU �ϸ���դ�����ڲ�ֵID3D12CommandQueue::Signal��
    Ҫ�ȴ�Fence�ﵽ CPU �ϵ��ض�ֵ����ʹ�ø�ID3D12Fence::SetEventOnCompletion������Ȼ����ø�WaitForSingleObject������
    Ҫ�ȴ�դ���� GPU �ϴﵽ�ض�ֵ����ʹ��ID3D12CommandQueue::Wait������
 */
ComPtr<ID3D12Fence> CreateFence(ComPtr<ID3D12Device2> device)
{
    ComPtr<ID3D12Fence> fence;
 
    ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
 
    return fence;
}

//����Event
/*
 *���դ����δ�յ��ض�ֵ���źţ��� CPU �߳̽���Ҫ��ֹ�κν�һ���Ĵ���ֱ��դ���յ���ֵ���źš�
 *����ϵͳ�¼������������ CPU �̣߳�ֱ��դ���յ��ź�Ϊֹ�������������ĺ���CreateEventHandle���ڴ�������ϵͳ�¼���
*/
HANDLE CreateEventHandle()
{
    HANDLE fenceEvent;
    
    fenceEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
    //BOOL bInitialState ����ò���ΪTRUE�����¼�����ĳ�ʼ״̬Ϊ���źţ������������źŵġ�
    assert(fenceEvent && "Failed to create fence event.");
 
    return fenceEvent;
}

//Fence �ź�
/*
    ��Signal�������ڴ� GPU ����Fence�źš�
    Ӧ��ע����ǣ���ʹ�ø�ID3D12CommandQueue::Signal������ GPU ����դ���ź�ʱ��դ���������������źţ����ǽ��� GPU ���������ִ���ڼ䵽��õ�ʱ�ŷ����źš�
    �ڵ����źŷ���֮ǰ�Ŷӵ��κ����������դ�������ź�֮ǰ���ִ�С�
    ������������Ŷӵ������������ִ�к�դ�����յ��źš�
    ��Signal�������� CPU �߳������� GPU �ϸ�֡���������С����κ���Դ֮ǰӦ�ȴ���դ��ֵ��
*/
uint64_t Signal(ComPtr<ID3D12CommandQueue> commandQueue, ComPtr<ID3D12Fence> fence,
    uint64_t& fenceValue)
{
    uint64_t fenceValueForSignal = ++fenceValue;
    ThrowIfFailed(commandQueue->Signal(fence.Get(), fenceValueForSignal));
 
    return fenceValueForSignal;
}

//�ȴ�Fenceֵ
/*
 *CPU �߳̿�����Ҫ��ͣ�Եȴ� GPU �������ִ��д����Դ�����Ȼ��������ʹ�á�
 *���磬�����ý������ĺ�̨��������Դ֮ǰ��ʹ�ø���Դ��Ϊ��ȾĿ����κ������������ɣ�Ȼ��������øú�̨��������Դ��
 *�κδ�δ������дĿ�����Դ�����������������Ҫ˫���壬Ҳ����Ҫ����ɫ������Ϊֻ����Դ����֮ǰֹͣ CPU �̡߳�
 *��д��Դ��������ȾĿ�꣩ȷʵ��Ҫͬ�����Ա�����Դ�����������ͬʱ�޸ġ�
 */
void WaitForFenceValue(ComPtr<ID3D12Fence> fence, uint64_t fenceValue, HANDLE fenceEvent,
    std::chrono::milliseconds duration = std::chrono::milliseconds::max() )
{
    //WaitForFenceValue���դ����δ�ﵽ�����յ��źţ��ض�ֵ��������ֹͣ CPU �̡߳��ú������ȴ�duration����ָ���ĳ���ʱ�䣬Ĭ��Ϊ584million years
    //���դ����δ�ﵽ��ֵ������դ��ע��һ���¼����󣬲���դ���ﵽָ��ֵʱ���η����źš�
    if (fence->GetCompletedValue() < fenceValue)
    {
        ThrowIfFailed(fence->SetEventOnCompletion(fenceValue, fenceEvent));
        ::WaitForSingleObject(fenceEvent, static_cast<DWORD>(duration.count()));
    }
}

//ˢ��GPU
/*
 *��Flush��������ȷ��֮ǰ�� GPU ��ִ�е��κ���������ִ�У�Ȼ������� CPU �̼߳�������
 *�����ȷ����ǰ�� GPU �ϡ������С����������õ��κκ�̨��������Դ�ڵ�����С֮ǰ�����ִ�зǳ����á�
 *ǿ�ҽ������ͷ���������ϵ�ǰ�������С��������б�������õ��κ���Դ֮ǰ�����磬�ڹر�Ӧ�ó���֮ǰ��ˢ�� GPU ������С�
 *�������ݣ�The Flush function is simply a Signal followed by a WaitForFenceValue.
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
 *1.�����̨������
 *2.������Ⱦ֡
 */
void Render()
{
    //���ݵ�ǰ��̨���������� ����ָ������������ͺ�̨��������Դ��ָ�롣
    auto commandAllocator = g_CommandAllocators[g_CurrentBackBufferIndex];
    auto backBuffer = g_BackBuffers[g_CurrentBackBufferIndex];

    //��������������������б����׼�����ڼ�¼��һ֡�������б�
    commandAllocator->Reset();
    g_CommandList->Reset(commandAllocator.Get(), nullptr);

    // �����ȾĿ��
    {
        //�������ȾĿ��֮ǰ�����뽫��ת������RENDER_TARGET״̬
        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            backBuffer.Get(),
            D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        g_CommandList->ResourceBarrier(1, &barrier);
        //�����̨������
        FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(g_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
            g_CurrentBackBufferIndex, g_RTVDescriptorSize);
 
        g_CommandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    }

    // չʾ
    {
        // ��Դת��
        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            backBuffer.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        g_CommandList->ResourceBarrier(1, &barrier);
        
        //ID3D12GraphicsCommandList::Close�ر������б��÷����������������б��ϵ��ã�Ȼ����������������ִ�С�
        ThrowIfFailed(g_CommandList->Close());
        
        ID3D12CommandList* const commandLists[] = {
            g_CommandList.Get()
        };//_countof��Windows�꣬��������һ����̬����������е�Ԫ�صĸ���
        g_CommandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);
    
        //���������ĵ�ǰ��̨���������ֵ���Ļ�� IDXGISwapChain::Present
        UINT syncInterval = g_VSync ? 1 : 0;
        UINT presentFlags = g_TearingSupported && !g_VSync ? DXGI_PRESENT_ALLOW_TEARING : 0;
        ThrowIfFailed(g_SwapChain->Present(syncInterval, presentFlags));
 
        g_FrameFenceValues[g_CurrentBackBufferIndex] = Signal(g_CommandQueue, g_Fence, g_FenceValue);

        //ʹ��DXGI_SWAP_EFFECT_FLIP_DISCARD��תģ��ʱ������֤��̨������������˳���������ġ�
        //��IDXGISwapChain3::GetCurrentBackBufferIndex�������ڻ�ȡ��������ǰ��̨��������������
        g_CurrentBackBufferIndex = g_SwapChain->GetCurrentBackBufferIndex();
        //������һ֡�����ݸ��ǵ�ǰ��̨������������֮ǰ��ʹ��ǰ��������WaitForFenceValue����ֹͣ CPU �߳�
        WaitForFenceValue(g_Fence, g_FrameFenceValues[g_CurrentBackBufferIndex], g_FenceEvent);
    }
}

// ������С
void Resize(uint32_t width, uint32_t height)
{
    if (g_ClientWidth != width || g_ClientHeight != height)
    {
        // Don't allow 0 size swap chain back buffers.
        g_ClientWidth = std::max(1u, width );
        g_ClientHeight = std::max( 1u, height);
 
        // Flush the GPU queue to make sure the swap chain's back buffers
        // are not being referenced by an in-flight command list.
        // ˢ�� GPU ���У���ȷ���������ĺ�̨������δ�����ڽ��е������б����á�
        // ���� GPU �Ͽ�����һ���������С��������б����ý������ĺ�̨�������������Ҫʹ��ǰ�������ĺ��� Flush ˢ�� GPU ��
        Flush(g_CommandQueue, g_Fence, g_FenceValue, g_FenceEvent);
        
        for (int i = 0; i < g_NumFrames; ++i)
        {
            // Any references to the back buffers must be released
            // before the swap chain can be resized.
            // �������ͷŶԺ�̨���������κ����ã�Ȼ����ܵ����������Ĵ�С��
            // �ͷŶԽ�������̨�������ı������á�ÿ֡դ��ֵҲ����Ϊ��ǰ��̨������������դ��ֵ��
            g_BackBuffers[i].Reset();
            g_FrameFenceValues[i] = g_FrameFenceValues[g_CurrentBackBufferIndex];
        }
        // ��ѯ��ǰ���������������Ա�ʹ����ͬ����ɫ��ʽ�ͽ�������־ ���´�����������������
        // ���ں�̨���������������ܲ���ͬ����˸���Ӧ�ó�����֪�ĵ�ǰ��̨�����������ǳ���Ҫ��
        DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
        ThrowIfFailed(g_SwapChain->GetDesc(&swapChainDesc));
        ThrowIfFailed(g_SwapChain->ResizeBuffers(g_NumFrames, g_ClientWidth, g_ClientHeight,
            swapChainDesc.BufferDesc.Format, swapChainDesc.Flags));
 
        g_CurrentBackBufferIndex = g_SwapChain->GetCurrentBackBufferIndex();

        //������������������С����Ҫ����������Щ����������������ʹ����ǰ�����ķ��� UpdateRenderTargetViews ���� RTV ��������
        UpdateRenderTargetViews(g_Device, g_SwapChain, g_RTVDescriptorHeap);
    }
}

// ����ȫ��
/*
    *Ϊ�˽��ȫ����ռģʽ�����⣬��ʹ��ȫ���ޱ߿򴰿ڣ�FSBW������󻯴��ڡ�

    ʹ��IDXGISwapChain::SetFullscreenState����̨�������л���ȫ����ռģʽ���ܺ��鷳�����Ҿ�������ȱ�㣺
    ����������ʱ��Ҫһ��DXGI_SWAP_CHAIN_FULLSCREEN_DESC�ṹ���л���ȫ��״̬��
    �ֱ��ʺ�ˢ���ʱ�������ʾ��֧�ֵ�ģʽ֮һƥ�䡣�ṩ����ȷ�ķֱ��ʻ�ˢ�������ÿ��ܻᵼ�������û�������
    �л���ȫ����ռģʽ���ܻᵼ�¶���ʾ�������е��κ�������ʾ����ڡ�
    ���������Ϊȫ����ʾ��
    ���������Ⱦ�� GPU ����ֱ�����ӵ���ʾ�豸�����л���ȫ��״̬����ʧ�ܡ����ڶ� GPU �����кܳ�����������м��� Intel ͼ��оƬ��ר�� GPU �ıʼǱ����ԣ���
 */
void SetFullscreen(bool fullscreen)
{
    if (g_Fullscreen != fullscreen)
    {
        g_Fullscreen = fullscreen;
 
        if (g_Fullscreen) // Switching to fullscreen.
            {
            // GetWindowRect�������洰�ھ������ڻָ�
            ::GetWindowRect(g_hWnd, &g_WindowRect);

            // ��������ʽ����Ϊ�ޱ߿򴰿ڡ�SetWindowLong�������������ޱ߿򴰿���ʽ
            // Set the window style to a borderless window so the client area fills
            // the entire screen.
            UINT windowStyle = WS_OVERLAPPEDWINDOW & ~(WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
            ::SetWindowLongW(g_hWnd, GWL_STYLE, windowStyle);
        
            // Query the name of the nearest display device for the window.
            // This is required to set the fullscreen dimensions of the window
            // when using a multi-monitor setup.    
            //��ѯ����Ӧ�ó��򴰿��������ʾ���ĳߴ�
            //��ѯ������������GetMonitorInfo���Ӻ������صĽṹGetMonitorInfo����һ�����νṹ������������������ȫ������
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


//������Ϣ����
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
            
        case WM_PAINT: //���»���Ӧ�ó��򴰿�
            Update();
            Render();
            break;
        //����ס Alt ��ͬʱ����һ����ϼ������磬Alt+Enter��ʱ��WM_SYSKEYDOWN��Ϣ�������͵����ڴ�������
            
        //�������κη�ϵͳ��ʱ��������WM_KEYDOWN��Ϣ������ĳ����������ס Alt ����
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
            // ���δ�������Ϣ����Ĭ�ϴ��ڹ��̽��ڰ� Alt+Enter �������ʱ����ϵͳ֪ͨ������
        case WM_SYSCHAR:
            break;
            
        case WM_SIZE:
            {
                //The client area of the window
                //�ͻ��˾������ڼ����Ⱥ͸߶��Ե����������������Ĵ�С��
                RECT clientRect = {};
                ::GetClientRect(g_hWnd, &clientRect);
 
                int width = clientRect.right - clientRect.left;
                int height = clientRect.bottom - clientRect.top;
 
                Resize(width, height);
            }
            break;
        //���Ͻǲ��
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
    //Windows 10 ���������ÿ������ V2 DPI ��֪�����ġ�
    //ʹ������ DPI ��֪ģʽ��Ӧ�ó����ܹ�ʵ�ִ��ڿͻ������ 100% �������ţ�ͬʱ��Ȼ����ǿͻ���������������Ͳ˵������� DPI ����
    //���磬������� 4K UHD��3840��2160 �� 2160p����ʾ���������ѽ� DPI ��������Ϊ 150%����Ĭ����Ϊ�ǽ��ͻ������С����Ϊ 2560��1440��
    //�ڴ�������֮ǰָ��Ӧ�ó���� DPI ��֪�����޸������⣬ͬʱ��Ȼ�����ڷǿͻ���������� DPI ���ţ����磬���ڵı������Խ����� DPI ���ý������ţ���
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 
    // Window class name. Used for registering / creating the window.
    const wchar_t* windowClassName = L"DX12WindowClass";
    ParseCommandLineArguments();//���������в���

    EnableDebugLayer();
    
    //��ѯӦ�ó����tearing֧��
    g_TearingSupported = CheckTearingSupport();
    //ע�ᴴ������
    RegisterWindowClass(hInstance, windowClassName);
    g_hWnd = CreateWindow(windowClassName, hInstance, L"Learning DirectX 12",
        g_ClientWidth, g_ClientHeight);
    //��ѯ���ھ�����׼��g_WindowRect�����л����ڵ�ȫ��״̬�ı���
    // Initialize the global window rect variable.
    ::GetWindowRect(g_hWnd, &g_WindowRect);

    //����DX12����
    ComPtr<IDXGIAdapter4> dxgiAdapter4 = GetAdapter(g_UseWarp);
    //�����������ݸ�CreateDevice�����Դ���ID3D12Device����
    g_Device = CreateDevice(dxgiAdapter4);
    //�������
    g_CommandQueue = CreateCommandQueue(g_Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    //������
    g_SwapChain = CreateSwapChain(g_hWnd, g_CommandQueue,
        g_ClientWidth, g_ClientHeight, g_NumFrames);
    //��ʼ��g_CurrentBackBufferIndex ��һ����̨�����������ܿ���Ϊ 0����Ҫȷ������ֱ�Ӵӽ�������ѯ�ģ������ǶԵ�ǰ��̨������������������
    g_CurrentBackBufferIndex = g_SwapChain->GetCurrentBackBufferIndex();
    //���� RTV �������� �� ���豸��ѯ RTV ������������С
    g_RTVDescriptorHeap = CreateDescriptorHeap(g_Device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, g_NumFrames);
    g_RTVDescriptorSize = g_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    //����ȾĿ����ͼ��䵽����������
    UpdateRenderTargetViews(g_Device, g_SwapChain, g_RTVDescriptorHeap);

    //���������б�����������
    for (int i = 0; i < g_NumFrames; ++i)
    {
        g_CommandAllocators[i] = CreateCommandAllocator(g_Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    }
    g_CommandList = CreateCommandList(g_Device,
        g_CommandAllocators[g_CurrentBackBufferIndex], D3D12_COMMAND_LIST_TYPE_DIRECT);

    //����դ����դ���¼�����
    g_Fence = CreateFence(g_Device);
    g_FenceEvent = CreateEventHandle();

    //��ʾ���ڲ�����Ӧ�ó������Ϣѭ��
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

    // �ڹر�֮ǰ����ȷ���������������������
    Flush(g_CommandQueue, g_Fence, g_FenceValue, g_FenceEvent);
    //�ͷ�դ���¼�����ľ��
    ::CloseHandle(g_FenceEvent);
 
    return 0;
}