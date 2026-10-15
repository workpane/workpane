#include "platform/macos/MacTextHelper.h"

namespace workpane::platform {

NSString* MacTextHelper::string(std::string_view text) {
    return [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
}

std::string MacTextHelper::utf8(NSString* value) {
    NSData* data = [value dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:YES];
    return data == nil ? std::string() : std::string(static_cast<const char*>(data.bytes), data.length);
}

} // namespace workpane::platform
