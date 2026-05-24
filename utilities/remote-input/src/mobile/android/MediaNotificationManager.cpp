#include "MediaNotificationManager.h"
#include "InputTypes.h"
#include "MediaTrackInfo.h"

#include <QtCore/QJniObject>
#include <QtCore/QJniEnvironment>
#include <QtCore/QCoreApplication>
#include <AndroidContextProvider.h>

namespace {
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

    QJniObject GetContext() {
        return AndroidContextProvider::GetAndroidContext();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_LibreConnect_mobile_MediaNotificationBridge_nativeOnMediaAction(JNIEnv* /*env*/, jclass /*clazz*/, jint keyCode) {
    MediaNotificationManager::InvokeAction(KeyCodeToSignal(keyCode));
}

extern "C" JNIEXPORT void JNICALL
Java_com_LibreConnect_mobile_MediaNotificationBridge_nativeOnSeek(JNIEnv* /*env*/, jclass /*clazz*/, jdouble positionSeconds) {
    MediaNotificationManager::InvokeSeek(positionSeconds);
}

void MediaNotificationManager::Show() {
    const QJniObject context = GetContext();

    if (!context.isValid())
        return;

    QJniEnvironment env;
    JNIEnv* jniEnv = env.jniEnv();
    if (!jniEnv) return;

    jclass bridgeClass = AndroidContextProvider::FindClass(jniEnv, "com/LibreConnect/mobile/MediaNotificationBridge");
    if (!bridgeClass) return;

    jmethodID method = jniEnv->GetStaticMethodID(bridgeClass, "show", "(Landroid/content/Context;)V");
    if (!method) {
        jniEnv->ExceptionClear();
        jniEnv->DeleteLocalRef(bridgeClass);
        return;
    }

    jniEnv->CallStaticVoidMethod(bridgeClass, method, context.object<jobject>());
    jniEnv->DeleteLocalRef(bridgeClass);

    if (jniEnv->ExceptionCheck()) {
        jniEnv->ExceptionDescribe();
        jniEnv->ExceptionClear();
    }
}

void MediaNotificationManager::Hide() {
    const QJniObject context = GetContext();

    if (!context.isValid())
        return;

    QJniEnvironment env;
    JNIEnv* jniEnv = env.jniEnv();
    if (!jniEnv) return;

    jclass bridgeClass = AndroidContextProvider::FindClass(jniEnv, "com/LibreConnect/mobile/MediaNotificationBridge");
    if (!bridgeClass) return;

    jmethodID method = jniEnv->GetStaticMethodID(bridgeClass, "hide", "(Landroid/content/Context;)V");
    if (!method) {
        jniEnv->ExceptionClear();
        jniEnv->DeleteLocalRef(bridgeClass);
        return;
    }

    jniEnv->CallStaticVoidMethod(bridgeClass, method, context.object<jobject>());
    jniEnv->DeleteLocalRef(bridgeClass);

    if (jniEnv->ExceptionCheck()) {
        jniEnv->ExceptionDescribe();
        jniEnv->ExceptionClear();
    }
}

void MediaNotificationManager::UpdateMetadata(const TrackMetadata& metadata) {
    const QJniObject context = GetContext();

    if (!context.isValid())
        return;

    QJniEnvironment env;
    JNIEnv* jniEnv = env.jniEnv();
    if (!jniEnv) return;

    jclass bridgeClass = AndroidContextProvider::FindClass(jniEnv, "com/LibreConnect/mobile/MediaNotificationBridge");
    if (!bridgeClass) return;

    jmethodID method = jniEnv->GetStaticMethodID(
        bridgeClass,
        "updateMetadata",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;J[B)V"
    );
    if (!method) {
        jniEnv->ExceptionClear();
        jniEnv->DeleteLocalRef(bridgeClass);
        return;
    }

    jstring jTitle = jniEnv->NewStringUTF(metadata.title.c_str());
    jstring jArtist = jniEnv->NewStringUTF(metadata.artist.c_str());
    jstring jAlbum = jniEnv->NewStringUTF(metadata.album.c_str());
    jlong jDuration = static_cast<jlong>(metadata.duration * 1000000.0);

    jbyteArray jCoverData = nullptr;
    if (!metadata.cover.empty()) {
        jCoverData = jniEnv->NewByteArray(metadata.cover.size());
        jniEnv->SetByteArrayRegion(jCoverData, 0, metadata.cover.size(), reinterpret_cast<const jbyte*>(metadata.cover.data()));
    }

    jniEnv->CallStaticVoidMethod(
        bridgeClass, method,
        context.object<jobject>(),
        jTitle, jArtist, jAlbum,
        jDuration, jCoverData
    );

    if (jCoverData) jniEnv->DeleteLocalRef(jCoverData);
    jniEnv->DeleteLocalRef(jAlbum);
    jniEnv->DeleteLocalRef(jArtist);
    jniEnv->DeleteLocalRef(jTitle);
    jniEnv->DeleteLocalRef(bridgeClass);

    if (jniEnv->ExceptionCheck()) {
        jniEnv->ExceptionDescribe();
        jniEnv->ExceptionClear();
    }
}

void MediaNotificationManager::UpdatePlaybackState(bool isPlaying, double position) {
    const QJniObject context = GetContext();

    if (!context.isValid())
        return;

    QJniEnvironment env;
    JNIEnv* jniEnv = env.jniEnv();
    if (!jniEnv) return;

    jclass bridgeClass = AndroidContextProvider::FindClass(jniEnv, "com/LibreConnect/mobile/MediaNotificationBridge");
    if (!bridgeClass) return;

    jmethodID method = jniEnv->GetStaticMethodID(bridgeClass, "updatePlaybackState", "(Landroid/content/Context;ZJ)V");
    if (!method) {
        jniEnv->ExceptionClear();
        jniEnv->DeleteLocalRef(bridgeClass);
        return;
    }

    jlong jPosition = static_cast<jlong>(position * 1000000.0);

    jniEnv->CallStaticVoidMethod(
        bridgeClass, method,
        context.object<jobject>(),
        static_cast<jboolean>(isPlaying),
        jPosition
    );
    jniEnv->DeleteLocalRef(bridgeClass);

    if (jniEnv->ExceptionCheck()) {
        jniEnv->ExceptionDescribe();
        jniEnv->ExceptionClear();
    }
}

bool MediaNotificationManager::IsVisible() {
    return false;
}