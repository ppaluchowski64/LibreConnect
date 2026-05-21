#include "MediaNotificationManager.h"

#include <QtCore/QJniObject>
#include <QtCore/QCoreApplication>

#include <mutex>

namespace {
    std::mutex g_callbackMutex;
    std::function<void(MediaSignal)> g_actionCallback;
    std::function<void(double)> g_seekCallback;

    MediaSignal KeyCodeToSignal(int keyCode) {
        switch (keyCode) {
            case 85:
                return MediaSignal::PlayPause;
            case 87:
                return MediaSignal::NextTrack;
            case 88:
                return MediaSignal::PreviousTrack;
            default:
                return MediaSignal::PlayPause;
        }
    }
}

extern "C" void LibreConnect_mediaTrackInfoJniAnchor();

extern "C" JNIEXPORT void JNICALL
Java_com_LibreConnect_mobile_MediaNotificationBridge_nativeOnMediaAction(JNIEnv* /*env*/, jclass /*clazz*/, jint keyCode) {
    std::function<void(MediaSignal)> callback; {
        std::lock_guard<std::mutex> lock(g_callbackMutex);
        callback = g_actionCallback;
    }

    if (callback)
        callback(KeyCodeToSignal(keyCode));
}

extern "C" JNIEXPORT void JNICALL
Java_com_LibreConnect_mobile_MediaNotificationBridge_nativeOnSeek(JNIEnv* /*env*/, jclass /*clazz*/, jdouble positionSeconds) {
    std::function<void(double)> callback; {
        std::lock_guard<std::mutex> lock(g_callbackMutex);
        callback = g_seekCallback;
    }

    if (callback)
        callback(positionSeconds);
}

void MediaNotificationManager::SetActionCallback(const std::function<void(MediaSignal)>& callback) {
    LibreConnect_mediaTrackInfoJniAnchor();

    std::lock_guard<std::mutex> lock(g_callbackMutex);
    g_actionCallback = callback;
}

void MediaNotificationManager::SetSeekCallback(const std::function<void(double)>& callback) {
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    g_seekCallback = callback;
}

void MediaNotificationManager::Show() {
    const QJniObject context = QNativeInterface::QAndroidApplication::context();

    if (!context.isValid())
        return;

    QJniObject::callStaticMethod<void>(
        "com/LibreConnect/mobile/MediaNotificationBridge",
        "show",
        "(Landroid/content/Context;)V",
        context.object()
    );
}

void MediaNotificationManager::Hide() {
    const QJniObject context = QNativeInterface::QAndroidApplication::context();

    if (!context.isValid())
        return;

    QJniObject::callStaticMethod<void>(
        "com/LibreConnect/mobile/MediaNotificationBridge",
        "hide",
        "(Landroid/content/Context;)V",
        context.object()
    );
}

void MediaNotificationManager::UpdateMetadata(const TrackMetadata& metadata) {
    const QJniObject context = QNativeInterface::QAndroidApplication::context();

    if (!context.isValid())
        return;

    QJniObject jTitle = QJniObject::fromString(QString::fromStdString(metadata.title));
    QJniObject jArtist = QJniObject::fromString(QString::fromStdString(metadata.artist));
    QJniObject jAlbum = QJniObject::fromString(QString::fromStdString(metadata.album));
    jlong jDuration = static_cast<jlong>(metadata.duration * 1000000.0);

    QJniObject jCoverData;

    if (!metadata.cover.empty()) {
        QJniEnvironment env;
        jbyteArray array = env->NewByteArray(metadata.cover.size());
        env->SetByteArrayRegion(array, 0, metadata.cover.size(), reinterpret_cast<const jbyte*>(metadata.cover.data()));
        jCoverData = QJniObject::fromLocalRef(array);
    }

    QJniObject::callStaticMethod<void>(
        "com/LibreConnect/mobile/MediaNotificationBridge",
        "updateMetadata",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;J[B)V",
        context.object(),
        jTitle.object<jstring>(),
        jArtist.object<jstring>(),
        jAlbum.object<jstring>(),
        jDuration,
        jCoverData.object<jbyteArray>()
    );
}

void MediaNotificationManager::UpdatePlaybackState(bool isPlaying, double position) {
    const QJniObject context = QNativeInterface::QAndroidApplication::context();

    if (!context.isValid())
        return;

    jlong jPosition = static_cast<jlong>(position * 1000000.0);

    QJniObject::callStaticMethod<void>(
        "com/LibreConnect/mobile/MediaNotificationBridge",
        "updatePlaybackState",
        "(Landroid/content/Context;ZJ)V",
        context.object(),
        static_cast<jboolean>(isPlaying),
        jPosition
    );
}
