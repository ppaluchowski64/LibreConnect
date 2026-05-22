#ifndef DAEMON_COMMON_H
#define DAEMON_COMMON_H

#include <cstdint>
#include <AsioCommon.h>

enum class DaemonPackage : PackageTypeInt {
    CONNECTED,
    REQUEST_CONNECTED_WINDOW,
    REQUEST_CONNECTED_WINDOW_RESPONSE
};

#endif //DAEMON_COMMON_H
