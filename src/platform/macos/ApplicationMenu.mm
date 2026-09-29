#include "platform/ApplicationMenu.h"

#import <Cocoa/Cocoa.h>

namespace workpane::platform {

// The application menu and the window menu are built again whole, so a new language replaces every title at once.
void ApplicationMenu::install(const Texts& texts) {
    @autoreleasepool {
        // clang-format off
        const auto title = [](const std::string& text) { return [NSString stringWithUTF8String:text.c_str()]; };
        // clang-format on
        NSMenu* bar = [[NSMenu alloc] init];
        NSMenuItem* applicationItem = [bar addItemWithTitle:@"" action:nil keyEquivalent:@""];
        NSMenu* application = [[NSMenu alloc] init];
        [application addItemWithTitle:title(texts.about) action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
        [application addItem:[NSMenuItem separatorItem]];
        NSMenuItem* servicesItem = [application addItemWithTitle:title(texts.services) action:nil keyEquivalent:@""];
        NSMenu* services = [[NSMenu alloc] init];
        servicesItem.submenu = services;
        NSApp.servicesMenu = services;
        [application addItem:[NSMenuItem separatorItem]];
        [application addItemWithTitle:title(texts.hide) action:@selector(hide:) keyEquivalent:@"h"];
        [[application addItemWithTitle:title(texts.hideOthers) action:@selector(hideOtherApplications:) keyEquivalent:@"h"] setKeyEquivalentModifierMask:NSEventModifierFlagOption | NSEventModifierFlagCommand];
        [application addItemWithTitle:title(texts.showAll) action:@selector(unhideAllApplications:) keyEquivalent:@""];
        [application addItem:[NSMenuItem separatorItem]];
        [application addItemWithTitle:title(texts.quit) action:@selector(terminate:) keyEquivalent:@"q"];
        applicationItem.submenu = application;

        NSMenuItem* windowItem = [bar addItemWithTitle:@"" action:nil keyEquivalent:@""];
        NSMenu* window = [[NSMenu alloc] initWithTitle:title(texts.window)];
        [window addItemWithTitle:title(texts.minimize) action:@selector(performMiniaturize:) keyEquivalent:@"m"];
        [window addItemWithTitle:title(texts.zoom) action:@selector(performZoom:) keyEquivalent:@""];
        [window addItem:[NSMenuItem separatorItem]];
        [window addItemWithTitle:title(texts.front) action:@selector(arrangeInFront:) keyEquivalent:@""];
        windowItem.submenu = window;

        [NSApplication sharedApplication].mainMenu = bar;
        NSApp.windowsMenu = window;
    }
}

} // namespace workpane::platform
