#include "LanguageController.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QSettings>
#include <QStringList>
#include <QVariantMap>

#include <DebugLog.h>

namespace
{
constexpr const char* kTranslationPrefix = "libreconnect_desktop_";
constexpr const char* kTranslationRoot = ":/i18n";
constexpr const char* kLanguageSetting = "ui/language";
constexpr const char* kSourceLanguage = "en";

QString TranslationPath(const QString& languageCode)
{
    return QStringLiteral("%1/%2%3.qm")
        .arg(QString::fromLatin1(kTranslationRoot),
             QString::fromLatin1(kTranslationPrefix),
             languageCode);
}

QString NativeLanguageDisplayName(const QString& languageCode)
{
    if (languageCode == QLatin1String("en")) {
        return QStringLiteral("English");
    }

    const QLocale locale(languageCode);
    const QString nativeName = locale.nativeLanguageName();
    if (!nativeName.isEmpty()) {
        return nativeName;
    }

    return QLocale::languageToString(locale.language());
}
}

LanguageController::LanguageController(QObject* parent)
    : QObject(parent)
{
    m_currentLanguage = NormalizeLanguageCode(
        QSettings().value(QString::fromLatin1(kLanguageSetting)).toString());
    m_installedLanguage = ResolveEffectiveLanguage(m_currentLanguage);
    m_effectiveLanguage = m_installedLanguage;

    QLocale::setDefault(QLocale(m_installedLanguage));
    InstallTranslator(m_installedLanguage);

    Debug::Log(
        "LanguageController: started with requested='{}', effective='{}'",
        m_currentLanguage.toStdString(),
        m_installedLanguage.toStdString()
    );
}

LanguageController::~LanguageController()
{
    if (m_translatorInstalled) {
        QCoreApplication::removeTranslator(&m_translator);
        m_translatorInstalled = false;
    }
}

QString LanguageController::currentLanguage() const
{
    return m_currentLanguage;
}

QString LanguageController::effectiveLanguage() const
{
    return m_effectiveLanguage;
}

bool LanguageController::restartRequired() const
{
    return m_restartRequired;
}

QVariantList LanguageController::availableLanguages() const
{
    QVariantList languages;

    QVariantMap systemEntry;
    systemEntry.insert(QStringLiteral("code"), QString());
    systemEntry.insert(
        QStringLiteral("name"),
        tr("System default (%1)").arg(QLocale::system().nativeLanguageName())
    );
    languages.append(systemEntry);

    QStringList languageCodes;
    languageCodes.append(QString::fromLatin1(kSourceLanguage));

    const QString prefix = QString::fromLatin1(kTranslationPrefix);
    const QDir translationDir(QString::fromLatin1(kTranslationRoot));
    const QStringList translationFiles = translationDir.entryList(
        QStringList() << (prefix + QStringLiteral("*.qm")),
        QDir::Files,
        QDir::Name
    );

    for (const QString& fileName : translationFiles) {
        if (!fileName.startsWith(prefix) || !fileName.endsWith(QStringLiteral(".qm"))) {
            continue;
        }

        const QString languageCode = fileName.mid(
            prefix.size(),
            fileName.size() - prefix.size() - 3
        );

        if (!languageCode.isEmpty() && !languageCodes.contains(languageCode)) {
            languageCodes.append(languageCode);
        }
    }

    languageCodes.sort(Qt::CaseInsensitive);

    for (const QString& languageCode : languageCodes) {
        QVariantMap entry;
        entry.insert(QStringLiteral("code"), languageCode);
        entry.insert(QStringLiteral("name"), NativeLanguageDisplayName(languageCode));
        languages.append(entry);
    }

    return languages;
}

void LanguageController::setLanguage(const QString& code)
{
    const QString normalizedCode = NormalizeLanguageCode(code);
    if (normalizedCode == m_currentLanguage) {
        return;
    }

    QSettings settings;
    if (normalizedCode.isEmpty()) {
        settings.remove(QString::fromLatin1(kLanguageSetting));
    } else {
        settings.setValue(QString::fromLatin1(kLanguageSetting), normalizedCode);
    }

    m_currentLanguage = normalizedCode;
    m_effectiveLanguage = ResolveEffectiveLanguage(normalizedCode);

    const bool restartRequired = m_effectiveLanguage != m_installedLanguage;
    Debug::Log(
        "LanguageController: language changed to requested='{}', effective='{}', restartRequired={}",
        m_currentLanguage.toStdString(),
        m_effectiveLanguage.toStdString(),
        restartRequired
    );

    emit currentLanguageChanged();

    if (m_restartRequired != restartRequired) {
        m_restartRequired = restartRequired;
        emit restartRequiredChanged();
    }
}

QString LanguageController::NormalizeLanguageCode(const QString& code)
{
    QString normalized = code.trimmed();
    normalized.replace(QLatin1Char('-'), QLatin1Char('_'));

    if (normalized.compare(QStringLiteral("system"), Qt::CaseInsensitive) == 0 ||
        normalized.compare(QStringLiteral("C"), Qt::CaseInsensitive) == 0 ||
        normalized.compare(QStringLiteral("POSIX"), Qt::CaseInsensitive) == 0) {
        return {};
    }

    return normalized;
}

QString LanguageController::SystemLanguageCode()
{
    return NormalizeLanguageCode(QLocale::system().name());
}

QString LanguageController::ResolveEffectiveLanguage(const QString& requestedCode)
{
    const QString requested = NormalizeLanguageCode(requestedCode);
    if (!requested.isEmpty() && IsTranslationAvailable(requested)) {
        return requested;
    }

    const QString systemLanguage = SystemLanguageCode();
    if (!systemLanguage.isEmpty() && IsTranslationAvailable(systemLanguage)) {
        return systemLanguage;
    }

    const QString systemBaseLanguage = systemLanguage.section(QLatin1Char('_'), 0, 0);
    if (!systemBaseLanguage.isEmpty() &&
        systemBaseLanguage != systemLanguage &&
        IsTranslationAvailable(systemBaseLanguage)) {
        return systemBaseLanguage;
    }

    return QString::fromLatin1(kSourceLanguage);
}

bool LanguageController::IsTranslationAvailable(const QString& languageCode)
{
    if (languageCode == QLatin1String(kSourceLanguage)) {
        return true;
    }

    return QFile::exists(TranslationPath(languageCode));
}

void LanguageController::InstallTranslator(const QString& languageCode)
{
    if (m_translatorInstalled) {
        QCoreApplication::removeTranslator(&m_translator);
        m_translatorInstalled = false;
    }

    if (languageCode == QLatin1String(kSourceLanguage)) {
        Debug::Log("LanguageController: using built-in English strings");
        return;
    }

    const QString translationPath = TranslationPath(languageCode);
    if (!m_translator.load(translationPath)) {
        Debug::LogWarning(
            "LanguageController: failed to load translation '{}'",
            translationPath.toStdString()
        );
        return;
    }

    QCoreApplication::installTranslator(&m_translator);
    m_translatorInstalled = true;
    Debug::Log(
        "LanguageController: installed translation '{}'",
        translationPath.toStdString()
    );
}
