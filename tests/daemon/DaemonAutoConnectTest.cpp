#include <DaemonCommon.h>
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>

TEST(DaemonAutoConnect, LaunchesRegisteredApplicationWithConnectionArguments) {
    QTemporaryDir temp(QDir::current().filePath(QStringLiteral("daemon-test-XXXXXX")));
    ASSERT_TRUE(temp.isValid());
    ASSERT_TRUE(QDir().mkpath(temp.filePath(QStringLiteral("LibreConnectDaemonTest"))));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());

    ASSERT_FALSE(QCoreApplication::applicationFilePath().isEmpty());
    ASSERT_TRUE(DaemonUtils::RegisterApplication()) << QSettings().fileName().toStdString();
    EXPECT_EQ(DaemonUtils::ApplicationPath(), QCoreApplication::applicationFilePath());

    const QString folder = temp.filePath(QStringLiteral("directory with spaces"));
    ASSERT_TRUE(QDir().mkpath(folder));
    const QString executable = folder + QStringLiteral("/LibreConnect.exe");
    ASSERT_TRUE(QFile::copy(QCoreApplication::applicationFilePath(), executable));

    QSettings settings;
    settings.setValue(QStringLiteral("daemon/applicationPath"), executable);
    settings.sync();
    ASSERT_EQ(DaemonUtils::ApplicationPath(), QFileInfo(executable).absoluteFilePath());

    const QString markerPath = temp.filePath(QStringLiteral("arguments.txt"));
    qputenv("LIBRECONNECT_DAEMON_TEST_MARKER", markerPath.toLocal8Bit());
#ifdef Q_OS_WIN
    const QByteArray libraryVariable("PATH");
    const QByteArray separator(";");
#elif defined(Q_OS_MACOS)
    const QByteArray libraryVariable("DYLD_LIBRARY_PATH");
    const QByteArray separator(":");
#else
    const QByteArray libraryVariable("LD_LIBRARY_PATH");
    const QByteArray separator(":");
#endif
    const QByteArray previousLibraryPath = qgetenv(libraryVariable.constData());
    qputenv(libraryVariable.constData(), QCoreApplication::applicationDirPath().toLocal8Bit() + separator + previousLibraryPath);
    QString error;
    ASSERT_TRUE(DaemonUtils::StartApplication(QStringLiteral("192.0.2.1"), 43431,
        QStringLiteral("1fcae0ce-3917-4f69-9a56-ae4440d82ba7"), &error)) << error.toStdString();

    const QByteArray expected("--port\n43431\n--address\n192.0.2.1\n--device-id\n1fcae0ce-3917-4f69-9a56-ae4440d82ba7\n--hidden");
    QByteArray actual;
    for (int attempt = 0; attempt < 100 && actual != expected; ++attempt) {
        QFile marker(markerPath);
        if (marker.open(QIODevice::ReadOnly)) {
            actual = marker.readAll();
        }
        if (actual != expected) {
            QThread::msleep(50);
        }
    }
    EXPECT_EQ(actual, expected);

    settings.setValue(QStringLiteral("daemon/applicationPath"), temp.filePath(QStringLiteral("missing.exe")));
    settings.sync();
    EXPECT_TRUE(DaemonUtils::ApplicationPath().isEmpty());
    qunsetenv("LIBRECONNECT_DAEMON_TEST_MARKER");
    qputenv(libraryVariable.constData(), previousLibraryPath);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("LibreConnectDaemonTest"));
    app.setApplicationName(QStringLiteral("LibreConnectDaemonTest"));

    if (app.arguments().contains(QStringLiteral("--port"))) {
        QFile marker(QString::fromLocal8Bit(qgetenv("LIBRECONNECT_DAEMON_TEST_MARKER")));
        if (!marker.open(QIODevice::WriteOnly)) {
            return 1;
        }
        marker.write(app.arguments().mid(1).join(QStringLiteral("\n")).toUtf8());
        return 0;
    }

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
