#pragma once

#include <string>

namespace workpane::platform {

// The menu the system shows for the application, which only macOS has, written in the language of the reader.
// Its Quit asks the window to close, so the quit confirmation of the product answers it like any other way of quitting.
class ApplicationMenu final {
  public:
    struct Texts final {
        std::string about;
        std::string services;
        std::string hide;
        std::string hideOthers;
        std::string showAll;
        std::string quit;
        std::string window;
        std::string minimize;
        std::string zoom;
        std::string front;
        std::string edit;
        std::string undo;
        std::string redo;
        std::string cut;
        std::string copy;
        std::string paste;
        std::string selectAll;
    };

    static void install(const Texts& texts);
};

} // namespace workpane::platform
