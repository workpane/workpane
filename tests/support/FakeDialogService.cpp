#include "support/FakeDialogService.h"

#include <utility>

namespace workpane::tests {

FakeDialogService::FakeDialogService(std::shared_ptr<DialogRecord> record) : m_record(std::move(record)) {}

void FakeDialogService::openFiles(std::string title, std::string, std::vector<platform::FileFilter>, bool, PathsHandler handler) {
    m_record->requests.push_back("open:" + title);
    m_paths.push_back(std::move(handler));
}

void FakeDialogService::selectFolder(std::string title, std::string, PathsHandler handler) {
    m_record->requests.push_back("folder:" + title);
    m_paths.push_back(std::move(handler));
}

void FakeDialogService::saveFile(std::string title, std::string, std::vector<platform::FileFilter>, PathsHandler handler) {
    m_record->requests.push_back("save:" + title);
    m_paths.push_back(std::move(handler));
}

void FakeDialogService::message(std::string title, std::string, platform::MessageKind, platform::MessageButtons, ChoiceHandler handler) {
    m_record->requests.push_back("message:" + title);
    m_choices.push_back(std::move(handler));
}

Result<void> FakeDialogService::notify(std::string title, std::string, platform::MessageKind) {
    m_record->notifications.push_back(std::move(title));

    return Result<void>::success();
}

void FakeDialogService::poll() {
    auto paths = std::move(m_paths);
    auto choices = std::move(m_choices);
    m_paths.clear();
    m_choices.clear();

    for (const auto& handler : paths) {
        handler(Result<std::vector<std::string>>::success(m_record->paths));
    }

    for (const auto& handler : choices) {
        handler(Result<std::string>::success(m_record->button));
    }
}

bool FakeDialogService::pending() const {
    return !m_paths.empty() || !m_choices.empty();
}

} // namespace workpane::tests
