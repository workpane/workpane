#pragma once

#include "agent/AgentResources.h"
#include "plugins/PluginInterface.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <functional>
#include <memory>

namespace workpane::agent {

// The agent reads what the published ecosystems left in the workspace it runs in and in the roots of the machine, and never installs any of it.
class AgentResourceCatalog final : public QObject {
    Q_OBJECT

  public:
    using Completion = std::function<void(const QVector<ResourceDescriptor>&)>;

    explicit AgentResourceCatalog(plugins::PluginHost& host, QObject* parent = nullptr);

    void discover(const QString& workdir, const Completion& completion);
    // A run reads the workspace again, because a skill or an instruction added while the application is open must reach the next one.
    void forget();

    [[nodiscard]] static ResourceDescriptor describe(const ResourceRoot& root, const QString& name, const QString& path, const QString& content);
    [[nodiscard]] static QVector<ResourceDescriptor> ofKind(const QVector<ResourceDescriptor>& resources, ResourceKind kind);

  private:
    struct Scan;

    void scanRoot(const std::shared_ptr<Scan>& scan);
    void scanEntry(const std::shared_ptr<Scan>& scan);
    void readDocument(const std::shared_ptr<Scan>& scan);
    void expandBundles(const std::shared_ptr<Scan>& scan);
    void finish(const std::shared_ptr<Scan>& scan);

    plugins::PluginHost& m_host;
    QString m_scannedWorkdir;
    QVector<ResourceDescriptor> m_cached;
    std::shared_ptr<Scan> m_running;
    bool m_cacheValid{false};
};

} // namespace workpane::agent
