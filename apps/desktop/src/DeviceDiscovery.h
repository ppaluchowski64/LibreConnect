#pragma once
#include <QObject>
#include <QTimer>
#include <QVariantMap>

#include "DeviceModel.h"
#include <Scanner.h>    // same include style as Scanner.cpp

class DeviceDiscovery : public QObject {
    Q_OBJECT
    Q_PROPERTY(DeviceModel* model READ model CONSTANT)
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)

public:
    explicit DeviceDiscovery(QObject* parent = nullptr)
        : QObject(parent)
    {
        // This timer defines how long we wait before pulling results
        m_timer.setInterval(1200);
        m_timer.setSingleShot(true);
        connect(&m_timer, &QTimer::timeout,
                this, &DeviceDiscovery::onDiscoveryTimeout);
    }

    DeviceModel* model() { return &m_model; }
    bool searching() const { return m_searching; }

    // Called from QML: start a scan
    Q_INVOKABLE void discover() {
        if (m_searching)
            return;  // already scanning

        // Clear previous results in the model
        m_model.clear();
        setSearching(true);

        // Kick off the actual LAN scan (async; returns immediately)
        LanDeviceScanner::BeginScan();

        // After 1.2s we'll pull discovered devices
        m_timer.start();
    }

    // Optional: allow QML to cancel a running scan if needed
    Q_INVOKABLE void cancelScan() {
        if (!m_searching)
            return;

        m_timer.stop();
        LanDeviceScanner::EndScan();
        setSearching(false);
    }

    // Convenience for QML if you want it
    Q_INVOKABLE QVariantMap deviceAt(int row) const {
        return m_model.get(row);
    }

signals:
    void searchingChanged();

private slots:
    void onDiscoveryTimeout() {
        // Get snapshot of currently discovered devices
        const std::vector<DeviceInfo> devices = LanDeviceScanner::GetDiscoveredDevices();

        for (const auto& dev : devices) {
            Device d;


            d.icon       = QStringLiteral("android.png");
            d.deviceName = QString::fromStdString(dev.deviceName);

            d.ipAddress  = QString::fromStdString(dev.deviceAddress);

            // jeszcze nie mamy ich w DeviceInfo
            d.osName     = QStringLiteral("Unknown");
            d.osVersion  = QStringLiteral("");  // or "—"
            d.appVersion = QStringLiteral("");  // or "—"

            m_model.append(d);
        }

        LanDeviceScanner::EndScan();

        setSearching(false);
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
