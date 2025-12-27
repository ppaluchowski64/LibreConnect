#include "Undocumented.h"
#include "Tools.h"
#include "EnumNames.h"
#include "MFTools.h"
#include "FrameGenerator.h"
#include "VCamAPI.h"

struct PushedFrame
{
	std::vector<BYTE> data;
	UINT width;
	UINT height;
	GUID format;
};

extern "C++" __declspec(dllimport) bool VCamAPI_HasExternalFrame(const GUID& clsid);
extern "C++" __declspec(dllimport) bool GetExternalFrame(const GUID& clsid, PushedFrame& frame);

HRESULT FrameGenerator::EnsureRenderTarget(UINT width, UINT height)
{
	if (!HasD3DManager())
	{
		// create a D2D1 render target from WIC bitmap
		wil::com_ptr_nothrow<ID2D1Factory> d2d1Factory;
		RETURN_IF_FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, IID_PPV_ARGS(&d2d1Factory)));

		wil::com_ptr_nothrow<IWICImagingFactory> wicFactory;
		RETURN_IF_FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&wicFactory)));

		RETURN_IF_FAILED(wicFactory->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &_bitmap));

		D2D1_RENDER_TARGET_PROPERTIES props{};
		props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
		props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
		RETURN_IF_FAILED(d2d1Factory->CreateWicBitmapRenderTarget(_bitmap.get(), props, &_renderTarget));

		RETURN_IF_FAILED(CreateRenderTargetResources(width, height));
	}

	_prevTime = MFGetSystemTime();
	_frame = 0;
	return S_OK;
}

const bool FrameGenerator::HasD3DManager() const
{
	return _texture != nullptr;
}

HRESULT FrameGenerator::SetD3DManager(IUnknown* manager, UINT width, UINT height)
{
	RETURN_HR_IF_NULL(E_POINTER, manager);
	RETURN_HR_IF(E_INVALIDARG, !width || !height);

	RETURN_IF_FAILED(manager->QueryInterface(&_dxgiManager));
	RETURN_IF_FAILED(_dxgiManager->OpenDeviceHandle(&_deviceHandle));

	wil::com_ptr_nothrow<ID3D11Device> device;
	RETURN_IF_FAILED(_dxgiManager->GetVideoService(_deviceHandle, IID_PPV_ARGS(&device)));

	// create a texture/surface to write
	CD3D11_TEXTURE2D_DESC desc
	(
		DXGI_FORMAT_B8G8R8A8_UNORM,
		width,
		height,
		1,
		1,
		D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET
	);
	RETURN_IF_FAILED(device->CreateTexture2D(&desc, nullptr, &_texture));
	wil::com_ptr_nothrow<IDXGISurface> surface;
	RETURN_IF_FAILED(_texture.copy_to(&surface));

	// create a D2D1 render target from 2D GPU surface
	wil::com_ptr_nothrow<ID2D1Factory> d2d1Factory;
	RETURN_IF_FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, IID_PPV_ARGS(&d2d1Factory)));

	auto props = D2D1::RenderTargetProperties
	(
		D2D1_RENDER_TARGET_TYPE_DEFAULT,
		D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED)
	);
	RETURN_IF_FAILED(d2d1Factory->CreateDxgiSurfaceRenderTarget(surface.get(), props, &_renderTarget));

	RETURN_IF_FAILED(CreateRenderTargetResources(width, height));

	// create GPU RGB => NV12 converter
	RETURN_IF_FAILED(CoCreateInstance(CLSID_VideoProcessorMFT, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&_converter)));

	wil::com_ptr_nothrow<IMFAttributes> atts;
	RETURN_IF_FAILED(_converter->GetAttributes(&atts));
	TraceMFAttributes(atts.get(), L"VideoProcessorMFT");

	MFT_OUTPUT_STREAM_INFO info{};
	RETURN_IF_FAILED(_converter->GetOutputStreamInfo(0, &info));
	WINTRACE(L"FrameGenerator::EnsureRenderTarget CLSID_VideoProcessorMFT flags:0x%08X size:%u alignment:%u", info.dwFlags, info.cbSize, info.cbAlignment);

	wil::com_ptr_nothrow<IMFMediaType> inputType;
	RETURN_IF_FAILED(MFCreateMediaType(&inputType));
	inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
	MFSetAttributeSize(inputType.get(), MF_MT_FRAME_SIZE, width, height);
	RETURN_IF_FAILED(_converter->SetInputType(0, inputType.get(), 0));

	wil::com_ptr_nothrow<IMFMediaType> outputType;
	RETURN_IF_FAILED(MFCreateMediaType(&outputType));
	outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
	MFSetAttributeSize(outputType.get(), MF_MT_FRAME_SIZE, width, height);
	RETURN_IF_FAILED(_converter->SetOutputType(0, outputType.get(), 0));

	// make sure the video processor works on GPU
	RETURN_IF_FAILED(_converter->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, (ULONG_PTR)manager));
	return S_OK;
}

// common to CPU & GPU
HRESULT FrameGenerator::CreateRenderTargetResources(UINT width, UINT height)
{
	assert(_renderTarget);
	RETURN_IF_FAILED(_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &_whiteBrush));

	RETURN_IF_FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&_dwrite));
	RETURN_IF_FAILED(_dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 40, L"", &_textFormat));
	RETURN_IF_FAILED(_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
	RETURN_IF_FAILED(_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));
	_width = width;
	_height = height;
	return S_OK;
}

HRESULT FrameGenerator::Generate(IMFSample* sample, REFGUID format, IMFSample** outSample)
{
	WINTRACE(L"FrameGenerator::Generate - No external frame available, using internal generator");
	OutputDebugStringA("FrameGenerator::PushFrame");
	RETURN_HR_IF_NULL(E_POINTER, sample);
	RETURN_HR_IF_NULL(E_POINTER, outSample);
	*outSample = nullptr;

	if (_renderTarget)
	{
		_renderTarget->BeginDraw();
		_renderTarget->Clear(D2D1::ColorF(0.0f, 0.5f, 0.5f, 1.0f));
		RETURN_IF_FAILED(_renderTarget->EndDraw());
	}

	// build a sample using either D3D/DXGI (GPU) or WIC (CPU)
	wil::com_ptr_nothrow<IMFMediaBuffer> mediaBuffer;
	if (HasD3DManager())
	{
		// remove all existing buffers
		RETURN_IF_FAILED(sample->RemoveAllBuffers());

		// create a buffer from this and add to sample
		RETURN_IF_FAILED(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), _texture.get(), 0, 0, &mediaBuffer));
		RETURN_IF_FAILED(sample->AddBuffer(mediaBuffer.get()));

		// if we're on GPU & format is not RGB, convert using GPU
		if (format == MFVideoFormat_NV12)
		{
			assert(_converter);
			RETURN_IF_FAILED(_converter->ProcessInput(0, sample, 0));

			MFT_OUTPUT_DATA_BUFFER buffer = {};
			DWORD status = 0;
			RETURN_IF_FAILED(_converter->ProcessOutput(0, 1, &buffer, &status));
			*outSample = buffer.pSample;
		}
		else
		{
			sample->AddRef();
			*outSample = sample;
		}

		_frame++;
		return S_OK;
	}

	RETURN_IF_FAILED(sample->GetBufferByIndex(0, &mediaBuffer));
	wil::com_ptr_nothrow<IMF2DBuffer2> buffer2D;
	BYTE* scanline;
	LONG pitch;
	BYTE* start;
	DWORD length;
	RETURN_IF_FAILED(mediaBuffer->QueryInterface(IID_PPV_ARGS(&buffer2D)));
	RETURN_IF_FAILED(buffer2D->Lock2DSize(MF2DBuffer_LockFlags_Write, &scanline, &pitch, &start, &length));

	wil::com_ptr_nothrow<IWICBitmapLock> lock;
	auto hr = _bitmap->Lock(nullptr, WICBitmapLockRead, &lock);

	if (SUCCEEDED(hr))
	{
		UINT w, h;
		hr = lock->GetSize(&w, &h);
		if (SUCCEEDED(hr))
		{
			UINT wicStride;
			hr = lock->GetStride(&wicStride);
			if (SUCCEEDED(hr))
			{
				UINT wicSize;
				WICInProcPointer wicPointer;
				hr = lock->GetDataPointer(&wicSize, &wicPointer);
				if (SUCCEEDED(hr))
				{
					WINTRACE(L"WIC stride:%u WIC size:%u MF pitch:%u MF length:%u frame:%u format:%s", wicStride, wicSize, pitch, length, _frame, GUID_ToStringW(format).c_str());
					if (format == MFVideoFormat_NV12)
					{
						hr = RGB32ToNV12(wicPointer, wicSize, wicStride, w, h, scanline, length, pitch);
					}
					else
					{
						hr = (wicSize != length || wicStride != pitch) ? E_FAIL : S_OK;
						if (SUCCEEDED(hr))
						{
							if (assert_true(wicPointer)) // WIC annotation is currently wrong on GetDataPointer wicPointer arg
							{
								CopyMemory(scanline, wicPointer, length);
							}
						}
					}

					if (SUCCEEDED(hr))
					{
						_frame++;
						sample->AddRef();
						*outSample = sample;
					}
				}
			}
		}
		lock.reset();
	}

	buffer2D->Unlock2D();
	return hr;
}

HRESULT FrameGenerator::PushExternalFrame(const void* data, UINT width, UINT height, const GUID& format) {
	WINTRACE(L"FrameGenerator::GenerateExternalFrame");
	OutputDebugStringA("FrameGenerator::PushExternalFrame");
	if (!data || !width || !height)
		return E_INVALIDARG;

	winrt::slim_lock_guard lock(_externalFrameLock);

	ExternalFrame frame;
	frame.width = width;
	frame.height = height;
	frame.format = format;

	// Calculate frame size
	size_t frameSize = 0;
	if (format == MFVideoFormat_RGB32)
	{
		frameSize = width * height * 4;
	}
	else if (format == MFVideoFormat_NV12)
	{
		frameSize = width * height * 3 / 2;
	}
	else
	{
		return E_INVALIDARG;
	}

	frame.data.resize(frameSize);
	memcpy(frame.data.data(), data, frameSize);

	_externalFrameQueue.push(frame);

	if (_externalFrameQueue.size() > 10)
	{
		_externalFrameQueue.pop();
	}

	return S_OK;
}

HRESULT FrameGenerator::GenerateFromExternal(IMFSample* sample, const GUID& format, IMFSample** outSample) {
	RETURN_HR_IF_NULL(E_POINTER, sample);
	RETURN_HR_IF_NULL(E_POINTER, outSample);
	*outSample = nullptr;

	// Try to get frame from local queue first
	ExternalFrame frame;
	bool hasFrame = false;
	{
		winrt::slim_lock_guard lock(_externalFrameLock);
		if (!_externalFrameQueue.empty())
		{
			frame = _externalFrameQueue.front();
			_externalFrameQueue.pop();
			hasFrame = true;
		}
	}

	if (!hasFrame)
	{
		extern GUID CLSID_VCam;
		PushedFrame apiFrame = {};

		if (GetExternalFrame(CLSID_VCam, apiFrame))
		{
			// Convert API frame to local format
			frame.data = apiFrame.data;
			frame.width = apiFrame.width;
			frame.height = apiFrame.height;
			frame.format = apiFrame.format;
			hasFrame = true;
			WINTRACE(L"FrameGenerator::GenerateFromExternal - Got frame from shared memory: %ux%u, format: %s, data size: %zu",
				frame.width, frame.height, GUID_ToStringW(frame.format).c_str(), frame.data.size());
		}
		else
		{
			WINTRACE(L"FrameGenerator::GenerateFromExternal - No frame in shared memory");
		}
	}
	else
	{
		WINTRACE(L"FrameGenerator::GenerateFromExternal - Got frame from local queue: %ux%u", frame.width, frame.height);
	}

	if (!hasFrame)
	{
		return MF_E_NOT_AVAILABLE;
	}

	// Ensure render target matches frame dimensions
	if (_width != frame.width || _height != frame.height)
	{
		RETURN_IF_FAILED(EnsureRenderTarget(frame.width, frame.height));
	}

	// Copy frame data to buffer
	wil::com_ptr_nothrow<IMFMediaBuffer> mediaBuffer;
	RETURN_IF_FAILED(sample->GetBufferByIndex(0, &mediaBuffer));

	wil::com_ptr_nothrow<IMF2DBuffer2> buffer2D;
	BYTE* scanline;
	LONG pitch;
	BYTE* start;
	DWORD length;
	RETURN_IF_FAILED(mediaBuffer->QueryInterface(IID_PPV_ARGS(&buffer2D)));
	RETURN_IF_FAILED(buffer2D->Lock2DSize(MF2DBuffer_LockFlags_Write, &scanline, &pitch, &start, &length));

	if (frame.format == MFVideoFormat_RGB32)
	{
		if (format == MFVideoFormat_RGB32)
		{
			// Direct copy
			UINT expectedStride = frame.width * 4;
			WINTRACE(L"FrameGenerator::GenerateFromExternal - Copying RGB32 frame: %ux%u, stride: %u (expected: %u), data size: %zu",
				frame.width, frame.height, pitch, expectedStride, frame.data.size());

			if (pitch == expectedStride)
			{
				memcpy(scanline, frame.data.data(), frame.data.size());
				WINTRACE(L"FrameGenerator::GenerateFromExternal - Direct copy completed");
			}
			else
			{
				// Copy line by line
				WINTRACE(L"FrameGenerator::GenerateFromExternal - Line-by-line copy (pitch mismatch)");
				for (UINT y = 0; y < frame.height; y++)
				{
					memcpy(scanline + y * pitch, frame.data.data() + y * expectedStride, expectedStride);
				}
			}
		}
		else if (format == MFVideoFormat_NV12)
		{
			// Convert RGB32 to NV12
			RETURN_IF_FAILED(RGB32ToNV12(frame.data.data(), (ULONG)frame.data.size(), frame.width * 4, frame.width, frame.height, scanline, length, pitch));
		}
	}
	else if (frame.format == MFVideoFormat_NV12 && format == MFVideoFormat_NV12)
	{
		// Direct copy NV12
		UINT expectedStride = frame.width;
		if (pitch == expectedStride)
		{
			memcpy(scanline, frame.data.data(), frame.data.size());
		}
		else
		{
			// Copy Y plane
			for (UINT y = 0; y < frame.height; y++)
			{
				memcpy(scanline + y * pitch, frame.data.data() + y * expectedStride, expectedStride);
			}
			// Copy UV plane
			BYTE* uvDest = scanline + frame.height * pitch;
			BYTE* uvSrc = frame.data.data() + frame.height * expectedStride;
			UINT uvHeight = frame.height / 2;
			for (UINT y = 0; y < uvHeight; y++)
			{
				memcpy(uvDest + y * pitch, uvSrc + y * expectedStride, expectedStride);
			}
		}
	}

	buffer2D->Unlock2D();

	_frame++;
	sample->AddRef();
	*outSample = sample;
	return S_OK;
}
