#include <QtGlobal>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifdef Q_OS_LINUX
#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTextStream>
#endif

#ifdef Q_OS_MAC
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>
#endif

#ifdef Q_OS_ANDROID
#include <QGuiApplication>
#endif

#include <SystemInfo.h>

float SystemInfo::GetBatteryLevel() {
#if defined(Q_OS_WIN)
    SYSTEM_POWER_STATUS status;
    if (GetSystemPowerStatus(&status)) {
        if (status.BatteryLifePercent != 255) {
            return status.BatteryLifePercent;
        }
    }
#elif defined(Q_OS_ANDROID)
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) return -1;

    const QJniObject batteryServiceString = QJniObject::getStaticObjectField<jstring>(
        "android/content/Context", "BATTERY_SERVICE"
    );

    const QJniObject batteryManager = context.callObjectMethod(
        "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;",
        batteryServiceString.object<jstring>()
    );

    if (batteryManager.isValid()) {
        const jint level = batteryManager.callMethod<jint>("getIntProperty", "(I)I", 4);
        return static_cast<int>(level);
    }
#elif defined(Q_OS_LINUX)
    QDir dir("/sys/class/power_supply");
    QStringList batteries = dir.entryList(QStringList() << "BAT*", QDir::Dirs);

    if (!batteries.isEmpty()) {
        QFile file("/sys/class/power_supply/" + batteries.first() + "/capacity");
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            QString line = in.readLine();
            file.close();
            return line.toInt();
        }
    }

#elif defined(Q_OS_MAC)
    int capacity = -1;
    CFTypeRef blob = IOPSCopyPowerSourcesInfo();
    CFArrayRef sources = IOPSCopyPowerSourcesList(blob);

    if (CFArrayGetCount(sources) > 0) {
        CFDictionaryRef pSource = IOPSGetPowerSourceDescription(blob, CFArrayGetValueAtIndex(sources, 0));
        if (pSource) {
            CFNumberRef currentCapacity = (CFNumberRef)CFDictionaryGetValue(pSource, CFSTR(kIOPSCurrentCapacityKey));
            CFNumberRef maxCapacity = (CFNumberRef)CFDictionaryGetValue(pSource, CFSTR(kIOPSMaxCapacityKey));

            if (currentCapacity && maxCapacity) {
                int cur = 0, max = 0;
                CFNumberGetValue(currentCapacity, kCFNumberIntType, &cur);
                CFNumberGetValue(maxCapacity, kCFNumberIntType, &max);
                if (max > 0) capacity = (cur * 100) / max;
            }
        }
    }

    CFRelease(sources);
    CFRelease(blob);
    return capacity;
#endif

    return -1;
}
