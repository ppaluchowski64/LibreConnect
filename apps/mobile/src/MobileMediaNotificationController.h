#pragma once

#include <QObject>
#include <QSettings>

class MobileMediaNotificationController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)

public:
    explicit MobileMediaNotificationController(QObject* parent = nullptr);
    ~MobileMediaNotificationController() override;

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);

signals:
    void enabledChanged();

private:
    void applyState();

    QSettings m_settings;
    bool m_enabled = false;
};
