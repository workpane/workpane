#pragma once

#include "ui/shell/DialogAnswer.h"
#include "ui/shell/DialogButton.h"

#include <functional>
#include <string>
#include <vector>

namespace workpane::ui {

enum class DialogKind { Confirm, Alert, Prompt, Custom };

struct DialogRequest final {
    std::string owner;
    DialogKind kind{DialogKind::Confirm};
    std::string title;
    std::string message;
    std::string detail;
    std::string confirmText;
    std::string cancelText;
    bool destructive{false};
    std::string value;
    std::string placeholder;
    std::string surface;
    std::vector<DialogButton> buttons;
    float width{440.0F};
    std::function<void(DialogAnswer)> answer;
};

} // namespace workpane::ui
