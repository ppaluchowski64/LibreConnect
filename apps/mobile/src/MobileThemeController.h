#pragma once

#include <QObject>
#include <QColor>
#include <QSettings>

class QStyleHints;

class MobileThemeController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(bool dark READ dark NOTIFY paletteChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor panelColor READ panelColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor panelBorderColor READ panelBorderColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor textColor READ textColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor mutedTextColor READ mutedTextColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor buttonColor READ buttonColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor selectedColor READ selectedColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor selectedTextColor READ selectedTextColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor accentColor READ accentColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor destructiveColor READ destructiveColor NOTIFY paletteChanged)

public:
    explicit MobileThemeController(QObject* parent = nullptr);

    QString mode() const;
    Q_INVOKABLE void setMode(const QString& mode);

    bool dark() const;
    QColor backgroundColor() const;
    QColor panelColor() const;
    QColor panelBorderColor() const;
    QColor textColor() const;
    QColor mutedTextColor() const;
    QColor buttonColor() const;
    QColor selectedColor() const;
    QColor selectedTextColor() const;
    QColor accentColor() const;
    QColor destructiveColor() const;

signals:
    void modeChanged();
    void paletteChanged();

private:
    QString normalizeMode(const QString& mode) const;
    bool isSystemDark() const;

    QString m_mode;
    QSettings m_settings;
};
