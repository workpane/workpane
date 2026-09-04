#include "app/ConfigurationManager.h"

#include "persistence/ConfigurationTransfer.h"
#include "persistence/DatabaseExecutor.h"

#include <utility>

namespace workpane::app {

ConfigurationManager::ConfigurationManager(persistence::DatabaseExecutor& databaseExecutor, QString pendingImportPath, QHash<QString, int> pluginSchemaVersions, QObject* parent) : QObject(parent), m_databaseExecutor(databaseExecutor), m_pendingImportPath(std::move(pendingImportPath)), m_pluginSchemaVersions(std::move(pluginSchemaVersions)) {}

QFuture<Result<void>> ConfigurationManager::exportConfiguration(const QString& destinationPath) {
    if (m_transferActive) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"configuration_transfer_pending", "A configuration transfer is already running", {}}));
    }

    m_transferActive = true;
    emit transferStateChanged(true);
    auto future = m_databaseExecutor.exportConfiguration(destinationPath);
    // clang-format off
    return future.then(this, [this](Result<void> result) {
        m_transferActive = false;
        emit transferStateChanged(false);
        return result;
    });
    // clang-format on
}

QFuture<Result<void>> ConfigurationManager::importConfiguration(const QString& sourcePath) {
    if (m_transferActive) {
        return QtFuture::makeReadyValueFuture(Result<void>::failure({"configuration_transfer_pending", "A configuration transfer is already running", {}}));
    }

    m_transferActive = true;
    emit transferStateChanged(true);
    auto future = persistence::ConfigurationTransfer::stageImport(sourcePath, m_pendingImportPath, m_pluginSchemaVersions);
    // clang-format off
    return future.then(this, [this](Result<void> result) {
        m_transferActive = false;
        emit transferStateChanged(false);
        return result;
    });
    // clang-format on
}

void ConfigurationManager::requestRestart() {
    emit restartRequested();
}

} // namespace workpane::app
