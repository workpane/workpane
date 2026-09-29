#include "platform/NativeDialogService.h"

#include <portable-file-dialogs.h>

#include <cstdio>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace workpane::platform {

// A dialog the platform runs on its own, polled without waiting until it has an answer and then answered exactly once.
class NativeDialogService::Operation {
  public:
    virtual ~Operation() = default;

    [[nodiscard]] virtual bool ready() = 0;
    virtual void finish() = 0;
};

std::vector<std::string> NativeDialogService::filters(const std::vector<FileFilter>& filters) {
    std::vector<std::string> flattened;

    for (const auto& filter : filters) {
        std::string patterns;

        for (const auto& pattern : filter.patterns) {
            patterns += (patterns.empty() ? "" : " ") + pattern;
        }

        flattened.push_back(filter.name);
        flattened.push_back(patterns);
    }

    return flattened;
}

pfd::icon NativeDialogService::icon(MessageKind kind) {
    switch (kind) {
    case MessageKind::Information:
        return pfd::icon::info;
    case MessageKind::Warning:
        return pfd::icon::warning;
    case MessageKind::Error:
        return pfd::icon::error;
    case MessageKind::Question:
        return pfd::icon::question;
    }

    return pfd::icon::info;
}

pfd::choice NativeDialogService::choice(MessageButtons buttons) {
    switch (buttons) {
    case MessageButtons::Ok:
        return pfd::choice::ok;
    case MessageButtons::OkCancel:
        return pfd::choice::ok_cancel;
    case MessageButtons::YesNo:
        return pfd::choice::yes_no;
    case MessageButtons::YesNoCancel:
        return pfd::choice::yes_no_cancel;
    }

    return pfd::choice::ok;
}

std::string NativeDialogService::button(pfd::button button) {
    switch (button) {
    case pfd::button::ok:
        return "ok";
    case pfd::button::yes:
        return "yes";
    case pfd::button::no:
        return "no";
    case pfd::button::cancel:
    case pfd::button::abort:
    case pfd::button::retry:
    case pfd::button::ignore:
        return "cancel";
    }

    return "cancel";
}

Error NativeDialogService::unavailable() {
    return {"dialog_unavailable", "The platform offers no native dialog, which on Linux needs zenity or kdialog", {}};
}

// A dialog still open when the product quits is closed with it, since its library would otherwise wait for the reader to answer a dialog of a product already gone.
template <typename Dialog, typename Handler, typename Converter> class NativeDialogService::PendingDialog final : public Operation {
  public:
    PendingDialog(std::unique_ptr<Dialog> dialog, Handler handler, Converter converter) : m_dialog(std::move(dialog)), m_handler(std::move(handler)), m_converter(std::move(converter)) {}

    ~PendingDialog() override {
        if (!m_dialog->ready(0)) {
            std::ignore = m_dialog->kill();
        }
    }

    [[nodiscard]] bool ready() override {
        return m_dialog->ready(0);
    }

    void finish() override {
        m_handler(m_converter(m_dialog->result()));
    }

  private:
    std::unique_ptr<Dialog> m_dialog;
    Handler m_handler;
    Converter m_converter;
};

// A system notification is kept until its helper process finishes, because dropping it earlier ends that process before it shows anything, and a helper still running when the product quits is ended with it.
class NativeDialogService::PendingNotification final : public Operation {
  public:
    explicit PendingNotification(std::unique_ptr<pfd::notify> notification) : m_notification(std::move(notification)) {}

    ~PendingNotification() override {
        if (!m_notification->ready(0)) {
            std::ignore = m_notification->kill();
        }
    }

    [[nodiscard]] bool ready() override {
        return m_notification->ready(0);
    }

    void finish() override {}

  private:
    std::unique_ptr<pfd::notify> m_notification;
};

NativeDialogService::NativeDialogService() = default;

NativeDialogService::~NativeDialogService() = default;

void NativeDialogService::openFiles(std::string title, std::string initial, std::vector<FileFilter> filters, bool multiple, PathsHandler handler) {
    if (!pfd::settings::available()) {
        handler(Result<std::vector<std::string>>::failure(unavailable()));
        return;
    }

    auto dialog = std::make_unique<pfd::open_file>(title, initial, NativeDialogService::filters(filters), multiple ? pfd::opt::multiselect : pfd::opt::none);
    // clang-format off
    const auto converter = [](std::vector<std::string> paths) { return Result<std::vector<std::string>>::success(std::move(paths)); };
    // clang-format on
    m_operations.push_back(std::make_unique<PendingDialog<pfd::open_file, PathsHandler, decltype(converter)>>(std::move(dialog), std::move(handler), converter));
}

void NativeDialogService::selectFolder(std::string title, std::string initial, PathsHandler handler) {
    if (!pfd::settings::available()) {
        handler(Result<std::vector<std::string>>::failure(unavailable()));
        return;
    }

    auto dialog = std::make_unique<pfd::select_folder>(title, initial, pfd::opt::force_path);
    // clang-format off
    const auto converter = [](std::string path) { return Result<std::vector<std::string>>::success(path.empty() ? std::vector<std::string>{} : std::vector<std::string>{std::move(path)}); };
    // clang-format on
    m_operations.push_back(std::make_unique<PendingDialog<pfd::select_folder, PathsHandler, decltype(converter)>>(std::move(dialog), std::move(handler), converter));
}

void NativeDialogService::saveFile(std::string title, std::string initial, std::vector<FileFilter> filters, PathsHandler handler) {
    if (!pfd::settings::available()) {
        handler(Result<std::vector<std::string>>::failure(unavailable()));
        return;
    }

    auto dialog = std::make_unique<pfd::save_file>(title, initial, NativeDialogService::filters(filters), pfd::opt::none);
    // clang-format off
    const auto converter = [](std::string path) { return Result<std::vector<std::string>>::success(path.empty() ? std::vector<std::string>{} : std::vector<std::string>{std::move(path)}); };
    // clang-format on
    m_operations.push_back(std::make_unique<PendingDialog<pfd::save_file, PathsHandler, decltype(converter)>>(std::move(dialog), std::move(handler), converter));
}

void NativeDialogService::message(std::string title, std::string text, MessageKind kind, MessageButtons buttons, ChoiceHandler handler) {
    if (!pfd::settings::available()) {
        handler(Result<std::string>::failure(unavailable()));
        return;
    }

    auto dialog = std::make_unique<pfd::message>(title, text, choice(buttons), icon(kind));
    // clang-format off
    const auto converter = [](pfd::button button) { return Result<std::string>::success(NativeDialogService::button(button)); };
    // clang-format on
    m_operations.push_back(std::make_unique<PendingDialog<pfd::message, ChoiceHandler, decltype(converter)>>(std::move(dialog), std::move(handler), converter));
}

Result<void> NativeDialogService::notify(std::string title, std::string text, MessageKind kind) {
    if (!pfd::settings::available()) {
        return Result<void>::failure(unavailable());
    }

    m_operations.push_back(std::make_unique<PendingNotification>(std::make_unique<pfd::notify>(title, text, icon(kind))));

    return Result<void>::success();
}

// Finished operations leave the list before their handlers run, so a handler opening another dialog never disturbs the iteration.
void NativeDialogService::poll() {
    std::vector<std::unique_ptr<Operation>> finished;

    for (auto& operation : m_operations) {
        if (operation->ready()) {
            finished.push_back(std::move(operation));
        }
    }

    // clang-format off
    std::erase_if(m_operations, [](const std::unique_ptr<Operation>& operation) { return operation == nullptr; });
    // clang-format on

    for (auto& operation : finished) {
        operation->finish();
    }
}

// A message shown before any window exists waits for the reader, because nothing else is running yet.
void NativeDialogService::alertBlocking(const std::string& title, const std::string& text) {
    if (!pfd::settings::available()) {
        std::fprintf(stderr, "%s\n%s\n", title.c_str(), text.c_str());
        return;
    }

    std::ignore = pfd::message(title, text, pfd::choice::ok, pfd::icon::error).result();
}

bool NativeDialogService::pending() const {
    return !m_operations.empty();
}

} // namespace workpane::platform
