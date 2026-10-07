#pragma once

#include <filesystem>

namespace workpane::ui {

// Where a terminal starts, at which size, with which shell when it is not the default one and in which file its shell keeps its history when it has one of its own.
struct TerminalLaunch final {
    std::filesystem::path directory;
    int columns{80};
    int rows{24};
    std::filesystem::path shell;
    std::filesystem::path historyFile;
};

} // namespace workpane::ui
