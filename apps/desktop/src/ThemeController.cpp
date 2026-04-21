#include "ThemeController.h"

#include <QGuiApplication>
#include <QStyleHints>

ThemeController::ThemeController(QObject* parent)
    : QObject(parent)
    , m_settings(QStringLiteral("LibreConnect"), QStringLiteral("LibreConnect"))
{
    m_mode = normalizeMode(m_settings.value(QStringLiteral("theme/mode"), QStringLiteral("system")).toString());

    QObject::connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this]() {
        emit paletteChanged();
    });
}

QString ThemeController::mode() const
{
    return m_mode;
}

void ThemeController::setMode(const QString& mode)
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

bool ThemeController::dark() const
{
    if (m_mode == QStringLiteral("dark")) {
        return true;
    }

    if (m_mode == QStringLiteral("light")) {
        return false;
    }

    return isSystemDark();
}

QColor ThemeController::backgroundColor() const
{
    return dark() ? QColor(QStringLiteral("#1C1C1C")) : QColor(QStringLiteral("#FFFFFF"));
}

QColor ThemeController::buttonColor() const
{
    return dark() ? QColor(QStringLiteral("#2B2B2B")) : QColor(QStringLiteral("#D9D9D9"));
}

QColor ThemeController::panelColor() const
{
    return dark() ? QColor(QStringLiteral("#242424")) : QColor(QStringLiteral("#F4F4F4"));
}

QColor ThemeController::panelBorderColor() const
{
    return dark() ? QColor(QStringLiteral("#3A3A3A")) : QColor(QStringLiteral("#D8D8D8"));
}

QColor ThemeController::textColor() const
{
    return dark() ? QColor(QStringLiteral("#F0F0F0")) : QColor(QStringLiteral("#111111"));
}

QColor ThemeController::mutedTextColor() const
{
    return dark() ? QColor(QStringLiteral("#BEBEBE")) : QColor(QStringLiteral("#444444"));
}

QColor ThemeController::subtleTextColor() const
{
    return dark() ? QColor(QStringLiteral("#9E9E9E")) : QColor(QStringLiteral("#666666"));
}

QColor ThemeController::selectedColor() const
{
    return dark() ? QColor(QStringLiteral("#2E4666")) : QColor(QStringLiteral("#DEEDFF"));
}

QColor ThemeController::selectedBorderColor() const
{
    return dark() ? QColor(QStringLiteral("#78A7E5")) : QColor(QStringLiteral("#7FAEDD"));
}

QColor ThemeController::selectedTextColor() const
{
    return dark() ? QColor(QStringLiteral("#FFFFFF")) : QColor(QStringLiteral("#111111"));
}

QColor ThemeController::successColor() const
{
    return dark() ? QColor(QStringLiteral("#5CD16A")) : QColor(QStringLiteral("#268D38"));
}

QColor ThemeController::destructiveFillColor() const
{
    return dark() ? QColor(QStringLiteral("#3A2328")) : QColor(QStringLiteral("#FDECEF"));
}

QColor ThemeController::destructiveFillHoverColor() const
{
    return dark() ? QColor(QStringLiteral("#4A2B32")) : QColor(QStringLiteral("#F9DCE3"));
}

bool ThemeController::isSystemDark() const
{
    return qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

QString ThemeController::normalizeMode(const QString& mode) const
{
    const QString lowered = mode.trimmed().toLower();
    if (lowered == QStringLiteral("light") || lowered == QStringLiteral("dark") || lowered == QStringLiteral("system")) {
        return lowered;
    }

    return QStringLiteral("system");
}
