#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h> // For CommandLineToArgvW

// min/max 宏与同名的成员函数冲突。
// Only use std::min and std::max defined in <algorithm>.
#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif

// 为了定义名为 CreateWindow 的函数，需要取消定义 Windows 宏。
#if defined(CreateWindow)
#undef CreateWindow
#endif

// Windows 运行时库。Microsoft::WRL::ComPtr<> 模板类。
#include <wrl.h>
using namespace Microsoft::WRL;

// DirectX 12 specific headers.
#include <d3dx12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <DirectXTex/DirectXTex.h>
#include <DirectXTex/DirectXTex.inl>

using namespace DirectX;



// STL Headers
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// Helper functions
#include <Helpers.h>