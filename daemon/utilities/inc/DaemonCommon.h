#ifndef DAEMON_COMMON_H
#define DAEMON_COMMON_H

#include <cstdint>
#include <AsioCommon.h>
#include <Packable.h>

enum class DaemonPackage : PackageTypeInt {
    CONNECTED,
    REQUEST_CONNECTED_WINDOW,
    REQUEST_CONNECTED_WINDOW_RESPONSE,
    SHOW_WINDOW_REQUEST
};

class DaemonUtils {
public:
    static void AddDeviceToAutoConnectList(const std::string& uuid);
    static void RemoveDeviceFromAutoConnectList(const std::string& uuid);

};

#endif //DAEMON_COMMON_H
