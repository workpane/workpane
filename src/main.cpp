#include "BuildInfo.h"
#include "app/Application.h"
#include "domain/ApplicationLanguage.h"
#include "domain/Result.h"
#include "plugins/CoreTranslations.h"
#include "ui/AppStyle.h"

#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QLocale>
#include <QMessageBox>
#include <QStyleFactory>

namespace workpane {

class MainHelper final {
  public:
    static int reportStartupFailure(const Error& error);
};

// A windowed application has no console to write to, so a failure that only reaches the log leaves the reader with a window that never opens.
int MainHelper::reportStartupFailure(const Error& error) {
    qCritical().noquote() << error.message << error.detail;
    const plugins::TranslationEntries entries = plugins::coretranslations::CoreCatalog::catalog().value(domain::ApplicationLanguages::resolveApplicationLanguage(QLocale::system().name()));
    QMessageBox failure(QMessageBox::Critical, entries.value(QStringLiteral("workpane.startup.failed-title")), entries.value(QStringLiteral("workpane.startup.failed-message")));
    failure.setDetailedText(error.detail.isEmpty() ? error.message : error.message + QLatin1Char('\n') + error.detail);
    failure.exec();
    return 1;
}

} // namespace workpane

int main(int argc, char* argv[]) {
    QApplication::setAttribute(Qt::AA_DontShowIconsInMenus, false);
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral(WORKPANE_PRODUCT_NAME));
    QCoreApplication::setApplicationVersion(QStringLiteral(WORKPANE_APP_VERSION));
    QCoreApplication::setOrganizationName(QStringLiteral(WORKPANE_ORGANIZATION_NAME));
    QCoreApplication::setOrganizationDomain(QStringLiteral(WORKPANE_ORGANIZATION_DOMAIN));
    application.setWindowIcon(QIcon(QStringLiteral(":/images/logo.png")));
#ifdef Q_OS_MACOS
    application.setFont(QFont(QStringLiteral(".AppleSystemUIFont")));
#endif
    QApplication::setStyle(new workpane::ui::AppStyle(QStyleFactory::create("Fusion")));
    QApplication::setPalette(workpane::ui::AppStyle::applicationPalette());
    const auto dataPath = workpane::app::Application::resolveDataPath(QCoreApplication::arguments());

    if (!dataPath.hasValue()) {
        return workpane::MainHelper::reportStartupFailure(dataPath.error());
    }

    workpane::app::Application workPaneApplication(dataPath.value(), nullptr);
    const auto initialization = workPaneApplication.initialize();

    if (!initialization.hasValue()) {
        return workpane::MainHelper::reportStartupFailure(initialization.error());
    }

    const auto interfaceResult = workPaneApplication.loadInterface();

    if (!interfaceResult.hasValue()) {
        return workpane::MainHelper::reportStartupFailure(interfaceResult.error());
    }

    // The window is already on screen, so everything the reader waits for runs from the event loop it is being painted by.
    // clang-format off
    const auto completeStartup = [&workPaneApplication]() {
        const auto startup = workPaneApplication.completeStartup();
        if (!startup.hasValue()) {
            QCoreApplication::exit(workpane::MainHelper::reportStartupFailure(startup.error()));
        }
    };
    QMetaObject::invokeMethod(&workPaneApplication, completeStartup, Qt::QueuedConnection);
    // clang-format on

    QObject::connect(&application, &QCoreApplication::aboutToQuit, &workPaneApplication, &workpane::app::Application::shutdown);
    return application.exec();
}
