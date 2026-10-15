#include "platform/macos/MacFonts.h"

#include "platform/macos/MacTextHelper.h"

#import <CoreText/CoreText.h>
#import <Foundation/Foundation.h>

#include <cmath>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
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

            const std::string name = MacTextHelper::utf8(family);

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

// The color emoji face of the system and then the faces Core Text falls back to from its monospaced face for the languages of the reader, in its order.
// Core Text keeps the emoji face far down its cascade, behind symbol faces that draw emoji without color, so it comes first.
std::vector<FallbackFace> MacFonts::fallbacks() {
    std::vector<FallbackFace> faces;
    std::set<std::string> names;

    @autoreleasepool {
        NSMutableArray* descriptors = [NSMutableArray arrayWithObject:CFBridgingRelease(CTFontDescriptorCreateWithNameAndSize(CFSTR("AppleColorEmoji"), 0.0))];
        CTFontRef base = CTFontCreateUIFontForLanguage(kCTFontUIFontUserFixedPitch, 0.0, nullptr);

        if (base != nullptr) {
            NSArray* cascade = CFBridgingRelease(CTFontCopyDefaultCascadeListForLanguages(base, (__bridge CFArrayRef)[NSLocale preferredLanguages]));
            CFRelease(base);

            if (cascade != nil) {
                [descriptors addObjectsFromArray:cascade];
            }
        }

        for (id item in descriptors) {
            CTFontRef font = CTFontCreateWithFontDescriptor((__bridge CTFontDescriptorRef)item, 0.0, nullptr);
            NSURL* file = CFBridgingRelease(CTFontCopyAttribute(font, kCTFontURLAttribute));
            NSString* name = CFBridgingRelease(CTFontCopyPostScriptName(font));
            const bool color = (CTFontGetSymbolicTraits(font) & kCTFontTraitColorGlyphs) != 0;
            CFRelease(font);
            const auto index = file.path == nil || name == nil ? std::nullopt : faceIndex(file, name);

            if (!index.has_value() || !names.insert(MacTextHelper::utf8(name)).second) {
                continue;
            }

            faces.push_back({std::filesystem::path(file.fileSystemRepresentation), *index, color});

            if (faces.size() == FallbackFace::largestCount) {
                break;
            }
        }
    }

    return faces;
}

// A file may hold several faces, and the position of the one with the given PostScript name is the index FreeType reads it by.
std::optional<unsigned int> MacFonts::faceIndex(NSURL* file, NSString* name) {
    NSArray* descriptors = CFBridgingRelease(CTFontManagerCreateFontDescriptorsFromURL((__bridge CFURLRef)file));

    for (NSUInteger position = 0; position < descriptors.count; ++position) {
        NSString* described = CFBridgingRelease(CTFontDescriptorCopyAttribute((__bridge CTFontDescriptorRef)descriptors[position], kCTFontNameAttribute));

        if ([described isEqualToString:name]) {
            return static_cast<unsigned int>(position);
        }
    }

    return std::nullopt;
}

} // namespace workpane::platform
