#include "Undocumented.h"
#include "Tools.h"
#include "EnumNames.h"
#include "MFTools.h"
#include "FrameGenerator.h"
#include "VCamAPI.h"

struct PushedFrame
{
	std::vector<BYTE> data;
};

extern "C++" __declspec(dllimport) bool HasCameraPendingExternalFrame(const GUID& clsid);
extern "C++" __declspec(dllimport) bool GetCameraExternalFrame(const GUID& clsid, PushedFrame& frame);

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

HRESULT FrameGenerator::GenerateFromExternal(IMFSample* sample, const GUID& clsid, const GUID& format, IMFSample** outSample) {
	RETURN_HR_IF_NULL(E_POINTER, sample);
	RETURN_HR_IF_NULL(E_POINTER, outSample);
	*outSample = nullptr;

	PushedFrame frame{};
	if (!GetCameraExternalFrame(clsid, frame))
	{
		WINTRACE(L"FrameGenerator::GenerateFromExternal - No frame in shared memory");
		return E_FAIL;
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

	const bool sourceRgb32 = (_format == MFVideoFormat_RGB32 || _format == MFVideoFormat_ARGB32);
	const bool sourceNv12 = (_format == MFVideoFormat_NV12);
	const bool outputRgb32 = (format == MFVideoFormat_RGB32 || format == MFVideoFormat_ARGB32);
	const bool outputNv12 = (format == MFVideoFormat_NV12);

	if (sourceRgb32)
	{
		if (outputRgb32)
		{
			// Direct copy
			UINT expectedStride = _width * 4;
			WINTRACE(L"FrameGenerator::GenerateFromExternal - Copying RGB32 frame: %ux%u, stride: %u (expected: %u), data size: %zu",
				_width, _height, pitch, expectedStride, frame.data.size());

			if (pitch == expectedStride)
			{
				memcpy(scanline, frame.data.data(), frame.data.size());
				WINTRACE(L"FrameGenerator::GenerateFromExternal - Direct copy completed");
			}
			else
			{
				// Copy line by line
				WINTRACE(L"FrameGenerator::GenerateFromExternal - Line-by-line copy (pitch mismatch)");
				for (UINT y = 0; y < _height; y++)
				{
					memcpy(scanline + y * pitch, frame.data.data() + y * expectedStride, expectedStride);
				}
			}
		}
		else if (outputNv12)
		{
			// Convert RGB32 to NV12
			RETURN_IF_FAILED(RGB32ToNV12(frame.data.data(), (ULONG)frame.data.size(), _width * 4, _width, _height, scanline, length, pitch));
		}
	}
	else if (sourceNv12 && outputNv12)
	{
		// Direct copy NV12
		UINT expectedStride = _width;
		if (pitch == expectedStride)
		{
			memcpy(scanline, frame.data.data(), frame.data.size());
		}
		else
		{
			// Copy Y plane
			for (UINT y = 0; y < _height; y++)
			{
				memcpy(scanline + y * pitch, frame.data.data() + y * expectedStride, expectedStride);
			}
			// Copy UV plane
			BYTE* uvDest = scanline + _height * pitch;
			BYTE* uvSrc = frame.data.data() + _height * expectedStride;
			UINT uvHeight = _height / 2;
			for (UINT y = 0; y < uvHeight; y++)
			{
				memcpy(uvDest + y * pitch, uvSrc + y * expectedStride, expectedStride);
			}
		}
	}
	else if (sourceNv12 && outputRgb32)
	{
		// Convert NV12 source from shared memory to RGB32 output sample.
		RETURN_IF_FAILED(NV12ToRGB32(frame.data.data(), static_cast<ULONG>(frame.data.size()), _width, _width, _height, scanline, length, pitch));
	}
	else
	{
		buffer2D->Unlock2D();
		return E_NOTIMPL;
	}

	buffer2D->Unlock2D();

	_frame++;
	sample->AddRef();
	*outSample = sample;
	return S_OK;
}
