#include <DeviceInfo.h>
#include <asio.hpp>
#include <ConnectionManager.h>
#include <DeviceData.h>
#include <CryptographicIdentityManager.h>
#include <QCoreApplication>
#include <fmt/format.h>

#ifdef ANDROID_DEVICE
#include <sys/system_properties.h>
#include <QJniObject>
#include <AndroidContextProvider.h>
#endif

DeviceInfo DeviceInfo::GetThisDeviceInfo() {
    constexpr std::string_view privateKeyPath{"certs/local/pkey.key"};
    constexpr std::string_view certificatePath{"certs/local/cert.key"};

    if (!CryptographicIdentityManager::IsCertificateValid(certificatePath)) {
        CryptographicIdentityManager::GenerateCertificate(privateKeyPath, certificatePath);
    }

    const TCPEndpoint endpoint = ConnectionManager::GetSeekEndpoint();
    DeviceInfo device{};

#ifdef DESKTOP_DEVICE
    device.deviceName = asio::ip::host_name();
    device.osName = "Desktop";
    device.osVersion.clear();
    device.appVersion = QCoreApplication::applicationVersion().toStdString();
#elif defined(ANDROID_DEVICE)
    char model[PROP_VALUE_MAX];
    char manufacturer[PROP_VALUE_MAX];
    char osVersion[PROP_VALUE_MAX];

    __system_property_get("ro.product.model", model);
    __system_property_get("ro.product.manufacturer", manufacturer);
    __system_property_get("ro.build.version.release", osVersion);
    device.deviceName = fmt::format("{} {}", manufacturer, model);
    device.osName = "Android";
    device.osVersion = osVersion;
    device.appVersion.clear();

    const QJniObject context = AndroidContextProvider::GetAndroidContext();
    if (context.isValid()) {
        const QJniObject packageManager = context.callObjectMethod(
            "getPackageManager",
            "()Landroid/content/pm/PackageManager;"
        );
        const QJniObject packageName = context.callObjectMethod(
            "getPackageName",
            "()Ljava/lang/String;"
        );
        if (packageManager.isValid() && packageName.isValid()) {
            const QJniObject packageInfo = packageManager.callObjectMethod(
                "getPackageInfo",
                "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;",
                packageName.object<jstring>(),
                0
            );
            if (packageInfo.isValid()) {
                const QJniObject versionName = packageInfo.getObjectField(
                    "versionName",
                    "Ljava/lang/String;"
                );
                if (versionName.isValid()) {
                    device.appVersion = versionName.toString().toStdString();
                }
            }
        }
    }

#endif

    device.deviceID   = DeviceData::GetDeviceUUID();
    device.deviceAddressPort = endpoint.port();
    device.deviceType = DeviceTypeDetector::GetDeviceType();
    device.certificateFingerprint = CryptographicIdentityManager::GetCertificateFingerprint(certificatePath);

    return device;
}
