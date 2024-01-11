#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h> // For HRESULT
#include <stdexcept>

// From DXSampleHelper.h 
// Source: https://github.com/Microsoft/DirectX-Graphics-Samples
// 可用于检查 DirectX API 函数的返回值的函数。如果函数返回失败代码，则会引发异常。
inline void ThrowIfFailed(HRESULT hr)
{
	if (FAILED(hr))
	{
		throw std::exception();
	}
}