#include "TextClipboard.h"

#include <string>
#include <cstdlib>
#include <cstring>
#include <cstdio>

#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>
#include <QString>

#ifdef __linux__
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

    #ifdef __linux__
        if (IsWayland() && HasWlClipboard()) {
            std::string cmd = "wl-copy";
            FILE* pipe = popen(cmd.c_str(), "w");
            if (!pipe)
                return false;

            fwrite(text.c_str(), 1, text.size(), pipe);
            pclose(pipe);
            return true;
        }
    #endif

    if (!QGuiApplication::instance() || text.empty())
        return false;

    QClipboard* const clipboard = QGuiApplication::clipboard();
    clipboard->setText(QString::fromUtf8(text.c_str()));

    return true;
}

std::string TextClipboard::Get() {
    #ifdef __linux__
        if (IsWayland() && HasWlClipboard()) {
            std::string result;
            char buffer[256];

            FILE* pipe = popen("wl-paste -n", "r");
            if (!pipe)
                return {};

            while (fgets(buffer, sizeof(buffer), pipe))
                result += buffer;

            pclose(pipe);
            return result;
        }
    #endif

    if (!QGuiApplication::instance())
        return {};

    const QClipboard* const clipboard = QGuiApplication::clipboard();
    const QString text = clipboard->text();

    return text.toStdString();
}

bool TextClipboard::Has() {
    #ifdef __linux__
        if (IsWayland() && HasWlClipboard()) {
            return std::system(
                "/bin/sh -c 'wl-paste -n >/dev/null 2>&1'"
            ) == 0;
        }
    #endif

    if (!QGuiApplication::instance())
        return false;

    const QClipboard* const clipboard = QGuiApplication::clipboard();
    const QMimeData* const mime = clipboard->mimeData();

    return mime->hasText();
}
