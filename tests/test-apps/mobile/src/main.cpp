#include <QGuiApplication>
#include <QPointer>

#include <string>

#include <ConnectionManager.h>
#include <Events.h>
#include <ModulesManager.h>
#include <Scanner.h>

namespace {

class AutoAcceptListener : public QObject {
protected:
    bool event(QEvent* e) override {
        const auto type = e->type();

        if (type == ConnectionPendingEvent::Type) {
            auto* ev = static_cast<ConnectionPendingEvent*>(e);
            ev->AcceptConnection();
            return true;
        }

        if (type == ConnectionVerificationEvent::Type) {
            auto* ev = static_cast<ConnectionVerificationEvent*>(e);
            ev->SendAnswer(std::string{});
            return true;
        }

        if (type == ConnectionFailedVerificationEvent::Type) {
            return true;
        }

        if (type == ConnectedEvent::Type || type == DisconnectedEvent::Type) {
            return true;
        }

        if (type == ScannerErrorEvent::Type) {
            return true;
        }

        return QObject::event(e);
    }
};

} // namespace

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);

    AutoAcceptListener listener;
    ConnectionManager::AddEventListener(QPointer<QObject>(&listener));

    ConnectionManager::StartAcceptingConnections();
    LanDeviceScanner::BeginScan();
    ModulesManager::Initialize();

    return app.exec();
}
