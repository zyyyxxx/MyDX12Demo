#include "Tutorial2.h"
 
#include "Application.h"
#include "CommandQueue.h"
#include <Helpers.h>
#include "Window.h"
 
#include <wrl.h>
using namespace Microsoft::WRL;
 
#include <d3dx12.h>
#include <d3dcompiler.h>
 
#include <algorithm> // For std::min and std::max.
#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif
 
using namespace DirectX;