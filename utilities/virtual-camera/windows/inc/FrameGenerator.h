#pragma once

#include "framework.h"
#include <queue>

class FrameGenerator
{
	UINT _width;
	UINT _height;
	GUID _format;
	ULONGLONG _frame;
	MFTIME _prevTime;
	HANDLE _deviceHandle;
	GUID _clsid;
	wil::com_ptr_nothrow<ID3D11Texture2D> _texture;
	wil::com_ptr_nothrow<ID2D1RenderTarget> _renderTarget;
	wil::com_ptr_nothrow<ID2D1SolidColorBrush> _whiteBrush;
	wil::com_ptr_nothrow<IDWriteTextFormat> _textFormat;
	wil::com_ptr_nothrow<IDWriteFactory> _dwrite;
	wil::com_ptr_nothrow<IMFTransform> _converter;
	wil::com_ptr_nothrow<IWICBitmap> _bitmap;
	wil::com_ptr_nothrow<IMFDXGIDeviceManager> _dxgiManager;

	HRESULT CreateRenderTargetResources(UINT width, UINT height);

public:
	FrameGenerator() = delete;

	FrameGenerator(const UINT width, const UINT height, const GUID format) :
		_width(width),
		_height(height),
		_format(format),
		_frame(0),
		_prevTime(MFGetSystemTime()),
		_deviceHandle(nullptr),
		_clsid(GUID_NULL){

	}

	~FrameGenerator()
	{
		if (_dxgiManager && _deviceHandle)
		{
			auto hr = _dxgiManager->CloseDeviceHandle(_deviceHandle); // don't report error at that point
			if (FAILED(hr))
			{
				WINTRACE(L"FrameGenerator CloseDeviceHandle: 0x%08X", hr);
			}
		}
	}

	HRESULT SetD3DManager(IUnknown* manager, UINT width, UINT height);
	const bool HasD3DManager() const;
	HRESULT EnsureRenderTarget(UINT width, UINT height);
	HRESULT Generate(IMFSample* sample, REFGUID format, IMFSample** outSample);
	HRESULT GenerateFromExternal(IMFSample* sample, const GUID& clsid, REFGUID format, IMFSample** outSample);
};