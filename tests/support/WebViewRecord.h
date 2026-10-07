#pragma once

#include "ui/NativeWebView.h"

#include <imgui_internal.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace workpane::tests {

struct WebViewRecord final {
    int created{0};
    std::vector<std::string> navigations;
    std::vector<std::string> documents;
    std::vector<std::string> commands;
    std::vector<ui::NativeWebView::OpenHandler> openers;
    std::vector<ui::NativeWebView::PopupHandler> popupers;
    std::vector<ui::NativeWebView::CloseHandler> closers;
    std::vector<ui::NativeWebView::DownloadHandler> downloaders;
    std::vector<ui::NativeWebView::PermissionHandler> askers;
    std::vector<ui::NativeWebView::FailureHandler> failers;
    std::vector<std::pair<std::uint64_t, bool>> answers;
    std::string icon;
    int placedVisible{0};
    std::vector<ImRect> floating;
    bool holdLoading{false};
};

} // namespace workpane::tests
