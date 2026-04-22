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

#ifdef __ANDROID__
    #include <QJniObject>
    #include <QtCore/qcoreapplication_platform.h>
#endif

static QMetaObject::Connection clipboardConnection;
static std::string lastText;
static std::function<void()> currentCallback;
static std::mutex clipboardMutex;

#if defined(__linux__) && !defined(__ANDROID__)
    static QProcess* wlpasteProcess = nullptr;

    bool TextClipboard::IsWayland() {
        const char* session = std::getenv("XDG_SESSION_TYPE");

        if (session && std::strcmp(session, "wayland") == 0)
            return true;

        return std::getenv("WAYLAND_DISPLAY") != nullptr;
    }

    bool TextClipboard::HasWlClipboard() {
        return std::system("/bin/sh -c 'command -v wl-copy >/dev/null 2>&1'") == 0 &&
               std::system("/bin/sh -c 'command -v wl-paste >/dev/null 2>&1'") == 0;
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
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        return false;
    }

    return QJniObject::callStaticMethod<jboolean>(
        "com/LibreConnect/mobile/ClipboardBridge",
        "setClipboardText",
        "(Landroid/content/Context;Ljava/lang/String;)Z",
        context.object<jobject>(),
        QJniObject::fromString(QString::fromUtf8(text.c_str())).object<jstring>()
    );
#else
    if (!QGuiApplication::instance())
        return false;

    auto setLogic = [text]() {
        #if defined(__linux__) && !defined(__ANDROID__)
            if (IsWayland() && HasWlClipboard()) {
                if (FILE* pipe = popen("wl-copy", "w")) {
                    fwrite(text.c_str(), 1, text.size(), pipe);
                    pclose(pipe);
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
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        return {};
    }

    const QJniObject text = QJniObject::callStaticObjectMethod(
        "com/LibreConnect/mobile/ClipboardBridge",
        "getClipboardText",
        "(Landroid/content/Context;)Ljava/lang/String;",
        context.object<jobject>()
    );

    if (!text.isValid()) {
        return {};
    }

    return text.toString().toStdString();
#else
    if (!QGuiApplication::instance())
        return {};

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
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        return false;
    }

    return QJniObject::callStaticMethod<jboolean>(
        "com/LibreConnect/mobile/ClipboardBridge",
        "hasClipboardText",
        "(Landroid/content/Context;)Z",
        context.object<jobject>()
    );
#else
    if (!QGuiApplication::instance())
        return false;

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
    if (!QGuiApplication::instance())
        return;
#endif

    RemoveClipboardUpdateListener();

    auto wrapper = [cb = std::move(callback)]() {
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
        const QJniObject context = QNativeInterface::QAndroidApplication::context();
        if (context.isValid()) {
            QJniObject::callStaticMethod<void>(
                "com/LibreConnect/mobile/ClipboardBridge",
                "setClipboardListenerEnabled",
                "(Landroid/content/Context;Z)V",
                context.object<jobject>(),
                true
            );
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
            return;
        }
    #endif

    clipboardConnection = QObject::connect(
        QGuiApplication::clipboard(),
        &QClipboard::dataChanged,
        wrapper
    );
}

void TextClipboard::RemoveClipboardUpdateListener() {
    #ifdef __ANDROID__
        {
            std::lock_guard lock(clipboardMutex);
            currentCallback = nullptr;
        }

        const QJniObject context = QNativeInterface::QAndroidApplication::context();
        if (context.isValid()) {
            QJniObject::callStaticMethod<void>(
                "com/LibreConnect/mobile/ClipboardBridge",
                "setClipboardListenerEnabled",
                "(Landroid/content/Context;Z)V",
                context.object<jobject>(),
                false
            );
        }
    #endif

    #if defined(__linux__) && !defined(__ANDROID__)
        if (wlpasteProcess) {
            wlpasteProcess->kill();
            wlpasteProcess->waitForFinished();
            delete wlpasteProcess;
            wlpasteProcess = nullptr;
        }
    #endif

    if (clipboardConnection) {
        QObject::disconnect(clipboardConnection);
        clipboardConnection = QMetaObject::Connection();
    }
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

    extern "C" JNIEXPORT void JNICALL
    Java_com_LibreConnect_mobile_ClipboardActionActivity_nativeOnClipboardTileClicked(JNIEnv* /*env*/, jobject /*obj*/) {
        DispatchClipboardCallback();
    }
#endif
