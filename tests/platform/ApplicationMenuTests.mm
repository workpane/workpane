#include "platform/ApplicationMenu.h"

#include <gtest/gtest.h>

#import <Cocoa/Cocoa.h>

#include <string>

namespace workpane::platform {

// The application menu carries the texts it is given, its Quit terminates the application, which the window turns into a close request, and a new language replaces every title.
TEST(ApplicationMenu, WritesTheMenuOfMacOSInTheLanguageGiven) {
    ApplicationMenu::install({"Sobre o Workpane", "Serviços", "Ocultar o Workpane", "Ocultar os Outros", "Mostrar Tudo", "Encerrar o Workpane", "Janela", "Minimizar", "Zoom", "Trazer Tudo para a Frente", "Editar", "Desfazer", "Refazer", "Recortar", "Copiar", "Colar", "Selecionar Tudo"});
    NSMenu* bar = NSApp.mainMenu;
    ASSERT_NE(bar, nil);
    ASSERT_EQ(bar.numberOfItems, 3);

    NSMenu* application = [bar itemAtIndex:0].submenu;
    EXPECT_EQ(std::string([application itemAtIndex:0].title.UTF8String), "Sobre o Workpane");
    NSMenuItem* quit = [application itemAtIndex:application.numberOfItems - 1];
    EXPECT_EQ(std::string(quit.title.UTF8String), "Encerrar o Workpane");
    EXPECT_EQ(quit.action, @selector(terminate:));
    EXPECT_EQ(std::string([bar itemAtIndex:2].submenu.title.UTF8String), "Janela");
    NSMenu* edit = [bar itemAtIndex:1].submenu;
    EXPECT_EQ(std::string(edit.title.UTF8String), "Editar");
    EXPECT_EQ([edit itemAtIndex:1].action, @selector(redo:));
    EXPECT_EQ(std::string([edit itemAtIndex:1].keyEquivalent.UTF8String), "Z");
    EXPECT_EQ([edit itemAtIndex:4].action, @selector(copy:));
    EXPECT_EQ(std::string([edit itemAtIndex:4].keyEquivalent.UTF8String), "c");
    EXPECT_EQ([edit itemAtIndex:5].action, @selector(paste:));
    EXPECT_EQ(std::string([edit itemAtIndex:5].keyEquivalent.UTF8String), "v");

    ApplicationMenu::install({"About Workpane", "Services", "Hide Workpane", "Hide Others", "Show All", "Quit Workpane", "Window", "Minimize", "Zoom", "Bring All to Front", "Edit", "Undo", "Redo", "Cut", "Copy", "Paste", "Select All"});
    EXPECT_EQ(std::string([[NSApp.mainMenu itemAtIndex:0].submenu itemAtIndex:0].title.UTF8String), "About Workpane");
}

} // namespace workpane::platform
