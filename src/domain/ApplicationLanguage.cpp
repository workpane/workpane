#include "domain/ApplicationLanguage.h"

namespace workpane::domain {

class ApplicationLanguageHelper final {
  public:
    static QStringList declaredCodes();
};

QStringList ApplicationLanguageHelper::declaredCodes() {
    QStringList codes;

    for (const ApplicationLanguageDescriptor& descriptor : ApplicationLanguages::applicationLanguageCatalog()) {
        codes.append(descriptor.code);
    }

    return codes;
}

const QVector<ApplicationLanguageDescriptor>& ApplicationLanguages::applicationLanguageCatalog() {
    static const QVector<ApplicationLanguageDescriptor> catalog{{QStringLiteral("en"), QStringLiteral("workpane.application.english")}, {QStringLiteral("pt"), QStringLiteral("workpane.application.portuguese")}};
    return catalog;
}

const QStringList& ApplicationLanguages::supportedApplicationLanguages() {
    static const QStringList languages = ApplicationLanguageHelper::declaredCodes();
    return languages;
}

bool ApplicationLanguages::isSupportedApplicationLanguage(const QString& language) {
    return ApplicationLanguages::supportedApplicationLanguages().contains(language);
}

QString ApplicationLanguages::resolveApplicationLanguage(QString localeName) {
    localeName.replace(QLatin1Char('_'), QLatin1Char('-'));
    localeName = localeName.toLower();

    if (ApplicationLanguages::isSupportedApplicationLanguage(localeName)) {
        return localeName;
    }

    const qsizetype separator = localeName.indexOf(QLatin1Char('-'));
    QString baseLanguage = separator > 0 ? localeName.first(separator) : localeName;

    if (ApplicationLanguages::isSupportedApplicationLanguage(baseLanguage)) {
        return baseLanguage;
    }

    return QStringLiteral("en");
}

} // namespace workpane::domain
