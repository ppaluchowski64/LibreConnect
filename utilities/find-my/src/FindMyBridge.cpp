#include "FindMyBridge.h"

#ifdef ANDROID_DEVICE

    #include <QJniObject>
    #include <QString>
    #include <AndroidContextProvider.h>

    void FindMyBridge::StartAlert(const std::string& customUri) {
        const QJniObject context = AndroidContextProvider::GetAndroidContext();
        if (!context.isValid()) return;

        const QJniObject jUri = QJniObject::fromString(QString::fromStdString(customUri));

        QJniObject::callStaticMethod<void>(
            "com/LibreConnect/mobile/FindMyPhone",
            "startAlert",
            "(Landroid/content/Context;Ljava/lang/String;)V",
            context.object(),
            jUri.object<jstring>()
        );
    }

    void FindMyBridge::StopAlert() {
        const QJniObject context = AndroidContextProvider::GetAndroidContext();
        if (!context.isValid()) return;

        QJniObject::callStaticMethod<void>(
            "com/LibreConnect/mobile/FindMyPhone",
            "stopAlert",
            "(Landroid/content/Context;)V",
            context.object()
        );
    }

#endif

