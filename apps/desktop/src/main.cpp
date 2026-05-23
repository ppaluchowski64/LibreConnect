#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDir>
#include <QDirIterator>
#include <QStandardPaths>
#include <QtQml>
#include <QCommandLineParser>
#include <QFontDatabase>
#include <QFont>
#include <QIcon>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QIcon>
#include <filesystem>
#include <memory>
#include <boost/uuid/nil_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <ConnectionManager.h>
#include <DebugLog.h>
#include <Events.h>
#include "DeviceDiscovery.h"
#include "DeviceModel.h"
#include "DeviceConnectionController.h"
#include "NotificationSyncController.h"
#include "ClipboardSyncController.h"
#include "SmsBridgeController.h"
#include "PermissionStateController.h"
#include "FileManagerController.h"
#include "TemporaryStorageController.h"
#include "VirtualMicrophoneController.h"
#ifndef MACOS_DEVICE
#include "VirtualCameraController.h"
#endif
#include "ThemeController.h"
#include "MediaNotificationController.h"

namespace
{
void AttachQmlCreationLogging(QQmlApplicationEngine& engine, QApplication& app, const QUrl& url)
{
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() {
            Debug::LogError("Failed to create QML object");
            QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [url](QObject* obj, const QUrl& createdUrl) {
            if (createdUrl != url) {
                return;
            }

            if (!obj) {
                qDebug() << "Failed to load QML at" << createdUrl;
                return;
            }

            qDebug() << "Qml object created";
        },
        Qt::QueuedConnection);
}

class StartupConnectionListener final : public QObject
{
public:
    explicit StartupConnectionListener(QApplication& app)
        : m_app(app)
    {
    }

    void setMainWindow(QObject* mainWindow)
    {
        m_mainWindow = mainWindow;
    }

protected:
    bool event(QEvent* event) override
    {
        switch (event->type()) {
        case ConnectedEvent::Type: {
            if (!m_waitingForConnection) {
                return QObject::event(event);
            }

            const auto* connectedEvent = static_cast<ConnectedEvent*>(event);
            m_waitingForConnection = false;

            if (connectedEvent->GetResult() == EventResult::SUCCESS) {
                m_connectedSuccessfully = true;

                if (m_mainWindow) {
                    const auto peerId = ConnectionManager::GetPeerUUID();
                    const QString peerDeviceId = peerId == boost::uuids::nil_uuid()
                        ? QString{}
                        : QString::fromStdString(boost::uuids::to_string(peerId));

                    m_mainWindow->setProperty("activeDeviceId", peerDeviceId);
                    m_mainWindow->setProperty("activeDeviceName", QString::fromStdString(ConnectionManager::GetPeerDeviceName()));

                    QMetaObject::invokeMethod(
                        m_mainWindow,
                        [mainWindow = m_mainWindow]() {
                            if (!mainWindow) {
                                return;
                            }

                            mainWindow->setProperty("startupConnectionPending", false);
                        },
                        Qt::QueuedConnection);
                }
            } else {
                Debug::LogError("Startup connection failed");
                QMetaObject::invokeMethod(&m_app, []() {
                    QCoreApplication::exit(-1);
                }, Qt::QueuedConnection);
            }

            return true;
        }
        case DisconnectedEvent::Type:
            if (m_connectedSuccessfully) {
                Debug::Log("Startup parameter connection was lost, closing application");
                QMetaObject::invokeMethod(&m_app, &QCoreApplication::quit, Qt::QueuedConnection);
                return true;
            }

            return QObject::event(event);
        case ScannerErrorEvent::Type: {
            if (!m_waitingForConnection) {
                return QObject::event(event);
            }

            const auto* scannerErrorEvent = static_cast<ScannerErrorEvent*>(event);
            m_waitingForConnection = false;

            Debug::LogError(
                "Startup connection failed during initial handshake: {}",
                scannerErrorEvent->GetErrorCode().message());

            QMetaObject::invokeMethod(&m_app, []() {
                QCoreApplication::exit(-1);
            }, Qt::QueuedConnection);
            return true;
        }
        case DeviceNotPairedEvent::Type:
            if (!m_waitingForConnection) {
                return QObject::event(event);
            }

            m_waitingForConnection = false;
            Debug::LogError("Startup connection failed because devices are not paired");
            QMetaObject::invokeMethod(&m_app, []() {
                QCoreApplication::exit(-1);
            }, Qt::QueuedConnection);
            return true;
        case DeviceCooldownEvent::Type:
            if (!m_waitingForConnection) {
                return QObject::event(event);
            }

            m_waitingForConnection = false;
            Debug::LogError("Startup connection failed because the remote device is temporarily blocking new attempts");
            QMetaObject::invokeMethod(&m_app, []() {
                QCoreApplication::exit(-1);
            }, Qt::QueuedConnection);
            return true;
        default:
            return QObject::event(event);
        }
    }

private:
    QApplication& m_app;
    QPointer<QObject> m_mainWindow;
    bool m_waitingForConnection = true;
    bool m_connectedSuccessfully = false;
};

class GlobalConnectionListener final : public QObject
{
public:
    explicit GlobalConnectionListener(QQuickWindow* window)
        : m_window(window)
    {
    }

protected:
    bool event(QEvent* event) override
    {
        if (event->type() == DisconnectedEvent::Type) {
            if (m_window && !m_window->isVisible()) {
                Debug::Log("Disconnected while hidden, quitting.");
                QMetaObject::invokeMethod(QCoreApplication::instance(), &QCoreApplication::quit, Qt::QueuedConnection);
            }
        } else if (event->type() == ShowWindowEvent::Type) {
            if (m_window) {
                QMetaObject::invokeMethod(m_window, [window = m_window]() {
                    if (window) {
                        window->show();
                        window->setFlag(Qt::WindowStaysOnTopHint, true);
                        window->raise();
                        window->requestActivate();
                        window->setFlag(Qt::WindowStaysOnTopHint, false);
                    }
                }, Qt::QueuedConnection);
            }
        }
        return QObject::event(event);
    }

private:
    QPointer<QQuickWindow> m_window;
};
}

void LibreConnectLogHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QString logMessage = QString("[QT] %1").arg(msg);
    std::string stdMsg = logMessage.toStdString();

    switch (type) {
    case QtDebugMsg:
        Debug::Log(stdMsg);
        break;
    case QtInfoMsg:
        Debug::Log(stdMsg);
        break;
    case QtWarningMsg:
        Debug::LogWarning(stdMsg);
        break;
    case QtCriticalMsg:
        Debug::LogError(stdMsg);
        break;
    case QtFatalMsg:
        Debug::LogError(stdMsg);
        abort();
    }
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName("LibreConnect");
    app.setApplicationName("LibreConnect");
    app.setApplicationVersion(QStringLiteral(LIBRECONNECT_APP_VERSION));
    app.setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    const QCommandLineOption portOption(QStringList() << "p" << "port", "The port number to connect to.", "port", "-1");
    const QCommandLineOption addressOption(QStringList() << "a" << "address", "The address to connect to.", "address", "-1");
    const QCommandLineOption hiddenOption("hidden", "Start the application hidden in the system tray.");

    parser.addOption(portOption);
    parser.addOption(addressOption);
    parser.addOption(hiddenOption);
    parser.process(app);

    const QString port = parser.value(portOption);
    const QString address = parser.value(addressOption);

    app.setDesktopFileName(QStringLiteral("libreconnect"));
    app.setWindowIcon(QIcon(QStringLiteral(":/LibreConnect/desktop/libreconnect_logo.png")));

    const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appDataPath.isEmpty()) {
        QDir().mkpath(appDataPath);
        QDir::setCurrent(appDataPath);
        QDir::setCurrent(QCoreApplication::applicationDirPath());

        const Debug::Settings settings{
            .rootPath = appDataPath.toStdString(),
            .maxFileSize = 2 * 1024 * 1024 * 1024ULL,
            .maxLogFilesAmount = 10,
            .deleteLogsAfter = 60 * 60 * 24 * 7
        };

        try {
            Debug::SetSettings(settings);
        } catch (...) {}
    }

    std::filesystem::current_path(appDataPath.toStdString());

    qInstallMessageHandler(LibreConnectLogHandler);
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QFontDatabase::addApplicationFont(QStringLiteral(":/LibreConnect/desktop/Inter-VariableFont.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/LibreConnect/desktop/Inter-Italic-VariableFont.ttf"));
    app.setFont(QFont(QStringLiteral("Inter")));

    ThemeController themeController;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Theme"), &themeController);

    qmlRegisterType<DeviceDiscovery>("LibreConnect.desktop", 1, 0, "DeviceDiscovery");
    qmlRegisterType<DeviceModel>("LibreConnect.desktop", 1, 0, "DeviceModel");
    qmlRegisterType<DeviceConnectionController>("LibreConnect.desktop", 1, 0, "DeviceConnectionController");
    qmlRegisterType<NotificationSyncController>("LibreConnect.desktop", 1, 0, "NotificationSyncController");
    qmlRegisterType<ClipboardSyncController>("LibreConnect.desktop", 1, 0, "ClipboardSyncController");
    qmlRegisterType<SmsBridgeController>("LibreConnect.desktop", 1, 0, "SmsBridgeController");
    qmlRegisterType<PermissionStateController>("LibreConnect.desktop", 1, 0, "PermissionStateController");
    qmlRegisterType<FileManagerController>("LibreConnect.desktop", 1, 0, "FileManagerController");
    qmlRegisterType<TemporaryStorageController>("LibreConnect.desktop", 1, 0, "TemporaryStorageController");
    qmlRegisterType<VirtualMicrophoneController>("LibreConnect.desktop", 1, 0, "VirtualMicrophoneController");
#ifndef MACOS_DEVICE
    qmlRegisterType<VirtualCameraController>("LibreConnect.desktop", 1, 0, "VirtualCameraController");
#endif
    qmlRegisterType<MediaNotificationController>("LibreConnect.desktop", 1, 0, "MediaNotificationController");

    const QUrl url = QUrl("qrc:/LibreConnect/desktop/MainWindow.qml");
    AttachQmlCreationLogging(engine, app, url);

    const bool shouldBeHidden = parser.isSet(hiddenOption) || port != "-1" || address != "-1";
    const bool startupConnectionRequested = port != "-1" && address != "-1";
    std::unique_ptr<StartupConnectionListener> startupConnectionListener;

    if (shouldBeHidden) {
        QVariantMap initialProperties;
        initialProperties.insert(QStringLiteral("visible"), false);

        if (startupConnectionRequested) {
            startupConnectionListener = std::make_unique<StartupConnectionListener>(app);
            ConnectionManager::AddEventListener(QPointer<QObject>(startupConnectionListener.get()));
            initialProperties.insert(QStringLiteral("startupConnectionPending"), true);
            ConnectionManager::StartAcceptingConnections();
        }

        engine.setInitialProperties(initialProperties);
    }

    engine.load(url);

    if (!engine.rootObjects().isEmpty()) {
        QObject* rootObject = engine.rootObjects().constFirst();
        QQuickWindow* window = qobject_cast<QQuickWindow*>(rootObject);

        if (window) {
            QSystemTrayIcon* trayIcon = new QSystemTrayIcon(QIcon(":/LibreConnect/desktop/libreconnect_logo.png"), &app);
            QMenu* trayMenu = new QMenu();
            QAction* showAction = trayMenu->addAction("Show");
            QAction* quitAction = trayMenu->addAction("Quit");

            trayIcon->setContextMenu(trayMenu);
            trayIcon->show();

            QObject::connect(showAction, &QAction::triggered, window, [window]() {
                window->show();
                window->raise();
                window->requestActivate();
            });

            QObject::connect(quitAction, &QAction::triggered, &app, &QCoreApplication::quit);

            QObject::connect(trayIcon, &QSystemTrayIcon::activated, window, [window](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
                    window->show();
                    window->raise();
                    window->requestActivate();
                }
            });

            auto* globalListener = new GlobalConnectionListener(window);
            globalListener->setParent(&app);
            ConnectionManager::AddEventListener(QPointer<QObject>(globalListener));

            QObject::connect(window, &QQuickWindow::closing, &app, [window, &app](auto* close) {
                if (ConnectionManager::GetConnectionState() == ConnectionState::CONNECTED) {
                    reinterpret_cast<QObject*>(close)->setProperty("accepted", false);
                    window->hide();
                } else {
                    QCoreApplication::quit();
                }
            });

            if (startupConnectionRequested) {
                startupConnectionListener->setMainWindow(rootObject);
                ConnectionManager::Connect(address.toStdString(), port.toUInt(), InitialConnectionMode::CONNECT_WITH_PAIR);
            }

            if (!shouldBeHidden) {
                window->setVisible(true);
            }
        }
    }

    return app.exec();
}
