#pragma once

#include "Result.h"
#include "platform/DialogService.h"
#include "platform/FileFilter.h"

#include <memory>
#include <string>
#include <vector>

namespace pfd {
enum class button;
enum class choice;
enum class icon;
} // namespace pfd

namespace workpane::platform {

// The dialogs the operating system draws itself, polled by the frame loop until each one answers.
class NativeDialogService final : public DialogService {
  public:
    NativeDialogService();
    ~NativeDialogService() override;

    void openFiles(std::string title, std::string initial, std::vector<FileFilter> filters, bool multiple, PathsHandler handler) override;
    void selectFolder(std::string title, std::string initial, PathsHandler handler) override;
    void saveFile(std::string title, std::string initial, std::vector<FileFilter> filters, PathsHandler handler) override;
    void message(std::string title, std::string text, MessageKind kind, MessageButtons buttons, ChoiceHandler handler) override;
    [[nodiscard]] Result<void> notify(std::string title, std::string text, MessageKind kind) override;
    void poll() override;
    [[nodiscard]] bool pending() const override;
    static void alertBlocking(const std::string& title, const std::string& text);

  private:
    class Operation;
    class PendingNotification;
    template <typename Dialog, typename Handler, typename Converter> class PendingDialog;

    [[nodiscard]] static std::vector<std::string> filters(const std::vector<FileFilter>& filters);
    [[nodiscard]] static pfd::icon icon(MessageKind kind);
    [[nodiscard]] static pfd::choice choice(MessageButtons buttons);
    [[nodiscard]] static std::string button(pfd::button button);
    [[nodiscard]] static Error unavailable();

    std::vector<std::unique_ptr<Operation>> m_operations;
};

} // namespace workpane::platform
