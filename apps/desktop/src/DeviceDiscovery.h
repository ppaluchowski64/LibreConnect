#pragma once
#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <QSet>
#include <string>

#include "DeviceModel.h"
#include <Scanner.h>
#include <ConnectionManager.h>
#include <boost/uuid/uuid_io.hpp>

class DeviceDiscovery : public QObject {
    Q_OBJECT
    Q_PROPERTY(DeviceModel* model READ model CONSTANT)
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)

public:
    explicit DeviceDiscovery(QObject* parent = nullptr)
        : QObject(parent)
    {
        ConnectionManager::StartAcceptingConnections();

        m_timer.setInterval(1200);
        m_timer.setSingleShot(false);
        connect(&m_timer, &QTimer::timeout,
                this, &DeviceDiscovery::onDiscoveryTimeout);
    }


    DeviceModel* model() { return &m_model; }
    bool searching() const { return m_searching; }

    Q_INVOKABLE void discover() {
        if (!m_searching) {
            m_model.clear();
            setSearching(true);

            LanDeviceScanner::BeginScan();
            m_timer.start();
            return;
        }

        onDiscoveryTimeout();
    }

    Q_INVOKABLE void cancelScan() {
        if (!m_searching)
            return;

        m_timer.stop();
        LanDeviceScanner::EndScan();
        setSearching(false);
    }

    Q_INVOKABLE QVariantMap deviceAt(int row) const {
        return m_model.get(row);
    }

    Q_INVOKABLE QVariantMap deviceById(const QString& id) const {
        const int rows = m_model.rowCount();
        for (int row = 0; row < rows; ++row) {
            const QVariantMap entry = m_model.get(row);
            if (entry.value("deviceId").toString() == id) {
                return entry;
            }
        }

        return {};
    }

signals:
    void searchingChanged();

private slots:
    void onDiscoveryTimeout() {
        if (!m_searching)
            return;

        const std::vector<DeviceInfo> devices = LanDeviceScanner::GetDiscoveredDevices();

        QSet<QString> addresses;
        for (const auto& dev : devices) {
            Device d;

            d.deviceId   = QString::fromStdString(boost::uuids::to_string(dev.deviceID));
            d.icon       = QStringLiteral("android.png");
            d.deviceName = QString::fromStdString(dev.deviceName);

            d.ipAddress  = QString::fromStdString(dev.deviceAddress);
            d.port       = static_cast<int>(dev.deviceAddressPort);

            d.osName     = QString::fromStdString(dev.osName.empty() ? std::string("Unknown") : dev.osName);
            d.osVersion  = QString::fromStdString(dev.osVersion);
            d.appVersion = QString::fromStdString(dev.appVersion);

            addresses.insert(d.ipAddress);
            m_model.upsertByAddress(d);
        }

        m_model.removeMissingByAddress(addresses);
    }

private:
    void setSearching(bool s) {
        if (m_searching == s)
            return;
        m_searching = s;
        emit searchingChanged();
    }

    DeviceModel m_model;
    bool m_searching = false;
    QTimer m_timer;
};
