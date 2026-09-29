#include "platform/NativeSystemServices.h"

#include "platform/UrlPolicy.h"
#include "platform/macos/MacFonts.h"
#include "platform/macos/MacText.h"
#include "platform/posix/ProgramImage.h"

#import <Cocoa/Cocoa.h>

#include <mach-o/dyld.h>
#include <unistd.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace workpane::platform {

Result<void> NativeSystemServices::openUrl(std::string_view url) {
    if (!UrlPolicy::allowed(url)) {
        return Result<void>::failure({"system_url_refused", "Only web addresses open in the default browser", std::string(url)});
    }

    @autoreleasepool {
        NSString* text = MacText::string(url);
        NSURL* address = text == nil ? nil : [NSURL URLWithString:text];

        if (address == nil || ![[NSWorkspace sharedWorkspace] openURL:address]) {
            return Result<void>::failure({"system_url_failed", "The default browser could not open the address", std::string(url)});
        }
    }

    return Result<void>::success();
}

Result<void> NativeSystemServices::revealPath(const std::filesystem::path& path) {
    @autoreleasepool {
        NSString* text = [NSString stringWithUTF8String:path.string().c_str()];

        if (text == nil) {
            return Result<void>::failure({"system_reveal_failed", "The path could not be shown in the file manager", path.string()});
        }

        [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[ [NSURL fileURLWithPath:text] ]];
    }

    return Result<void>::success();
}

// The first preferred language of the reader is what the system shows them, which is more precise than the region format.
std::string NativeSystemServices::locale() {
    @autoreleasepool {
        NSArray<NSString*>* preferred = [NSLocale preferredLanguages];
        NSString* identifier = preferred.count > 0 ? preferred.firstObject : [NSLocale currentLocale].localeIdentifier;
        return MacText::utf8(identifier);
    }
}

std::filesystem::path NativeSystemServices::home() {
    return std::filesystem::path(NSHomeDirectory().fileSystemRepresentation);
}

// The downloads folder is the one Finder names for the account.
std::filesystem::path NativeSystemServices::downloads() {
    @autoreleasepool {
        NSURL* folder = [NSFileManager.defaultManager URLsForDirectory:NSDownloadsDirectory inDomains:NSUserDomainMask].firstObject;
        return folder != nil ? std::filesystem::path(folder.fileSystemRepresentation) : home() / "Downloads";
    }
}

// The zone the system is set to is read again each time, so a change made while the product runs is seen.
Result<std::string> NativeSystemServices::timeZone() {
    @autoreleasepool {
        [NSTimeZone resetSystemTimeZone];
        return Result<std::string>::success(MacText::utf8([NSTimeZone systemTimeZone].name));
    }
}

std::int64_t NativeSystemServices::processId() {
    return static_cast<std::int64_t>(::getpid());
}

std::vector<InstalledFont> NativeSystemServices::monospaceFonts() {
    return MacFonts::monospace();
}

Result<int> NativeSystemServices::zoneOffset(std::string_view zone, std::int64_t seconds) {
    @autoreleasepool {
        NSString* name = [NSString stringWithUTF8String:std::string(zone).c_str()];
        NSTimeZone* found = name == nil ? nil : [NSTimeZone timeZoneWithName:name];

        if (found == nil) {
            return Result<int>::failure({"time_zone_unknown", "The time zone is not known to the system", std::string(zone)});
        }

        return Result<int>::success(static_cast<int>([found secondsFromGMTForDate:[NSDate dateWithTimeIntervalSince1970:static_cast<double>(seconds)]]));
    }
}

Result<void> NativeSystemServices::relaunch(const std::vector<std::string>& arguments) {
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string executable(size, '\0');

    if (_NSGetExecutablePath(executable.data(), &size) != 0) {
        return Result<void>::failure({"system_relaunch_failed", "The product executable could not be located", {}});
    }

    executable.resize(std::char_traits<char>::length(executable.c_str()));
    std::vector<std::string> values{executable};
    values.insert(values.end(), arguments.begin(), arguments.end());

    // The new product starts in a session of its own with the streams of this one and none of its other descriptors, so it holds nothing this one leaves behind.
    if (const auto spawned = ProgramImage(values, ProgramImage::inheritedEnvironment(), {}).spawn({-1, -1, -1}, ProgramImage::Grouping::Session); !spawned.hasValue()) {
        return Result<void>::failure({"system_relaunch_failed", "The product could not be started again", executable});
    }

    return Result<void>::success();
}

} // namespace workpane::platform
