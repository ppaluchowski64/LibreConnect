#include "MobileThemeController.h"

#include <QGuiApplication>
#include <QStyleHints>

MobileThemeController::MobileThemeController(QObject* parent)
    : QObject(parent)
    , m_settings(QStringLiteral("LibreConnect"), QStringLiteral("LibreConnectMobile"))
{
    m_mode = normalizeMode(m_settings.value(QStringLiteral("theme/mode"), QStringLiteral("system")).toString());

    QObject::connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this]() {
        emit paletteChanged();
    });
}

QString MobileThemeController::mode() const
{
    return m_mode;
}

void MobileThemeController::setMode(const QString& mode)
{
    const QString normalized = normalizeMode(mode);
    if (normalized == m_mode) {
        return;
    }

    m_mode = normalized;
    m_settings.setValue(QStringLiteral("theme/mode"), m_mode);
    emit modeChanged();
    emit paletteChanged();
}

bool MobileThemeController::dark() const
{
    if (m_mode == QStringLiteral("dark")) {
        return true;
    }

    if (m_mode == QStringLiteral("light")) {
        return false;
    }

    return isSystemDark();
}

QColor MobileThemeController::backgroundColor() const
{
    return dark() ? QColor(QStringLiteral("#121212")) : QColor(QStringLiteral("#FAFAFA"));
}

QColor MobileThemeController::panelColor() const
{
    return dark() ? QColor(QStringLiteral("#1E1E1E")) : QColor(QStringLiteral("#FFFFFF"));
}

QColor MobileThemeController::panelBorderColor() const
{
    return dark() ? QColor(QStringLiteral("#333333")) : QColor(QStringLiteral("#D9D9D9"));
}

QColor MobileThemeController::textColor() const
{
    return dark() ? QColor(QStringLiteral("#F0F0F0")) : QColor(QStringLiteral("#111111"));
}

QColor MobileThemeController::mutedTextColor() const
{
    return dark() ? QColor(QStringLiteral("#BBBBBB")) : QColor(QStringLiteral("#4A4A4A"));
}

QColor MobileThemeController::buttonColor() const
{
    return dark() ? QColor(QStringLiteral("#2A2A2A")) : QColor(QStringLiteral("#F0F0F0"));
}

QColor MobileThemeController::selectedColor() const
{
    return dark() ? QColor(QStringLiteral("#365A8D")) : QColor(QStringLiteral("#D6E6FF"));
}

QColor MobileThemeController::selectedTextColor() const
{
    return dark() ? QColor(QStringLiteral("#FFFFFF")) : QColor(QStringLiteral("#0B2342"));
}

QColor MobileThemeController::accentColor() const
{
    return dark() ? QColor(QStringLiteral("#8AB4F8")) : QColor(QStringLiteral("#1A73E8"));
}

QColor MobileThemeController::destructiveColor() const
{
    return dark() ? QColor(QStringLiteral("#FF8A80")) : QColor(QStringLiteral("#C62828"));
}

QString MobileThemeController::normalizeMode(const QString& mode) const
{
    const QString lowered = mode.trimmed().toLower();
    if (lowered == QStringLiteral("light") || lowered == QStringLiteral("dark") || lowered == QStringLiteral("system")) {
        return lowered;
    }

    return QStringLiteral("system");
}

bool MobileThemeController::isSystemDark() const
{
    return qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}
