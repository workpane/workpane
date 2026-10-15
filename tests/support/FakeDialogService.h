#pragma once

#include "Result.h"
#include "platform/DialogService.h"
#include "platform/FileFilter.h"
#include "support/DialogRecord.h"

#include <memory>
#include <string>
#include <vector>

namespace workpane::tests {

// Answers every platform dialog on the next poll with what the record holds, the way a reader answers a real one a moment later.
class FakeDialogService final : public platform::DialogService {
  public:
    explicit FakeDialogService(std::shared_ptr<DialogRecord> record);

    void openFiles(std::string title, std::string initial, std::vector<platform::FileFilter> filters, bool multiple, PathsHandler handler) override;
    void selectFolder(std::string title, std::string initial, PathsHandler handler) override;
    void saveFile(std::string title, std::string initial, std::vector<platform::FileFilter> filters, PathsHandler handler) override;
    void message(std::string title, std::string text, platform::MessageKind kind, platform::MessageButtons buttons, ChoiceHandler handler) override;
    [[nodiscard]] Result<void> notify(std::string title, std::string text, platform::MessageKind kind) override;
    void poll() override;
    [[nodiscard]] bool pending() const override;

  private:
    std::shared_ptr<DialogRecord> m_record;
    std::vector<PathsHandler> m_paths;
    std::vector<ChoiceHandler> m_choices;
};

} // namespace workpane::tests
