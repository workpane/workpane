#include "persistence/DatabaseExecutor.h"

#include "domain/ApplicationSettings.h"
#include "persistence/ConfigurationTransfer.h"
#include "persistence/StateStore.h"

#include <QMetaObject>
#include <QPromise>

#include <functional>
#include <memory>
#include <utility>

namespace workpane::persistence {

class DatabaseWorker final : public QObject {
  public:
    explicit DatabaseWorker(QString filePath) : m_filePath(std::move(filePath)) {}

    [[nodiscard]] Result<StateStore*> openedStore() {
        if (m_stateStore != nullptr) {
            return Result<StateStore*>::success(m_stateStore.get());
        }

        auto stateStore = std::make_unique<StateStore>(m_filePath);
        const auto result = stateStore->initialize();

        if (!result.hasValue()) {
            return Result<StateStore*>::failure(result.error());
        }

        m_stateStore = std::move(stateStore);
        return Result<StateStore*>::success(m_stateStore.get());
    }

    void close() {
        m_stateStore.reset();
    }

  private:
    QString m_filePath;
    std::unique_ptr<StateStore> m_stateStore;
};

class DatabaseExecutorHelper final {
  public:
    template <typename T> static QFuture<Result<T>> submit(DatabaseWorker* worker, std::function<Result<T>(StateStore&)> operation);
};

template <typename T> QFuture<Result<T>> DatabaseExecutorHelper::submit(DatabaseWorker* worker, std::function<Result<T>(StateStore&)> operation) {
    auto promise = std::make_shared<QPromise<Result<T>>>();
    promise->start();
    const QFuture<Result<T>> future = promise->future();

    // clang-format off
    const bool submitted = QMetaObject::invokeMethod(worker, [worker, operation = std::move(operation), promise]() mutable {
        const auto opened = worker->openedStore();
        if (!opened.hasValue()) {
            promise->addResult(Result<T>::failure(opened.error()));
            promise->finish();
            return;
        }
        promise->addResult(operation(*opened.value()));
        promise->finish();
    }, Qt::QueuedConnection);
    // clang-format on

    if (!submitted) {
        promise->addResult(Result<T>::failure({"database_executor_unavailable", "The database executor is unavailable", {}}));
        promise->finish();
    }

    return future;
}

template <> QFuture<Result<void>> DatabaseExecutorHelper::submit(DatabaseWorker* worker, std::function<Result<void>(StateStore&)> operation) {
    auto promise = std::make_shared<QPromise<Result<void>>>();
    promise->start();
    const QFuture<Result<void>> future = promise->future();

    // clang-format off
    const bool submitted = QMetaObject::invokeMethod(worker, [worker, operation = std::move(operation), promise]() mutable {
        const auto opened = worker->openedStore();
        if (!opened.hasValue()) {
            promise->addResult(Result<void>::failure(opened.error()));
            promise->finish();
            return;
        }
        promise->addResult(operation(*opened.value()));
        promise->finish();
    }, Qt::QueuedConnection);
    // clang-format on

    if (!submitted) {
        promise->addResult(Result<void>::failure({"database_executor_unavailable", "The database executor is unavailable", {}}));
        promise->finish();
    }

    return future;
}

DatabaseExecutor::DatabaseExecutor(QString filePath, QObject* parent) : QObject(parent), m_worker(std::make_unique<DatabaseWorker>(std::move(filePath))) {
    m_worker->moveToThread(&m_workerThread);
    m_workerThread.setObjectName(QStringLiteral("workpaneDatabase"));
    m_workerThread.start();
}

DatabaseExecutor::~DatabaseExecutor() {
    if (!m_workerThread.isRunning()) {
        return;
    }

    QThread* ownerThread = thread();
    // clang-format off
    QMetaObject::invokeMethod(m_worker.get(), [this, ownerThread]() {
        m_worker->close();
        m_worker->moveToThread(ownerThread);
        m_workerThread.quit();
    }, Qt::QueuedConnection);
    // clang-format on
    m_workerThread.wait();
}

QFuture<Result<void>> DatabaseExecutor::saveSettings(const QString& ownerId, const QJsonObject& document) {
    // clang-format off
    return DatabaseExecutorHelper::submit<void>(m_worker.get(), [ownerId, document](StateStore& stateStore) { return stateStore.saveSettings(ownerId, document); });
    // clang-format on
}

QFuture<Result<void>> DatabaseExecutor::exportConfiguration(const QString& destinationPath) {
    // clang-format off
    return DatabaseExecutorHelper::submit<void>(m_worker.get(), [destinationPath](StateStore& stateStore) { return ConfigurationTransfer::exportDatabaseNow(stateStore.filePath(), destinationPath); });
    // clang-format on
}

QFuture<Result<void>> DatabaseExecutor::executePluginDatabase(const QString& pluginId, const QString& statement, const QVariantList& bindings) {
    // clang-format off
    return DatabaseExecutorHelper::submit<void>(m_worker.get(), [pluginId, statement, bindings](StateStore& stateStore) { return stateStore.executePluginDatabase(pluginId, statement, bindings); });
    // clang-format on
}

QFuture<Result<void>> DatabaseExecutor::executePluginDatabaseTransaction(const QString& pluginId, const QVector<DatabaseStatement>& statements) {
    // clang-format off
    return DatabaseExecutorHelper::submit<void>(m_worker.get(), [pluginId, statements](StateStore& stateStore) { return stateStore.executePluginDatabaseTransaction(pluginId, statements); });
    // clang-format on
}

QFuture<Result<DatabaseRows>> DatabaseExecutor::queryPluginDatabase(const QString& pluginId, const QString& statement, const QVariantList& bindings) {
    // clang-format off
    return DatabaseExecutorHelper::submit<DatabaseRows>(m_worker.get(), [pluginId, statement, bindings](StateStore& stateStore) { return stateStore.queryPluginDatabase(pluginId, statement, bindings); });
    // clang-format on
}

} // namespace workpane::persistence
