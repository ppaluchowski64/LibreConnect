#include "MediaNotificationManager.h"
#include "InputTypes.h"

#include <mutex>

#ifdef __ANDROID__
    extern "C" void LibreConnect_mediaTrackInfoJniAnchor();
#endif

namespace {
    std::mutex g_callbackMutex;
    std::function<void(MediaSignal)> g_actionCallback;
    std::function<void(double)> g_seekCallback;
}

void MediaNotificationManager::SetActionCallback(const std::function<void(MediaSignal)>& callback) {
    #ifdef __ANDROID__
        LibreConnect_mediaTrackInfoJniAnchor();
    #endif

    std::lock_guard<std::mutex> lock(g_callbackMutex);
    g_actionCallback = callback;
}

void MediaNotificationManager::SetSeekCallback(const std::function<void(double)>& callback) {
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    g_seekCallback = callback;
}

void MediaNotificationManager::InvokeAction(MediaSignal signal) {
    std::function<void(MediaSignal)> cb;
    {
        std::lock_guard<std::mutex> lock(g_callbackMutex);
        cb = g_actionCallback;
    }

    if (cb)
        cb(signal);
}

void MediaNotificationManager::InvokeSeek(double position) {
    std::function<void(double)> cb;
    {
        std::lock_guard<std::mutex> lock(g_callbackMutex);
        cb = g_seekCallback;
    }

    if (cb)
        cb(position);
}
