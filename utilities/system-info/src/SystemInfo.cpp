#include <QtGlobal>
#include <mutex>

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
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>
#endif

#include <SystemInfo.h>

#ifdef Q_OS_ANDROID
namespace
{
std::mutex g_androidContextMutex;
JavaVM* g_androidContextVm = nullptr;
jobject g_androidContextGlobal = nullptr;

float TryGetBatteryLevelFromCachedContext()
{
    JavaVM* vm = nullptr;
    jobject contextGlobal = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_androidContextMutex);
        vm = g_androidContextVm;
        contextGlobal = g_androidContextGlobal;
    }

    if (!vm || !contextGlobal) {
        return -1;
    }

    JNIEnv* env = nullptr;
    bool attachedHere = false;
    const jint envStatus = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (envStatus == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            return -1;
        }
        attachedHere = true;
    } else if (envStatus != JNI_OK || !env) {
        return -1;
    }

    float batteryLevel = -1;
    const jobject localContext = env->NewLocalRef(contextGlobal);
    if (localContext) {
        jclass contextClass = env->FindClass("android/content/Context");
        if (contextClass) {
            const jfieldID batteryServiceField = env->GetStaticFieldID(
                contextClass,
                "BATTERY_SERVICE",
                "Ljava/lang/String;"
            );
            const jmethodID getSystemServiceMethod = env->GetMethodID(
                contextClass,
                "getSystemService",
                "(Ljava/lang/String;)Ljava/lang/Object;"
            );

            if (batteryServiceField && getSystemServiceMethod) {
                const auto batteryServiceString = static_cast<jstring>(env->GetStaticObjectField(contextClass, batteryServiceField));
                const jobject batteryManager = env->CallObjectMethod(localContext, getSystemServiceMethod, batteryServiceString);

                if (batteryServiceString) {
                    env->DeleteLocalRef(batteryServiceString);
                }

                if (batteryManager) {
                    jclass batteryManagerClass = env->FindClass("android/os/BatteryManager");
                    if (batteryManagerClass) {
                        const jfieldID capacityField = env->GetStaticFieldID(batteryManagerClass, "BATTERY_PROPERTY_CAPACITY", "I");
                        const jmethodID getIntPropertyMethod = env->GetMethodID(batteryManagerClass, "getIntProperty", "(I)I");

                        if (capacityField && getIntPropertyMethod) {
                            const jint capacityProperty = env->GetStaticIntField(batteryManagerClass, capacityField);
                            const jint level = env->CallIntMethod(batteryManager, getIntPropertyMethod, capacityProperty);
                            batteryLevel = static_cast<float>(level);
                        }

                        env->DeleteLocalRef(batteryManagerClass);
                    }

                    env->DeleteLocalRef(batteryManager);
                }
            }

            env->DeleteLocalRef(contextClass);
        }

        env->DeleteLocalRef(localContext);
    }

    if (attachedHere) {
        vm->DetachCurrentThread();
    }

    return batteryLevel;
}
}
#endif

float SystemInfo::GetBatteryLevel() {
#if defined(Q_OS_WIN)
    SYSTEM_POWER_STATUS status;
    if (GetSystemPowerStatus(&status)) {
        if (status.BatteryLifePercent != 255) {
            return status.BatteryLifePercent;
        }
    }
#elif defined(Q_OS_ANDROID)
    const float cachedContextResult = TryGetBatteryLevelFromCachedContext();
    if (cachedContextResult >= 0) {
        return cachedContextResult;
    }

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

#ifdef Q_OS_ANDROID
void SystemInfo::SetAndroidContext(JNIEnv* env, jobject context)
{
    if (!env || !context) {
        return;
    }

    env->GetJavaVM(&g_androidContextVm);

    jobject applicationContext = nullptr;
    jclass contextClass = env->GetObjectClass(context);
    if (contextClass) {
        const jmethodID getApplicationContextMethod = env->GetMethodID(
            contextClass,
            "getApplicationContext",
            "()Landroid/content/Context;"
        );
        if (getApplicationContextMethod) {
            applicationContext = env->CallObjectMethod(context, getApplicationContextMethod);
        }
        env->DeleteLocalRef(contextClass);
    }

    std::lock_guard<std::mutex> lock(g_androidContextMutex);
    if (g_androidContextGlobal) {
        env->DeleteGlobalRef(g_androidContextGlobal);
        g_androidContextGlobal = nullptr;
    }

    jobject contextToStore = applicationContext ? applicationContext : context;
    g_androidContextGlobal = env->NewGlobalRef(contextToStore);

    if (applicationContext) {
        env->DeleteLocalRef(applicationContext);
    }
}

void SystemInfo::ClearAndroidContext(JNIEnv* env)
{
    std::lock_guard<std::mutex> lock(g_androidContextMutex);
    if (env && g_androidContextGlobal) {
        env->DeleteGlobalRef(g_androidContextGlobal);
    }

    g_androidContextGlobal = nullptr;
    g_androidContextVm = nullptr;
}
#endif
