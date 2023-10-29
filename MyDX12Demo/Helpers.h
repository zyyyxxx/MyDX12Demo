#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h> // For HRESULT
#include <stdexcept>

// From DXSampleHelper.h 
// Source: https://github.com/Microsoft/DirectX-Graphics-Samples
// �����ڼ�� DirectX API �����ķ���ֵ�ĺ����������������ʧ�ܴ��룬��������쳣��
inline void ThrowIfFailed(HRESULT hr)
{
	if (FAILED(hr))
	{
		throw std::exception();
	}
}