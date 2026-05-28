#include "BackendBridge.h"

#ifdef ANDROID_DEVICE
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>

namespace BackendBridge
{
namespace
{
QJniObject GetApplicationContext()
{
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        return {};
    }

    const QJniObject applicationContext = context.callObjectMethod(
        "getApplicationContext",
        "()Landroid/content/Context;"
    );
    return applicationContext.isValid() ? applicationContext : context;
}
}

QJsonObject ReadStateSnapshot()
{
    QString storageRoot;
    const QJniObject context = GetApplicationContext();
    if (context.isValid()) {
        const QJniObject filesDir = context.callObjectMethod(
            "getFilesDir",
            "()Ljava/io/File;"
        );
        if (filesDir.isValid()) {
            storageRoot = filesDir.callObjectMethod(
                "getAbsolutePath",
                "()Ljava/lang/String;"
            ).toString();
        }
    }

    if (storageRoot.isEmpty()) {
        storageRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }

    const QString statePath = storageRoot + QStringLiteral("/backend_state.json");
    QFile file(statePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return {};
    }

    return document.object();
}

static void BuildAndSendIntent(const char* action, std::function<void(QJniObject&)> configureExtras = {})
{
    const QJniObject context = GetApplicationContext();
    if (!context.isValid()) {
        return;
    }

    const QJniObject intent("android/content/Intent", "()V");
    if (!intent.isValid()) {
        return;
    }

    const QJniObject packageName = context.callObjectMethod("getPackageName", "()Ljava/lang/String;");
    intent.callObjectMethod(
        "setClassName",
        "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
        packageName.object<jstring>(),
        QJniObject::fromString(QStringLiteral("com.LibreConnect.mobile.MainService")).object<jstring>()
    );
    intent.callObjectMethod(
        "setAction",
        "(Ljava/lang/String;)Landroid/content/Intent;",
        QJniObject::fromString(QString::fromLatin1(action)).object<jstring>()
    );

    if (configureExtras) {
        QJniObject mutableIntent = intent;
        configureExtras(mutableIntent);
    }

    context.callObjectMethod(
        "startService",
        "(Landroid/content/Intent;)Landroid/content/ComponentName;",
        intent.object<jobject>()
    );
}

void SendAction(const char* action)
{
    BuildAndSendIntent(action);
}

void SendAction(const char* action, const char* extraKey, const bool value)
{
    BuildAndSendIntent(action, [extraKey, value](QJniObject& intent) {
        intent.callObjectMethod(
            "putExtra",
            "(Ljava/lang/String;Z)Landroid/content/Intent;",
            QJniObject::fromString(QString::fromLatin1(extraKey)).object<jstring>(),
            static_cast<jboolean>(value)
        );
    });
}

void SendAction(const char* action, const char* extraKey, const int value)
{
    BuildAndSendIntent(action, [extraKey, value](QJniObject& intent) {
        intent.callObjectMethod(
            "putExtra",
            "(Ljava/lang/String;I)Landroid/content/Intent;",
            QJniObject::fromString(QString::fromLatin1(extraKey)).object<jstring>(),
            static_cast<jint>(value)
        );
    });
}

void SendAction(const char* action, const char* extraKey, const double value)
{
    BuildAndSendIntent(action, [extraKey, value](QJniObject& intent) {
        intent.callObjectMethod(
            "putExtra",
            "(Ljava/lang/String;D)Landroid/content/Intent;",
            QJniObject::fromString(QString::fromLatin1(extraKey)).object<jstring>(),
            static_cast<jdouble>(value)
        );
    });
}

void SendAction(const char* action, const char* extraKey, const QString& value)
{
    BuildAndSendIntent(action, [extraKey, &value](QJniObject& intent) {
        intent.callObjectMethod(
            "putExtra",
            "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
            QJniObject::fromString(QString::fromLatin1(extraKey)).object<jstring>(),
            QJniObject::fromString(value).object<jstring>()
        );
    });
}

void SendAction(const char* action,
                const char* key1, const bool val1,
                const char* key2, const QString& val2)
{
    BuildAndSendIntent(action, [&](QJniObject& intent) {
        intent.callObjectMethod(
            "putExtra",
            "(Ljava/lang/String;Z)Landroid/content/Intent;",
            QJniObject::fromString(QString::fromLatin1(key1)).object<jstring>(),
            static_cast<jboolean>(val1)
        );
        intent.callObjectMethod(
            "putExtra",
            "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
            QJniObject::fromString(QString::fromLatin1(key2)).object<jstring>(),
            QJniObject::fromString(val2).object<jstring>()
        );
    });
}

void SendAction(const char* action,
                const char* key1, const int val1,
                const char* key2, const QString& val2,
                const char* key3, const int val3)
{
    BuildAndSendIntent(action, [&](QJniObject& intent) {
        intent.callObjectMethod(
            "putExtra",
            "(Ljava/lang/String;I)Landroid/content/Intent;",
            QJniObject::fromString(QString::fromLatin1(key1)).object<jstring>(),
            static_cast<jint>(val1)
        );
        intent.callObjectMethod(
            "putExtra",
            "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
            QJniObject::fromString(QString::fromLatin1(key2)).object<jstring>(),
            QJniObject::fromString(val2).object<jstring>()
        );
        intent.callObjectMethod(
            "putExtra",
            "(Ljava/lang/String;I)Landroid/content/Intent;",
            QJniObject::fromString(QString::fromLatin1(key3)).object<jstring>(),
            static_cast<jint>(val3)
        );
    });
}

}
#endif
