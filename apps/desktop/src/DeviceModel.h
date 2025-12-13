#pragma once
#include <QAbstractListModel>
#include <QVector>
#include <QString>
#include <QVariantMap>

struct Device {
    QString icon;
    QString deviceName;
    QString ipAddress;
    QString osName;
    QString osVersion;
    QString appVersion;
};

class DeviceModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        IconRole = Qt::UserRole + 1,
        DeviceNameRole,
        IpAddressRole,
        OsNameRole,
        OsVersionRole,
        AppVersionRole
    };
    Q_ENUM(Roles)

    explicit DeviceModel(QObject* parent = nullptr)
        : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        return parent.isValid() ? 0 : m_items.size();
    }

    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
            return {};
        const auto& d = m_items[index.row()];
        switch (role) {
        case IconRole:       return d.icon;
        case DeviceNameRole: return d.deviceName;
        case IpAddressRole:  return d.ipAddress;
        case OsNameRole:     return d.osName;
        case OsVersionRole:  return d.osVersion;
        case AppVersionRole: return d.appVersion;
        }
        return {};
    }

    QHash<int, QByteArray> roleNames() const override {
        return {
            { IconRole,       "icon" },
            { DeviceNameRole, "deviceName" },
            { IpAddressRole,  "ipAddress" },
            { OsNameRole,     "osName" },
            { OsVersionRole,  "osVersion" },
            { AppVersionRole, "appVersion" }
        };
    }

    Q_INVOKABLE QVariantMap get(int row) const {
        QVariantMap m;
        if (row < 0 || row >= m_items.size()) return m;
        const auto& d = m_items[row];
        m["icon"]        = d.icon;
        m["deviceName"]  = d.deviceName;
        m["ipAddress"]   = d.ipAddress;
        m["osName"]      = d.osName;
        m["osVersion"]   = d.osVersion;
        m["appVersion"]  = d.appVersion;
        return m;
    }

    void clear() {
        beginResetModel();
        m_items.clear();
        endResetModel();
    }

    void append(const Device& d) {
        const int pos = m_items.size();
        beginInsertRows(QModelIndex(), pos, pos);
        m_items.push_back(d);
        endInsertRows();
    }

private:
    QVector<Device> m_items;
};
