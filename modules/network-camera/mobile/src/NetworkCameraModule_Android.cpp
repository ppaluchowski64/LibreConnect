#ifdef ANDROID_DEVICE

#include <NetworkCameraModule.h>
#include <CameraUtilities.h>
#include <QJniEnvironment>
#include <QJniObject>
#include <QJsonArray>
#include <qjsondocument.h>
#include <QJsonObject>
#include <QtCore/qcoreapplication_platform.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <set>
#include <tuple>

static std::mutex g_frameTargetMutex{};
static std::weak_ptr<NetworkCameraModule> g_frameTarget{};
static constexpr int64_t kFullHdPixels = 1920 * 1080;
static std::mutex g_accessUnitPoolMutex{};
static std::vector<std::vector<uint8_t>> g_accessUnitPool{};
static constexpr size_t kMaxAccessUnitPoolSize = 8;
static constexpr size_t kMaxReusableAccessUnitCapacity = 4 * 1024 * 1024;

static std::vector<uint8_t> AcquireAccessUnitBuffer(const size_t requiredSize) {
    std::vector<uint8_t> accessUnit;
    {
        std::lock_guard<std::mutex> lock(g_accessUnitPoolMutex);
        if (!g_accessUnitPool.empty()) {
            accessUnit = std::move(g_accessUnitPool.back());
            g_accessUnitPool.pop_back();
        }
    }

    if (accessUnit.capacity() < requiredSize) {
        accessUnit.reserve(requiredSize);
    }
    accessUnit.resize(requiredSize);
    return accessUnit;
}

static void RecycleAccessUnitBuffer(std::vector<uint8_t>&& accessUnit) {
    if (accessUnit.capacity() == 0 || accessUnit.capacity() > kMaxReusableAccessUnitCapacity) {
        return;
    }

    accessUnit.clear();

    std::lock_guard<std::mutex> lock(g_accessUnitPoolMutex);
    if (g_accessUnitPool.size() >= kMaxAccessUnitPoolSize) {
        return;
    }

    g_accessUnitPool.emplace_back(std::move(accessUnit));
}

static bool SplitLengthPrefixedNalUnitsWithLength(
    const uint8_t* data,
    const size_t size,
    const uint8_t nalLengthSize,
    std::vector<CameraUtilitiesLC::NalSpan>& out
) {
    if (!data || nalLengthSize < 1 || nalLengthSize > 4) {
        return false;
    }

    size_t offset = 0;
    while (offset + nalLengthSize <= size) {
        uint32_t nalSize = 0;
        for (uint8_t i = 0; i < nalLengthSize; ++i) {
            nalSize = (nalSize << 8) | data[offset + i];
        }
        offset += nalLengthSize;

        if (nalSize == 0 || offset + nalSize > size) {
            return false;
        }

        out.push_back({data + offset, nalSize});
        offset += nalSize;
    }

    return !out.empty() && offset == size;
}

static bool SplitLengthPrefixedNalUnits(
    const uint8_t* data,
    const size_t size,
    const uint8_t preferredNalLengthSize,
    std::vector<CameraUtilitiesLC::NalSpan>& out
) {
    std::vector<CameraUtilitiesLC::NalSpan> parsed;
    if (SplitLengthPrefixedNalUnitsWithLength(data, size, preferredNalLengthSize, parsed)) {
        out = std::move(parsed);
        return true;
    }

    constexpr uint8_t candidates[] = {4, 2, 1};
    for (const uint8_t candidate : candidates) {
        if (candidate == preferredNalLengthSize) {
            continue;
        }

        parsed.clear();
        if (SplitLengthPrefixedNalUnitsWithLength(data, size, candidate, parsed)) {
            out = std::move(parsed);
            return true;
        }
    }

    return false;
}

static bool UpsertHevcParameterSet(
    std::vector<std::vector<uint8_t>>& parameterSets,
    const uint8_t* data,
    const size_t size
) {
    if (!data || size < 2) {
        return false;
    }

    const uint8_t nalType = static_cast<uint8_t>((data[0] >> 1) & 0x3F);
    if (nalType < 32 || nalType > 34) {
        return false;
    }

    for (auto& existing : parameterSets) {
        if (existing.size() >= 2) {
            const uint8_t existingType = static_cast<uint8_t>((existing[0] >> 1) & 0x3F);
            if (existingType == nalType) {
                if (existing.size() == size && std::equal(existing.begin(), existing.end(), data)) {
                    return false;
                }
                existing.assign(data, data + size);
                return true;
            }
        }
    }

    parameterSets.emplace_back(data, data + size);
    return true;
}

static bool UpdateHevcParameterSetsFromNalSpans(
    std::vector<std::vector<uint8_t>>& parameterSets,
    const std::vector<CameraUtilitiesLC::NalSpan>& nals
) {
    bool updated = false;
    for (const auto& nal : nals) {
        updated = UpsertHevcParameterSet(parameterSets, nal.data, nal.size) || updated;
    }
    return updated;
}

extern "C" JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_MainService_nativeOnCameraEncodedSample(
    JNIEnv* env,
    jobject,
    const jobject encodedSample,
    const jint size,
    const jint flags,
    const jlong ptsUs
) {
    std::shared_ptr<NetworkCameraModule> module;
    {
        std::lock_guard<std::mutex> lock(g_frameTargetMutex);
        module = g_frameTarget.lock();
    }

    if (!module || !encodedSample || size <= 0) {
        return;
    }

    const uint8_t* encodedData = static_cast<const uint8_t*>(env->GetDirectBufferAddress(encodedSample));
    const jlong capacity = env->GetDirectBufferCapacity(encodedSample);
    if (!encodedData || capacity <= 0 || static_cast<jlong>(size) > capacity) {
        return;
    }

    std::vector<uint8_t> accessUnit = AcquireAccessUnitBuffer(static_cast<size_t>(size));
    std::memcpy(accessUnit.data(), encodedData, static_cast<size_t>(size));

    module->OnAndroidEncodedFrame(
        std::move(accessUnit),
        static_cast<int32_t>(flags),
        static_cast<int64_t>(ptsUs)
    );
}

QString NetworkCameraModule::QueryMainServiceCameraConfigurationsJson() {
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        Debug::LogWarning("NetworkCameraModule: Android context unavailable for Camera query");
        return {};
    }

    const QJniObject response = QJniObject::callStaticObjectMethod(
        "com/LibreConnect/mobile/MainService",
        "queryAvailableCameraConfigurations",
        "(Landroid/content/Context;)Ljava/lang/String;",
        context.object<jobject>()
    );

    if (!response.isValid()) {
        const QJniEnvironment env;
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        Debug::LogWarning("NetworkCameraModule: MainService Camera query returned invalid JNI response");
        return {};
    }

    return response.toString();
}

bool NetworkCameraModule::IsCameraFormatSupportedByCodec(const AVCodec* codec, const int width, const int height, const int requestedFps) {
    if (!codec) {
        return true;
    }

    const AVPixelFormat encoderPixelFormat = CameraUtilitiesLC::PickEncoderPixelFormat(codec, AV_PIX_FMT_YUV420P);
    if (encoderPixelFormat == AV_PIX_FMT_NONE) {
        return false;
    }

    return CameraUtilitiesLC::CanOpenEncoderWithFormat(
        codec,
        width,
        height,
        std::max(1, requestedFps),
        encoderPixelFormat
    );
}

std::vector<CameraSpecification> NetworkCameraModule::FetchCamerasSpecificationForCodec(const AVCodec* codec) {
        const QString payload = QueryMainServiceCameraConfigurationsJson();
        if (payload.isEmpty()) {
            return {};
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
            Debug::LogWarning(
                "NetworkCameraModule: Failed to parse Camera configuration JSON: {}",
                parseError.errorString().toStdString()
            );
            return {};
        }

        const QJsonArray cameraArray = document.array();
        std::vector<CameraSpecification> output;
        output.reserve(static_cast<size_t>(cameraArray.size()));

        for (const QJsonValue& cameraValue : cameraArray) {
            if (!cameraValue.isObject()) {
                continue;
            }

            const QJsonObject cameraObject = cameraValue.toObject();
            const QString cameraID = cameraObject.value("id").toString();
            if (cameraID.isEmpty()) {
                continue;
            }

            CameraSpecification specification;
            specification.id = cameraID.toStdString();
            specification.description = cameraObject.value("description").toString(cameraID).toStdString();
            specification.isDefault = cameraObject.value("isDefault").toBool(false);

            std::set<std::tuple<int32_t, int32_t, uint16_t>> uniqueFormats;
            const QJsonArray formatsArray = cameraObject.value("formats").toArray();
            for (const QJsonValue& formatValue : formatsArray) {
                if (!formatValue.isObject()) {
                    continue;
                }

                const QJsonObject formatObject = formatValue.toObject();
                const int width = formatObject.value("width").toInt(0);
                const int height = formatObject.value("height").toInt(0);
                const int fps = std::max(1, formatObject.value("framerate").toInt(0));

                if (width <= 0 || height <= 0) {
                    continue;
                }

                if (!IsCameraFormatSupportedByCodec(codec, width, height, fps)) {
                    continue;
                }

                uniqueFormats.emplace(
                    static_cast<int32_t>(width),
                    static_cast<int32_t>(height),
                    static_cast<uint16_t>(fps)
                );
            }

            specification.formats.reserve(uniqueFormats.size());
            for (const auto& [w, h, f] : uniqueFormats) {
                specification.formats.emplace_back(w, h, f);
            }

            if (!specification.formats.empty()) {
                output.emplace_back(std::move(specification));
            }
        }

        return output;
}

void NetworkCameraModule::OnAndroidEncodedFrame(std::vector<uint8_t> accessUnit, const int32_t flags, const int64_t ptsUs) {
    if (accessUnit.empty()) {
        RecycleAccessUnitBuffer(std::move(accessUnit));
        return;
    }

    if (!m_streamActive.load() || !m_videoStream) {
        RecycleAccessUnitBuffer(std::move(accessUnit));
        return;
    }

    if (!TryReserveFrameSlot("android-encoded")) {
        RecycleAccessUnitBuffer(std::move(accessUnit));
        return;
    }

    const uint64_t generation = m_streamGeneration.load();
    asio::co_spawn(
        m_moduleStrand,
        SendEncodedFrame(
            std::move(accessUnit),
            flags,
            ptsUs,
            generation
        ),
        asio::detached
    );
}

void NetworkCameraModule::UpdateMainServiceCameraRequest(const bool enabled) {
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        return;
    }

    const QJniObject intent(
        "android/content/Intent",
        "()V"
    );

    if (!intent.isValid()) {
        return;
    }

    const QJniObject packageName = context.callObjectMethod(
        "getPackageName",
        "()Ljava/lang/String;"
    );

    if (!packageName.isValid()) {
        return;
    }

    // ReSharper disable once CppExpressionWithoutSideEffects
    intent.callObjectMethod(
        "setClassName",
        "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
        packageName.object<jstring>(),
        QJniObject::fromString("com.LibreConnect.mobile.MainService").object<jstring>()
    );

    // ReSharper disable once CppExpressionWithoutSideEffects
    intent.callObjectMethod(
        "setAction",
        "(Ljava/lang/String;)Landroid/content/Intent;",
        QJniObject::fromString("com.LibreConnect.mobile.action.SET_CAMERA_REQUEST").object<jstring>()
    );

    intent.callObjectMethod(
        "putExtra",
        "(Ljava/lang/String;Z)Landroid/content/Intent;",
        QJniObject::fromString("com.LibreConnect.mobile.EXTRA_REQUEST_CAMERA").object<jstring>(),
        static_cast<jboolean>(enabled)
    );

    // ReSharper disable once CppExpressionWithoutSideEffects
    context.callObjectMethod(
        "startService",
        "(Landroid/content/Intent;)Landroid/content/ComponentName;",
        intent.object<jobject>()
    );
}

bool NetworkCameraModule::StartMainServiceCameraFrameReceiver(const std::string& cameraID, const int32_t width, const int32_t height, const int32_t fps, const int32_t bitrate) {
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        Debug::LogWarning("NetworkCameraModule: Cannot start Camera frame receiver (invalid Android context)");
        return false;
    }

    QJniObject cameraIdArg;
    jstring cameraIdJni = nullptr;
    if (!cameraID.empty()) {
        cameraIdArg = QJniObject::fromString(QString::fromStdString(cameraID));
        cameraIdJni = cameraIdArg.object<jstring>();
    }

    const jboolean started = QJniObject::callStaticMethod<jboolean>(
        "com/LibreConnect/mobile/MainService",
        "startCameraFrameReceiver",
        "(Landroid/content/Context;Ljava/lang/String;IIII)Z",
        context.object<jobject>(),
        cameraIdJni,
        static_cast<jint>(width),
        static_cast<jint>(height),
        static_cast<jint>(fps),
        static_cast<jint>(bitrate)
    );

    return started == JNI_TRUE;
}

void NetworkCameraModule::StopMainServiceCameraFrameReceiver() {
    QJniObject::callStaticMethod<void>(
        "com/LibreConnect/mobile/MainService",
        "stopCameraFrameReceiver",
        "()V"
    );
}

int32_t NetworkCameraModule::ComputeTargetBitrate(const int width, const int height, const int fps) {
    const int64_t pixels = static_cast<int64_t>(std::max(1, width)) * static_cast<int64_t>(std::max(1, height));
    const int64_t safeFps = std::max(1, fps);
    double targetBpp = 0.225;

    if (pixels > 2073600) {
        // HEVC high-res streams are significantly more sensitive to packet loss/backpressure.
        // Keep bitrate conservative to reduce RTP drops on typical Wi-Fi links.
        targetBpp = 0.07;
    } else if (pixels > 921600) {
        targetBpp = 0.18;
    }

    constexpr int64_t kMinBitrate = 2'000'000;
    const int64_t kMaxBitrate = (pixels > 2073600) ? 20'000'000 : 35'000'000;

    const int64_t pixelsPerSecond = pixels * safeFps;
    const int64_t bitrate = std::llround(static_cast<double>(pixelsPerSecond) * targetBpp);
    return static_cast<int32_t>(std::clamp<int64_t>(bitrate, kMinBitrate, kMaxBitrate));
}

void NetworkCameraModule::StartStream_Android(const size_t requestID, const std::string& cameraID, const CameraFormat requestedFormat) {
    const uint64_t generation = m_streamGeneration.fetch_add(1) + 1;

    if (generation != m_streamGeneration.load()) {
        ConnectionManager::SendRequestResponse(requestID, PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM_RESPONSE, StreamStartFailReason::InternalError);
        ProcessError(ModuleFailReason::InvalidState);
        return;
    }

    UpdateMainServiceCameraRequest(true);
    const int requestedWidth = static_cast<int>(requestedFormat.width);
    const int requestedHeight = static_cast<int>(requestedFormat.height);
    const int requestedFps = std::max(1, static_cast<int>(requestedFormat.framerate));
    if (requestedWidth <= 0 || requestedHeight <= 0) {
        ConnectionManager::SendRequestResponse(requestID, PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM_RESPONSE, StreamStartFailReason::IncorrectConfig);
        ProcessError(ModuleFailReason::IncorrectConfig);
        return;
    }

    const int32_t targetBitrate = ComputeTargetBitrate(requestedWidth, requestedHeight, requestedFps);
    const int64_t requestedPixels = static_cast<int64_t>(requestedWidth) * static_cast<int64_t>(requestedHeight);
    m_streamCodecId = (requestedPixels > kFullHdPixels) ? CodecID::H265 : CodecID::H264;
    Debug::Log(
        "Android MainService hardware encoder params: size={}x{}, fps={}, bitrate={} (auto), codec={}",
        requestedWidth,
        requestedHeight,
        requestedFps,
        targetBitrate,
        (m_streamCodecId == CodecID::H265 ? "H265" : "H264")
    );

    m_videoStream = std::make_shared<SRTP::Stream>(
        m_context,
        m_localKey,
        m_remoteKey,
        requestedFps,
        m_streamCodecId == CodecID::H265 ? SRTP::VideoCodec::H265 : SRTP::VideoCodec::H264
    );
    const auto peerAddr = ConnectionManager::GetPeerAddress();
    const auto peerPort = m_portNumber.load();
    Debug::Log("Binding SRTP to {}:{}", peerAddr.to_string(), peerPort);
    m_videoStream->Bind(UDPEndpoint(peerAddr, peerPort));

    m_h264ParameterSets.clear();
    m_h264LengthSize = 4;
    m_codecConfigSent = false;

    if (!StartMainServiceCameraFrameReceiver(
        cameraID,
        requestedWidth,
        requestedHeight,
        requestedFps,
        targetBitrate
    )) {
        Debug::LogWarning("NetworkCameraModule: Failed to start MainService Camera frame receiver");
        ConnectionManager::SendRequestResponse(requestID, PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM_RESPONSE, StreamStartFailReason::IncorrectConfig);
        ProcessError(ModuleFailReason::IncorrectConfig);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_frameTargetMutex);
        g_frameTarget = std::dynamic_pointer_cast<NetworkCameraModule>(shared_from_this());
    }

    m_ptsCounter = 0;
    m_streamActive.store(true);
    ConnectionManager::SendRequestResponse(requestID, PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM_RESPONSE, StreamStartFailReason::None);
}

void NetworkCameraModule::StopStream_Android() {
    UpdateMainServiceCameraRequest(false);
    StopMainServiceCameraFrameReceiver();

    {
        std::lock_guard<std::mutex> lock(g_frameTargetMutex);
        g_frameTarget = {};
    }
}


asio::awaitable<void> NetworkCameraModule::SendEncodedFrame(std::vector<uint8_t> accessUnit, const int32_t flags, const int64_t ptsUs, const uint64_t generation) {
    const auto accessUnitDeleter = [&accessUnit](const int* p) {
        (void)p;
        RecycleAccessUnitBuffer(std::move(accessUnit));
    };
    std::unique_ptr<int, decltype(accessUnitDeleter)> accessUnitGuard(reinterpret_cast<int*>(1), accessUnitDeleter);

    const auto slotDeleter = [this](const int* p) {
        (void)p;
        ReleaseFrameSlot();
    };

    std::unique_ptr<int, decltype(slotDeleter)> slotGuard(reinterpret_cast<int*>(1), slotDeleter);

    if (accessUnit.empty() || !m_streamActive.load() || generation != m_streamGeneration.load()) {
        co_return;
    }

    const std::shared_ptr<SRTP::Stream> stream = m_videoStream;
    if (!stream) {
        co_return;
    }

    constexpr int32_t kMediaCodecBufferFlagKeyFrame = 1;
    constexpr int32_t kMediaCodecBufferFlagCodecConfig = 2;
    const bool isKeyPacket = (flags & kMediaCodecBufferFlagKeyFrame) != 0;
    const bool isCodecConfigPacket = (flags & kMediaCodecBufferFlagCodecConfig) != 0;

    if (m_waitForKeyframeAfterDrop.load(std::memory_order_relaxed) && !isKeyPacket && !isCodecConfigPacket) {
        co_return;
    }
    if (isKeyPacket) {
        m_waitForKeyframeAfterDrop.store(false, std::memory_order_relaxed);
    }

    std::vector<CameraUtilitiesLC::NalSpan> nalSpans;
    if (CameraUtilitiesLC::IsAnnexB(accessUnit.data(), accessUnit.size())) {
        CameraUtilitiesLC::SplitAnnexB(accessUnit.data(), accessUnit.size(), nalSpans);
    } else {
        if (m_streamCodecId == CodecID::H264) {
            if (!CameraUtilitiesLC::SplitAvccAuto(accessUnit.data(), accessUnit.size(), m_h264LengthSize, nalSpans)) {
                SplitLengthPrefixedNalUnits(accessUnit.data(), accessUnit.size(), m_h264LengthSize, nalSpans);
            }
        } else {
            SplitLengthPrefixedNalUnits(accessUnit.data(), accessUnit.size(), m_h264LengthSize, nalSpans);
        }
    }

    if (nalSpans.empty()) {
        nalSpans.push_back({accessUnit.data(), accessUnit.size()});
    }

    const bool inBandUpdated = (m_streamCodecId == CodecID::H264)
        ? CameraUtilitiesLC::UpdateH264ParameterSetsFromNalSpans(m_h264ParameterSets, nalSpans)
        : UpdateHevcParameterSetsFromNalSpans(m_h264ParameterSets, nalSpans);
    if (inBandUpdated) {
        Debug::Log(
            "Captured {} parameter sets from MainService in-band NALs (count={})",
            m_streamCodecId == CodecID::H265 ? "H265" : "H264",
            m_h264ParameterSets.size()
        );
    }

    const uint32_t ts = stream->NextTimestamp();
    const bool shouldSendCodecConfig = !isCodecConfigPacket && (!m_codecConfigSent || isKeyPacket) && !m_h264ParameterSets.empty();
    const std::vector<std::vector<uint8_t>> codecConfigSnapshot = shouldSendCodecConfig
        ? m_h264ParameterSets
        : std::vector<std::vector<uint8_t>>{};

    if (shouldSendCodecConfig) {
        bool sentAllConfig = true;
        for (const auto& nal : codecConfigSnapshot) {
            if (!m_streamActive.load() || generation != m_streamGeneration.load()) {
                sentAllConfig = false;
                break;
            }
            co_await stream->AsyncSendNal(nal.data(), nal.size(), ts, false);
            if (!m_streamActive.load() || generation != m_streamGeneration.load()) {
                sentAllConfig = false;
                break;
            }
        }

        if (sentAllConfig && generation == m_streamGeneration.load()) {
            m_codecConfigSent = true;
        }
    }

    if (isCodecConfigPacket) {
        bool sentAllConfig = true;
        for (size_t i = 0; i < nalSpans.size(); ++i) {
            if (!m_streamActive.load() || generation != m_streamGeneration.load()) {
                sentAllConfig = false;
                break;
            }
            const bool marker = (i + 1 == nalSpans.size());
            co_await stream->AsyncSendNal(nalSpans[i].data, nalSpans[i].size, ts, marker);
            if (!m_streamActive.load() || generation != m_streamGeneration.load()) {
                sentAllConfig = false;
                break;
            }
        }

        if (sentAllConfig && generation == m_streamGeneration.load()) {
            m_codecConfigSent = true;
        }
        co_return;
    }

    for (size_t i = 0; i < nalSpans.size(); ++i) {
        if (!m_streamActive.load() || generation != m_streamGeneration.load()) {
            break;
        }
        const bool marker = (i + 1 == nalSpans.size());
        co_await stream->AsyncSendNal(nalSpans[i].data, nalSpans[i].size, ts, marker);
        if (!m_streamActive.load() || generation != m_streamGeneration.load()) {
            break;
        }
    }
}

#endif
