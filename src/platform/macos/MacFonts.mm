#include "platform/macos/MacFonts.h"

#include "platform/macos/MacText.h"

#import <CoreText/CoreText.h>
#import <Foundation/Foundation.h>

#include <cmath>
#include <filesystem>
#include <map>
#include <string>
#include <utility>

namespace workpane::platform {

// Every family with an upright face that writes each character in the same width is offered, with the file of its face closest to the regular weight.
std::vector<InstalledFont> MacFonts::monospace() {
    std::map<std::string, std::pair<double, std::filesystem::path>> chosen;

    @autoreleasepool {
        CTFontCollectionRef collection = CTFontCollectionCreateFromAvailableFonts(nullptr);
        NSArray* descriptors = CFBridgingRelease(CTFontCollectionCreateMatchingFontDescriptors(collection));
        CFRelease(collection);

        for (id item in descriptors) {
            CTFontDescriptorRef descriptor = (__bridge CTFontDescriptorRef)item;
            NSDictionary* traits = CFBridgingRelease(CTFontDescriptorCopyAttribute(descriptor, kCTFontTraitsAttribute));
            NSString* family = CFBridgingRelease(CTFontDescriptorCopyAttribute(descriptor, kCTFontFamilyNameAttribute));
            NSURL* file = CFBridgingRelease(CTFontDescriptorCopyAttribute(descriptor, kCTFontURLAttribute));
            const unsigned int symbolic = [traits[(__bridge NSString*)kCTFontSymbolicTrait] unsignedIntValue];
            const double distance = std::abs([traits[(__bridge NSString*)kCTFontWeightTrait] doubleValue]);

            if (family == nil || file.path == nil || (symbolic & kCTFontTraitMonoSpace) == 0 || (symbolic & kCTFontTraitItalic) != 0) {
                continue;
            }

            const std::string name = MacText::utf8(family);

            if (const auto known = chosen.find(name); known == chosen.end() || distance < known->second.first) {
                chosen[name] = {distance, std::filesystem::path(file.fileSystemRepresentation)};
            }
        }
    }

    std::vector<InstalledFont> fonts;

    for (const auto& [family, entry] : chosen) {
        fonts.push_back({family, entry.second});
    }

    return fonts;
}

} // namespace workpane::platform
