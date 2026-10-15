#pragma once

#import <Foundation/Foundation.h>

#include <string>
#include <string_view>

namespace workpane::platform {

// Converts between the UTF-8 text of the product and the strings Cocoa speaks, writing a replacement for what UTF-8 cannot carry, such as a lone surrogate a page sets.
class MacTextHelper final {
  public:
    [[nodiscard]] static NSString* string(std::string_view text);
    [[nodiscard]] static std::string utf8(NSString* value);
};

} // namespace workpane::platform
