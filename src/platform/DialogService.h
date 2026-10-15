#pragma once

#include "Result.h"
#include "platform/FileFilter.h"

#include <functional>
#include <string>
#include <vector>

namespace workpane::platform {

enum class MessageKind { Information, Warning, Error, Question };

enum class MessageButtons { Ok, OkCancel, YesNo, YesNoCancel };

// The dialogs the operating system draws itself, answered asynchronously so the frame loop keeps running while one is open.
// A file dialog answers an empty list of paths when the reader cancelled it.
class DialogService {
  public:
    using PathsHandler = std::function<void(Result<std::vector<std::string>>)>;
    using ChoiceHandler = std::function<void(Result<std::string>)>;

    virtual ~DialogService() = default;

    virtual void openFiles(std::string title, std::string initial, std::vector<FileFilter> filters, bool multiple, PathsHandler handler) = 0;
    virtual void selectFolder(std::string title, std::string initial, PathsHandler handler) = 0;
    virtual void saveFile(std::string title, std::string initial, std::vector<FileFilter> filters, PathsHandler handler) = 0;
    virtual void message(std::string title, std::string text, MessageKind kind, MessageButtons buttons, ChoiceHandler handler) = 0;
    [[nodiscard]] virtual Result<void> notify(std::string title, std::string text, MessageKind kind) = 0;
    virtual void poll() = 0;
    [[nodiscard]] virtual bool pending() const = 0;
};

} // namespace workpane::platform
