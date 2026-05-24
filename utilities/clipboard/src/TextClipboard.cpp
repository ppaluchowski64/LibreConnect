#include "TextClipboard.h"

#include <string>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <mutex>

#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>
#include <QString>
#include <QObject>
#include <QProcess>
#include <QThread>
#include <QMetaObject>
#include <DebugLog.h>

#ifdef __ANDROID__
    #include <QJniObject>
    #include <AndroidContextProvider.h>
    extern "C" __attribute__((weak)) void UpdateLastRemoteClipboard(const std::string& text) {}
#endif

static QMetaObject::Connection clipboardConnection;
static std::string lastText;
static std::function<void()> currentCallback;
static std::mutex clipboardMutex;

#ifdef __ANDROID__
namespace
{
jclass FindClipboardBridge(JNIEnv* env)
{
    return AndroidContextProvider::FindClass(env, "com/LibreConnect/mobile/ClipboardBridge");
}

bool CallSetClipboardText(const QJniObject& context, const std::string& text)
{
    QJniEnvironment env;
    JNIEnv* jniEnv = env.jniEnv();
    if (!jniEnv || !context.isValid()) {
        return false;
    }

    jclass clipboardBridge = FindClipboardBridge(jniEnv);
    if (!clipboardBridge) {
        return false;
    }

    jmethodID method = jniEnv->GetStaticMethodID(
        clipboardBridge,
        "setClipboardText",
        "(Landroid/content/Context;Ljava/lang/String;)Z"
    );
    if (!method) {
        jniEnv->ExceptionClear();
        jniEnv->DeleteLocalRef(clipboardBridge);
        return false;
    }

    const QJniObject jText = QJniObject::fromString(QString::fromUtf8(text.c_str()));
    const bool result = jText.isValid() && jniEnv->CallStaticBooleanMethod(
        clipboardBridge,
        method,
        context.object<jobject>(),
        jText.object<jstring>()
    );
    if (jniEnv->ExceptionCheck()) {
        jniEnv->ExceptionClear();
    }
    jniEnv->DeleteLocalRef(clipboardBridge);
    return result;
}

std::string CallGetClipboardText(const QJniObject& context)
{
    QJniEnvironment env;
    JNIEnv* jniEnv = env.jniEnv();
    if (!jniEnv || !context.isValid()) {
        return {};
    }

    jclass clipboardBridge = FindClipboardBridge(jniEnv);
    if (!clipboardBridge) {
        return {};
    }

    jmethodID method = jniEnv->GetStaticMethodID(
        clipboardBridge,
        "getClipboardText",
        "(Landroid/content/Context;)Ljava/lang/String;"
    );
    if (!method) {
        jniEnv->ExceptionClear();
        jniEnv->DeleteLocalRef(clipboardBridge);
        return {};
    }

    jobject result = jniEnv->CallStaticObjectMethod(clipboardBridge, method, context.object<jobject>());
    if (jniEnv->ExceptionCheck()) {
        jniEnv->ExceptionClear();
        result = nullptr;
    }
    jniEnv->DeleteLocalRef(clipboardBridge);
    if (!result) {
        return {};
    }

    const QString text = QJniObject::fromLocalRef(result).toString();
    return text.toStdString();
}

bool CallHasClipboardText(const QJniObject& context)
{
    QJniEnvironment env;
    JNIEnv* jniEnv = env.jniEnv();
    if (!jniEnv || !context.isValid()) {
        return false;
    }

    jclass clipboardBridge = FindClipboardBridge(jniEnv);
    if (!clipboardBridge) {
        return false;
    }

    jmethodID method = jniEnv->GetStaticMethodID(
        clipboardBridge,
        "hasClipboardText",
        "(Landroid/content/Context;)Z"
    );
    if (!method) {
        jniEnv->ExceptionClear();
        jniEnv->DeleteLocalRef(clipboardBridge);
        return false;
    }

    const bool result = jniEnv->CallStaticBooleanMethod(clipboardBridge, method, context.object<jobject>());
    if (jniEnv->ExceptionCheck()) {
        jniEnv->ExceptionClear();
    }
    jniEnv->DeleteLocalRef(clipboardBridge);
    return result;
}

void CallSetClipboardListenerEnabled(const QJniObject& context, const bool enabled)
{
    QJniEnvironment env;
    JNIEnv* jniEnv = env.jniEnv();
    if (!jniEnv || !context.isValid()) {
        return;
    }

    jclass clipboardBridge = FindClipboardBridge(jniEnv);
    if (!clipboardBridge) {
        return;
    }

    jmethodID method = jniEnv->GetStaticMethodID(
        clipboardBridge,
        "setClipboardListenerEnabled",
        "(Landroid/content/Context;Z)V"
    );
    if (!method) {
        jniEnv->ExceptionClear();
        jniEnv->DeleteLocalRef(clipboardBridge);
        return;
    }

    jniEnv->CallStaticVoidMethod(
        clipboardBridge,
        method,
        context.object<jobject>(),
        static_cast<jboolean>(enabled)
    );
    if (jniEnv->ExceptionCheck()) {
        jniEnv->ExceptionClear();
    }
    jniEnv->DeleteLocalRef(clipboardBridge);
}
}
#endif

#if defined(__linux__) && !defined(__ANDROID__)
    static QProcess* wlpasteProcess = nullptr;

    bool TextClipboard::IsWayland() {
        if (QGuiApplication::instance()) {
            if (QGuiApplication::platformName() == "wayland")
                return true;
        }

        const char* session = std::getenv("XDG_SESSION_TYPE");

        if (session && std::strcmp(session, "wayland") == 0)
            return true;

        return std::getenv("WAYLAND_DISPLAY") != nullptr;
    }

    bool TextClipboard::HasWlClipboard() {
        static bool hasWl = []() {
            const bool found = std::system("/bin/sh -c 'command -v wl-copy >/dev/null 2>&1'") == 0 &&
                               std::system("/bin/sh -c 'command -v wl-paste >/dev/null 2>&1'") == 0;
            if (!found && IsWayland()) {
                Debug::LogWarning("Wayland detected but wl-copy/wl-paste not found. Clipboard sync might not work in background.");
            }
            return found;
        }();
        return hasWl;
    }
#endif

bool TextClipboard::Set(const std::string& text) {
    if (text.empty())
        return false;

    {
        std::lock_guard lock(clipboardMutex);
        lastText = text;
    }

#ifdef __ANDROID__
    if (AndroidContextProvider::HasServiceContext()) {
        UpdateLastRemoteClipboard(text);

        // Also attempt to set system clipboard from service context. This works
        // on Android 9 and below; on Android 10+ the frontend polling path handles it.
        const QJniObject context = AndroidContextProvider::GetAndroidContext();
        if (context.isValid()) {
            CallSetClipboardText(context, text);
        }

        return true;
    }

    const QJniObject context = AndroidContextProvider::GetAndroidContext();
    if (!context.isValid()) {
        return false;
    }

    return CallSetClipboardText(context, text);
#else
    if (!QGuiApplication::instance())
        return false;

    auto setLogic = [text]() {
        #if defined(__linux__) && !defined(__ANDROID__)
            if (IsWayland() && HasWlClipboard()) {
                if (FILE* pipe = popen("wl-copy", "w")) {
                    fwrite(text.c_str(), 1, text.size(), pipe);
                    pclose(pipe);
                    return;
                }
            }
        #endif

        QClipboard* const clipboard = QGuiApplication::clipboard();
        clipboard->setText(QString::fromUtf8(text.c_str()));
    };

    if (QThread::currentThread() != QGuiApplication::instance()->thread())
        QMetaObject::invokeMethod(QGuiApplication::instance(), setLogic, Qt::BlockingQueuedConnection);
    else
        setLogic();

    return true;
#endif
}

std::string TextClipboard::Get() {
#ifdef __ANDROID__
    const QJniObject context = AndroidContextProvider::GetAndroidContext();
    if (!context.isValid()) {
        return {};
    }

    return CallGetClipboardText(context);
#else
    if (!QGuiApplication::instance()) {
        #if defined(__linux__) && !defined(__ANDROID__)
            if (IsWayland() && HasWlClipboard()) {
                std::string result;
                char buffer[256];

                if (FILE* pipe = popen("wl-paste -n", "r")) {
                    while (fgets(buffer, sizeof(buffer), pipe)) {
                        result += buffer;
                    }

                    pclose(pipe);
                }

                return result;
            }
        #endif
        return {};
    }

    auto getLogic = []() -> std::string {
        #if defined(__linux__) && !defined(__ANDROID__)
            if (IsWayland() && HasWlClipboard()) {
                std::string result;
                char buffer[256];

                if (FILE* pipe = popen("wl-paste -n", "r")) {
                    while (fgets(buffer, sizeof(buffer), pipe)) {
                        result += buffer;
                    }

                    pclose(pipe);
                }

                return result;
            }
        #endif

        const QClipboard* const clipboard = QGuiApplication::clipboard();
        return clipboard->text().toStdString();
    };

    if (QThread::currentThread() != QGuiApplication::instance()->thread()) {
        std::string result;

        QMetaObject::invokeMethod(QGuiApplication::instance(), [&]() {
            result = getLogic();
        }, Qt::BlockingQueuedConnection);

        return result;
    }

    return getLogic();
#endif
}

bool TextClipboard::Has() {
#ifdef __ANDROID__
    const QJniObject context = AndroidContextProvider::GetAndroidContext();
    if (!context.isValid()) {
        return false;
    }

    return CallHasClipboardText(context);
#else
    if (!QGuiApplication::instance()) {
        #if defined(__linux__) && !defined(__ANDROID__)
            if (IsWayland() && HasWlClipboard())
                return std::system("/bin/sh -c 'wl-paste -n >/dev/null 2>&1'") == 0;
        #endif
        return false;
    }

    auto hasLogic = []() -> bool {
        #if defined(__linux__) && !defined(__ANDROID__)
            if (IsWayland() && HasWlClipboard())
                return std::system("/bin/sh -c 'wl-paste -n >/dev/null 2>&1'") == 0;
        #endif

        const QClipboard* const clipboard = QGuiApplication::clipboard();
        return clipboard->mimeData()->hasText();
    };

    if (QThread::currentThread() != QGuiApplication::instance()->thread()) {
        bool result = false;

        QMetaObject::invokeMethod(QGuiApplication::instance(), [&]() {
            result = hasLogic();
        }, Qt::BlockingQueuedConnection);

        return result;
    }

    return hasLogic();
#endif
}

void TextClipboard::AddClipboardUpdateListener(std::function<void()>&& callback) {
    if (!callback)
        return;

#ifndef __ANDROID__
    if (!QGuiApplication::instance()) {
        #if defined(__linux__) && !defined(__ANDROID__)
            if (!(IsWayland() && HasWlClipboard()))
        #endif
        return;
    }
#endif

    auto setupLogic = [cb = std::move(callback)]() mutable {
        RemoveClipboardUpdateListener();

        auto wrapper = [cb = std::move(cb)]() {
            std::string currentText = TextClipboard::Get();

            {
                std::lock_guard lock(clipboardMutex);
                if (currentText == lastText) {
                    return;
                }
            }

            cb();
        };

        #ifdef __ANDROID__
            {
                std::lock_guard lock(clipboardMutex);
                currentCallback = std::move(wrapper);
            }
            const QJniObject context = AndroidContextProvider::GetAndroidContext();
            if (context.isValid()) {
                CallSetClipboardListenerEnabled(context, true);
            }
            return;
        #endif

        #if defined(__linux__) && !defined(__ANDROID__)
            if (IsWayland() && HasWlClipboard()) {
                wlpasteProcess = new QProcess();

                QObject::connect(wlpasteProcess, &QProcess::readyReadStandardOutput, [wrapper]() {
                    wlpasteProcess->readAllStandardOutput();
                    wrapper();
                });

                wlpasteProcess->start("wl-paste", QStringList() << "--watch" << "echo" << "1");
                Debug::Log("TextClipboard: Started wl-paste --watch for Wayland clipboard sync");
                return;
            }
        #endif

        clipboardConnection = QObject::connect(
            QGuiApplication::clipboard(),
            &QClipboard::dataChanged,
            std::move(wrapper)
        );
    };

#ifdef __ANDROID__
    setupLogic();
#else
    if (QGuiApplication::instance() && QThread::currentThread() != QGuiApplication::instance()->thread())
        QMetaObject::invokeMethod(QGuiApplication::instance(), std::move(setupLogic), Qt::BlockingQueuedConnection);
    else
        setupLogic();
#endif
}

void TextClipboard::RemoveClipboardUpdateListener() {
    auto removeLogic = []() {
        #ifdef __ANDROID__
            {
                std::lock_guard lock(clipboardMutex);
                currentCallback = nullptr;
            }

            const QJniObject context = AndroidContextProvider::GetAndroidContext();
            if (context.isValid()) {
                CallSetClipboardListenerEnabled(context, false);
            }
        #endif

        #if defined(__linux__) && !defined(__ANDROID__)
            if (wlpasteProcess) {
                wlpasteProcess->kill();
                wlpasteProcess->waitForFinished();
                delete wlpasteProcess;
                wlpasteProcess = nullptr;
                Debug::Log("TextClipboard: Stopped wl-paste --watch");
            }
        #endif

        if (clipboardConnection) {
            QObject::disconnect(clipboardConnection);
            clipboardConnection = QMetaObject::Connection();
        }
    };

#ifdef __ANDROID__
    removeLogic();
#else
    if (QGuiApplication::instance() && QThread::currentThread() != QGuiApplication::instance()->thread())
        QMetaObject::invokeMethod(QGuiApplication::instance(), removeLogic, Qt::BlockingQueuedConnection);
    else
        removeLogic();
#endif
}

#ifdef __ANDROID__
    namespace {
        void DispatchClipboardCallback() {
            std::function<void()> callback;

            {
                std::lock_guard lock(clipboardMutex);
                callback = currentCallback;
            }

            if (callback) {
                callback();
            }
        }
    }

    extern "C" JNIEXPORT void JNICALL
    Java_com_LibreConnect_mobile_ClipboardBridge_nativeOnClipboardChanged(JNIEnv* /*env*/, jclass /*clazz*/) {
        DispatchClipboardCallback();
    }
#endif
