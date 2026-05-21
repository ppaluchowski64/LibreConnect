#include "MediaTrackInfo.h"

#include <QtCore/QJniObject>
#include <mutex>
#include <chrono>

namespace {
    struct CachedState {
        TrackMetadata info;
        long long timestamp = 0;
        long long rawPosMicros = 0;
    };

    std::mutex g_mutex;
    std::optional<CachedState> g_state;
}

extern "C" JNIEXPORT void JNICALL
Java_com_LibreConnect_mobile_MediaTrackListenerService_nativeOnTrackUpdate(
    JNIEnv* env, jobject /*thiz*/,
    jstring jTitle, jstring jArtist, jstring jAlbum,
    jlong jDurationMicros, jlong jPositionMicros,
    jboolean jIsPlaying, jbyteArray jCoverData)
{
    CachedState state{};

    if (jTitle)
        state.info.title = QJniObject(jTitle).toString().toStdString();

    if (jArtist)
        state.info.artist = QJniObject(jArtist).toString().toStdString();

    if (jAlbum)
        state.info.album = QJniObject(jAlbum).toString().toStdString();

    state.info.duration = static_cast<double>(jDurationMicros) / 1000000.0;
    state.rawPosMicros = jPositionMicros;
    state.info.playing = static_cast<bool>(jIsPlaying);

    state.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    if (jCoverData) {
        jsize len = env->GetArrayLength(jCoverData);

        if (len > 0) {
            jbyte* bytes = env->GetByteArrayElements(jCoverData, nullptr);
            state.info.cover.assign(bytes, bytes + len);
            env->ReleaseByteArrayElements(jCoverData, bytes, JNI_ABORT);
        }
    }

    std::unique_lock<std::mutex> lock(g_mutex);
    g_state = std::move(state);
}

extern "C" void LibreConnect_mediaTrackInfoJniAnchor() {}

std::optional<TrackMetadata> MediaTrackInfo::GetCurrentTrack() {
    std::unique_lock<std::mutex> lock(g_mutex);

    if (!g_state)
        return std::nullopt;

    TrackMetadata info = g_state->info;

    double rawPos = static_cast<double>(g_state->rawPosMicros) / 1000000.0;

    info.position = CalculateInterpolatedPosition(
        rawPos,
        g_state->timestamp,
        info.playing
    );

    if (info.duration > 0.0 && info.position > info.duration)
        info.position = info.duration;

    return info;
}

void MediaTrackInfo::SetPosition(double seconds) {
    long long ms = static_cast<long long>(seconds * 1000.0);

    QJniObject::callStaticMethod<void>(
        "com/LibreConnect/mobile/MediaTrackListenerService",
        "setPosition",
        "(J)V",
        static_cast<jlong>(ms)
    );
}
