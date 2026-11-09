#ifndef LANGUAGE_CONTROLLER_H
#define LANGUAGE_CONTROLLER_H

#include <QObject>
#include <QString>
#include <QTranslator>
#include <QVariantList>

class LanguageController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentLanguage READ currentLanguage NOTIFY currentLanguageChanged)
    Q_PROPERTY(QString effectiveLanguage READ effectiveLanguage NOTIFY currentLanguageChanged)
    Q_PROPERTY(QVariantList availableLanguages READ availableLanguages CONSTANT)
    Q_PROPERTY(bool restartRequired READ restartRequired NOTIFY restartRequiredChanged)

public:
    explicit LanguageController(QObject* parent = nullptr);
    ~LanguageController() override;

    QString currentLanguage() const;
    QString effectiveLanguage() const;
    QVariantList availableLanguages() const;
    bool restartRequired() const;

    Q_INVOKABLE void setLanguage(const QString& code);

    static QString NormalizeLanguageCode(const QString& code);
    static QString ResolveEffectiveLanguage(const QString& requestedCode);
    static QString SystemLanguageCode();

signals:
    void currentLanguageChanged();
    void restartRequiredChanged();

private:
    void InstallTranslator(const QString& languageCode);
    static bool IsTranslationAvailable(const QString& languageCode);

    QTranslator m_translator;
    QString m_currentLanguage;
    QString m_effectiveLanguage;
    QString m_installedLanguage;
    bool m_translatorInstalled = false;
    bool m_restartRequired = false;
};

#endif //LANGUAGE_CONTROLLER_H
