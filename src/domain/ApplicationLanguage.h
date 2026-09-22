#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace workpane::domain {

// A language and the key that names it are declared together, so one cannot be added without the other.
struct ApplicationLanguageDescriptor final {
    QString code;
    QString titleKey;
};

class ApplicationLanguages final {
  public:
    [[nodiscard]] static const QVector<ApplicationLanguageDescriptor>& applicationLanguageCatalog();
    [[nodiscard]] static const QStringList& supportedApplicationLanguages();
    [[nodiscard]] static bool isSupportedApplicationLanguage(const QString& language);
    [[nodiscard]] static QString resolveApplicationLanguage(QString localeName);
};

} // namespace workpane::domain
