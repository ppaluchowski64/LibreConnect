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
    if (!QGuiApplication::instance() || text.empty())
        return false;

    {
        std::lock_guard lock(clipboardMutex);
        lastText = text;
    }

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
}

std::string TextClipboard::Get() {
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
}

bool TextClipboard::Has() {
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
}

void TextClipboard::AddClipboardUpdateListener(std::function<void()>&& callback) {
    if (!QGuiApplication::instance() || !callback)
        return;

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
    extern "C" JNIEXPORT void JNICALL
    Java_com_LibreConnect_mobile_ClipboardActionActivity_nativeOnClipboardTileClicked(JNIEnv* /*env*/, jobject /*obj*/) {
        std::function<void()> callback;

        {
            std::lock_guard lock(clipboardMutex);
            callback = currentCallback;
        }

        if (callback) {
            callback();
        }
    }
#endif
