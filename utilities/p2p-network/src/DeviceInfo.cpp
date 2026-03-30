#include <DeviceInfo.h>
#include <asio.hpp>
#include <ConnectionManager.h>
#include <DeviceData.h>
#include <fmt/format.h>

#ifdef ANDROID_DEVICE
#include <sys/system_properties.h>
#endif

DeviceInfo DeviceInfo::GetThisDeviceInfo() {
    const TCPEndpoint endpoint = ConnectionManager::GetSeekEndpoint();
    DeviceInfo device{};

#ifdef DESKTOP_DEVICE
    device.deviceName = asio::ip::host_name();
#elif defined(ANDROID_DEVICE)
    char model[PROP_VALUE_MAX];
    char manufacturer[PROP_VALUE_MAX];

    __system_property_get("ro.product.model", model);
    __system_property_get("ro.product.manufacturer", manufacturer);
    device.deviceName = fmt::format("{} {}", manufacturer, model);

#endif

    device.deviceID   = DeviceData::GetDeviceUUID();
    device.deviceAddressPort = endpoint.port();
    device.deviceType = DeviceTypeDetector::GetDeviceType();

    return device;
}
