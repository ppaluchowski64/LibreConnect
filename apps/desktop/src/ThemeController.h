#pragma once

#include <QObject>
#include <QColor>
#include <QSettings>

class QStyleHints;

class ThemeController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(bool dark READ dark NOTIFY paletteChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor buttonColor READ buttonColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor panelColor READ panelColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor panelBorderColor READ panelBorderColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor textColor READ textColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor mutedTextColor READ mutedTextColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor subtleTextColor READ subtleTextColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor selectedColor READ selectedColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor selectedBorderColor READ selectedBorderColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor dangerColor READ dangerColor CONSTANT)
    Q_PROPERTY(QString fontFamily READ fontFamily CONSTANT)

public:
    explicit ThemeController(QObject* parent = nullptr);

    QString mode() const;
    Q_INVOKABLE
    void setMode(const QString& mode);

    bool dark() const;

    QColor backgroundColor() const;
    QColor buttonColor() const;
    QColor panelColor() const;
    QColor panelBorderColor() const;
    QColor textColor() const;
    QColor mutedTextColor() const;
    QColor subtleTextColor() const;
    QColor selectedColor() const;
    QColor selectedBorderColor() const;

    QColor dangerColor() const { return QColor(QStringLiteral("#B00020")); }
    QString fontFamily() const { return QStringLiteral("Inter"); }

signals:
    void modeChanged();
    void paletteChanged();

private:
    bool isSystemDark() const;
    QString normalizeMode(const QString& mode) const;

    QString m_mode;
    QSettings m_settings;
};
