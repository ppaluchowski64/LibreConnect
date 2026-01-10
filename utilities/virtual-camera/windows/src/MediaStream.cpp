#include "Undocumented.h"
#include "Tools.h"
#include "EnumNames.h"
#include "MFTools.h"
#include "FrameGenerator.h"
#include "MediaStream.h"
#include "MediaSource.h"

static void GetCameraConfig(const GUID& clsid, UINT& width, UINT& height, UINT& fps)
{
	const std::wstring keyPath = L"SOFTWARE\\LibreConnect_VirtualCamera_Configs\\" + GUID_ToStringW(clsid);
	HKEY hKey;
	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyPath.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS)
	{
		DWORD w = 0, h = 0, f = 0, size = sizeof(DWORD);
		if (RegQueryValueExW(hKey, L"Width", nullptr, nullptr, reinterpret_cast<LPBYTE>(&w), &size) == ERROR_SUCCESS) width = w;
		if (RegQueryValueExW(hKey, L"Height", nullptr, nullptr, reinterpret_cast<LPBYTE>(&h), &size) == ERROR_SUCCESS) height = h;
		if (RegQueryValueExW(hKey, L"Fps", nullptr, nullptr, reinterpret_cast<LPBYTE>(&f), &size) == ERROR_SUCCESS) fps = f;
		RegCloseKey(hKey);
	}
}

HRESULT MediaStream::Configure(const GUID& clsid)
{
    _clsid = clsid;
    GetCameraConfig(_clsid, _width, _height, _fps);

    if (_width == 0) _width = 640;
    if (_height == 0) _height = 480;
    if (_fps == 0) _fps = 30;

    auto types = wil::make_unique_cotaskmem_array<wil::com_ptr_nothrow<IMFMediaType>>(2);

    wil::com_ptr_nothrow<IMFMediaType> rgbType;
    RETURN_IF_FAILED(MFCreateMediaType(&rgbType));
    rgbType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    rgbType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    MFSetAttributeSize(rgbType.get(), MF_MT_FRAME_SIZE, _width, _height);
    rgbType->SetUINT32(MF_MT_DEFAULT_STRIDE, _width * 4);
    rgbType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    rgbType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    MFSetAttributeRatio(rgbType.get(), MF_MT_FRAME_RATE, _fps, 1);

    auto bitrate = static_cast<uint32_t>(_width * _height * 4 * 8 * _fps);
    rgbType->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
    MFSetAttributeRatio(rgbType.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    types[0] = rgbType.detach();

    // NV12 (Optional but recommended)
    if (types.size() > 1)
    {
        wil::com_ptr_nothrow<IMFMediaType> nv12Type;
        RETURN_IF_FAILED(MFCreateMediaType(&nv12Type));
        nv12Type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        nv12Type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        nv12Type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        nv12Type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
        MFSetAttributeSize(nv12Type.get(), MF_MT_FRAME_SIZE, _width, _height);
        nv12Type->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT>(_width * 1.5));
        MFSetAttributeRatio(nv12Type.get(), MF_MT_FRAME_RATE, _fps, 1);

        bitrate = static_cast<uint32_t>(_width * 1.5 * _height * 8 * _fps);
        nv12Type->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
        MFSetAttributeRatio(nv12Type.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        types[1] = nv12Type.detach();
    }

    RETURN_IF_FAILED_MSG(MFCreateStreamDescriptor(_index, static_cast<DWORD>(types.size()), types.get(), &_descriptor), "MFCreateStreamDescriptor failed");

    wil::com_ptr_nothrow<IMFMediaTypeHandler> handler;
    RETURN_IF_FAILED(_descriptor->GetMediaTypeHandler(&handler));
    RETURN_IF_FAILED(handler->SetCurrentMediaType(types[0]));

    return S_OK;
}

HRESULT MediaStream::Initialize(IMFMediaSource* source, int index)
{
	RETURN_HR_IF_NULL(E_POINTER, source);
	_source = source;
	_index = index;

	RETURN_IF_FAILED(SetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, PINNAME_VIDEO_CAPTURE));
	RETURN_IF_FAILED(SetUINT32(MF_DEVICESTREAM_STREAM_ID, index));
	RETURN_IF_FAILED(SetUINT32(MF_DEVICESTREAM_FRAMESERVER_SHARED, 1));
	RETURN_IF_FAILED(SetUINT32(MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES, MFFrameSourceTypes::MFFrameSourceTypes_Color));

	RETURN_IF_FAILED(MFCreateEventQueue(&_queue));

	return S_OK;
}

HRESULT MediaStream::Start(IMFMediaType* type)
{
	RETURN_HR_IF(MF_E_SHUTDOWN, !_queue || !_allocator);

	if (type)
	{
		RETURN_IF_FAILED(type->GetGUID(MF_MT_SUBTYPE, &_format));
	}

	RETURN_IF_FAILED(_generator.EnsureRenderTarget(_width, _height));

	RETURN_IF_FAILED(_allocator->InitializeSampleAllocator(10, type));
	RETURN_IF_FAILED(_queue->QueueEventParamVar(MEStreamStarted, GUID_NULL, S_OK, nullptr));
	_state = MF_STREAM_STATE_RUNNING;

	_sampleThreadRunning.store(true);
	_sampleThread = std::thread([this]() {
		this->SampleHandlerThread();
	});

	return S_OK;
}

HRESULT MediaStream::Stop()
{
	RETURN_HR_IF(MF_E_SHUTDOWN, !_queue || !_allocator);

	RETURN_IF_FAILED(_allocator->UninitializeSampleAllocator());
	RETURN_IF_FAILED(_queue->QueueEventParamVar(MEStreamStopped, GUID_NULL, S_OK, nullptr));
	_state = MF_STREAM_STATE_STOPPED;

	_sampleThreadRunning.store(false);
	if (_sampleThread.joinable()) {
		_sampleThread.join();
	}

	return S_OK;
}

MFSampleAllocatorUsage MediaStream::GetAllocatorUsage()
{
	return MFSampleAllocatorUsage_UsesProvidedAllocator;
}

HRESULT MediaStream::SetAllocator(IUnknown* allocator)
{
	RETURN_HR_IF_NULL(E_POINTER, allocator);
	_allocator.reset();
	RETURN_HR(allocator->QueryInterface(&_allocator));
}

HRESULT MediaStream::SetD3DManager(IUnknown* manager)
{
	RETURN_HR_IF_NULL(E_POINTER, manager);
	RETURN_IF_FAILED(_allocator->SetDirectXManager(manager));
	RETURN_IF_FAILED(_generator.SetD3DManager(manager, _width, _height));

	return S_OK;
}

void MediaStream::Shutdown()
{
	if (_queue)
	{
		LOG_IF_FAILED_MSG(_queue->Shutdown(), "Queue shutdown failed");
		_queue.reset();
	}

	_descriptor.reset();
	_source.reset();
	_attributes.reset();
}

// IMFMediaEventGenerator
STDMETHODIMP MediaStream::BeginGetEvent(IMFAsyncCallback* pCallback, IUnknown* punkState)
{
	//WINTRACE(L"MediaSource::BeginGetEvent");
	winrt::slim_lock_guard lock(_lock);
	RETURN_HR_IF(MF_E_SHUTDOWN, !_queue);

	RETURN_IF_FAILED(_queue->BeginGetEvent(pCallback, punkState));
	return S_OK;
}

STDMETHODIMP MediaStream::EndGetEvent(IMFAsyncResult* pResult, IMFMediaEvent** ppEvent)
{
	//WINTRACE(L"MediaStream::EndGetEvent");
	RETURN_HR_IF_NULL(E_POINTER, ppEvent);
	*ppEvent = nullptr;
	winrt::slim_lock_guard lock(_lock);
	RETURN_HR_IF(MF_E_SHUTDOWN, !_queue);

	RETURN_IF_FAILED(_queue->EndGetEvent(pResult, ppEvent));
	return S_OK;
}

STDMETHODIMP MediaStream::GetEvent(DWORD dwFlags, IMFMediaEvent** ppEvent)
{
	WINTRACE(L"MediaStream::GetEvent");
	RETURN_HR_IF_NULL(E_POINTER, ppEvent);
	*ppEvent = nullptr;
	winrt::slim_lock_guard lock(_lock);
	RETURN_HR_IF(MF_E_SHUTDOWN, !_queue);

	RETURN_IF_FAILED(_queue->GetEvent(dwFlags, ppEvent));
	return S_OK;
}

STDMETHODIMP MediaStream::QueueEvent(MediaEventType met, REFGUID guidExtendedType, HRESULT hrStatus, const PROPVARIANT* pvValue)
{
	WINTRACE(L"MediaStream::QueueEvent");
	winrt::slim_lock_guard lock(_lock);
	RETURN_HR_IF(MF_E_SHUTDOWN, !_queue);

	RETURN_IF_FAILED(_queue->QueueEventParamVar(met, guidExtendedType, hrStatus, pvValue));
	return S_OK;
}

// IMFMediaStream
STDMETHODIMP MediaStream::GetMediaSource(IMFMediaSource** ppMediaSource)
{
	WINTRACE(L"MediaSource::GetMediaSource");
	RETURN_HR_IF_NULL(E_POINTER, ppMediaSource);
	*ppMediaSource = nullptr;
	RETURN_HR_IF(MF_E_SHUTDOWN, !_source);

	RETURN_IF_FAILED(_source.copy_to(ppMediaSource));
	return S_OK;
}

STDMETHODIMP MediaStream::GetStreamDescriptor(IMFStreamDescriptor** ppStreamDescriptor)
{
	WINTRACE(L"MediaStream::GetStreamDescriptor");
	RETURN_HR_IF_NULL(E_POINTER, ppStreamDescriptor);
	*ppStreamDescriptor = nullptr;
	winrt::slim_lock_guard lock(_lock);
	RETURN_HR_IF(MF_E_SHUTDOWN, !_descriptor);

	RETURN_IF_FAILED(_descriptor.copy_to(ppStreamDescriptor));
	return S_OK;
}

#define CONTINUE_IF_FAILED(x) if(x != S_OK) continue

void MediaStream::SampleHandlerThread() {
	const float sleepTime = 1000/_fps;
	const LONGLONG fixedDuration = 10000000/_fps;
	while (_sampleThreadRunning.load()) {
		Sleep(sleepTime);

		{
			winrt::slim_lock_guard lock(_lock);

			if (!_allocator || !_queue) {
				return;
			}

			wil::com_ptr_nothrow<IMFSample> sample;
			CONTINUE_IF_FAILED(_allocator->AllocateSample(&sample));
			CONTINUE_IF_FAILED(sample->SetSampleTime(MFGetSystemTime()));
			CONTINUE_IF_FAILED(sample->SetSampleDuration(fixedDuration));

			wil::com_ptr_nothrow<IMFSample> outSample;
			if (FAILED(_generator.GenerateFromExternal(sample.get(), _clsid,  _format, &outSample))) {
				CONTINUE_IF_FAILED(_generator.Generate(sample.get(), _format, &outSample));
			}

			if (_pendingSample) {
				CONTINUE_IF_FAILED(outSample->SetUnknown(MFSampleExtension_Token, _pendingSample));
				_pendingSample->Release();
			}

			CONTINUE_IF_FAILED(_queue->QueueEventParamUnk(MEMediaSample, GUID_NULL, S_OK, outSample.get()));
		}
	}
}

STDMETHODIMP MediaStream::RequestSample(IUnknown* pToken)
{
	winrt::slim_lock_guard lock(_lock);

	if (_pendingSample != nullptr)
		return S_OK;

	if (pToken)
		pToken->AddRef();

	_pendingSample = pToken;
	return S_OK;
}

// IMFMediaStream2
STDMETHODIMP MediaStream::SetStreamState(const MF_STREAM_STATE value)
{
	WINTRACE(L"MediaStream::SetStreamState current:%u value:%u", _state, value);
	if (_state == value)
		return S_OK;
	switch (value)
	{
	case MF_STREAM_STATE_PAUSED:
		if (_state != MF_STREAM_STATE_RUNNING)
			RETURN_HR(MF_E_INVALID_STATE_TRANSITION);

		_state = value;
		break;

	case MF_STREAM_STATE_RUNNING:
		RETURN_IF_FAILED(Start(nullptr));
		break;

	case MF_STREAM_STATE_STOPPED:
		RETURN_IF_FAILED(Stop());
		break;

	default:
		RETURN_HR(MF_E_INVALID_STATE_TRANSITION);
		break;
	}
	return S_OK;
}

STDMETHODIMP MediaStream::GetStreamState(MF_STREAM_STATE* value)
{
	WINTRACE(L"MediaStream::GetStreamState state:%u", _state);
	RETURN_HR_IF_NULL(E_POINTER, value);
	*value = _state;
	return S_OK;
}

// IKsControl
STDMETHODIMP_(NTSTATUS) MediaStream::KsProperty(PKSPROPERTY property, ULONG length, LPVOID data, ULONG dataLength, ULONG* bytesReturned)
{
	WINTRACE(L"MediaStream::KsProperty len:%u data:%p dataLength:%u", length, data, dataLength);
	RETURN_HR_IF_NULL(E_POINTER, property);
	RETURN_HR_IF_NULL(E_POINTER, bytesReturned);
	winrt::slim_lock_guard lock(_lock);

	WINTRACE(L"MediaStream::KsProperty prop:%s", PKSIDENTIFIER_ToString(property, length).c_str());

	return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

STDMETHODIMP_(NTSTATUS) MediaStream::KsMethod(PKSMETHOD method, ULONG length, LPVOID data, ULONG dataLength, ULONG* bytesReturned)
{
	WINTRACE(L"MediaStream::KsMethod len:%u data:%p dataLength:%u", length, data, dataLength);
	RETURN_HR_IF_NULL(E_POINTER, method);
	RETURN_HR_IF_NULL(E_POINTER, bytesReturned);
	winrt::slim_lock_guard lock(_lock);

	WINTRACE(L"MediaStream::KsMethod method:%s", PKSIDENTIFIER_ToString(method, length).c_str());

	return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

STDMETHODIMP_(NTSTATUS) MediaStream::KsEvent(PKSEVENT evt, ULONG length, LPVOID data, ULONG dataLength, ULONG* bytesReturned)
{
	WINTRACE(L"MediaStream::KsEvent evt:%p len:%u data:%p dataLength:%u", evt, length, data, dataLength);
	RETURN_HR_IF_NULL(E_POINTER, bytesReturned);
	winrt::slim_lock_guard lock(_lock);

	WINTRACE(L"MediaStream::KsEvent event:%s", PKSIDENTIFIER_ToString(evt, length).c_str());
	return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}
