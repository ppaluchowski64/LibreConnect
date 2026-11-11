#ifndef DAEMON_COMMON_H
#define DAEMON_COMMON_H

#include <cstdint>
#include <AsioCommon.h>
#include <Packable.h>

class QString;
class QJsonArray;

enum class DaemonPackage : PackageTypeInt {
    CONNECTED,
    REQUEST_CONNECTED_WINDOW,
    REQUEST_CONNECTED_WINDOW_RESPONSE,
    SHOW_WINDOW_REQUEST
};

class DaemonUtils {
public:
    static bool SetAutoConnect(const std::string& uuid, bool enabled);
    static bool IsAutoConnectEnabled(const std::string& uuid);
    static bool RegisterApplication();
    static QString ApplicationPath();
    static bool StartApplication(const QString& address, uint16_t port, const QString& deviceId, QString* error);
    static bool StartDaemon();

private:
    static QString listPath();
    static QString daemonPath();
    static QJsonArray readList(bool* valid = nullptr);
    static bool configureStartup(bool enabled);
};

#endif //DAEMON_COMMON_H
