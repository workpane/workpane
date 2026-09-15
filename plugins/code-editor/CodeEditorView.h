#pragma once

#include "CodeEditorPlugin.h"
#include "CodeWorkspaceView.h"
#include "ui/TabBar.h"

class QLabel;

namespace workpane::plugins::codeeditor {

class CodeEditorView final : public QWidget {
    Q_OBJECT

  public:
    CodeEditorView(CodeEditorPlugin& plugin, QWidget* parent = nullptr);

  private:
    void chooseWorkspace();
    void closeWorkspace(int index);
    void synchronizeWorkspaces();
    void synchronizeWordWrap();
    void synchronizeEditorFont();
    void synchronizeColorScheme();
    void synchronizeLanguageServers();
    void reportError(const QString& message);
    [[nodiscard]] CodeWorkspaceView* workspaceView(const QString& workspaceId) const;
    // Every page of the workspace strip is one of these, so the tab widget is read through this rather than cast at each caller.
    [[nodiscard]] CodeWorkspaceView* workspaceAt(int index) const;
    [[nodiscard]] CodeWorkspaceView* currentWorkspace() const;
    // The open workspaces in the order the strip shows them, so a caller iterates a typed list rather than a strip of widgets.
    [[nodiscard]] QVector<CodeWorkspaceView*> workspaces() const;

    CodeEditorPlugin& m_plugin;
    ui::TabWidget* m_workspaces{nullptr};
    QLabel* m_empty{nullptr};
    bool m_rebuilding{false};
};

} // namespace workpane::plugins::codeeditor
