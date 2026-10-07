#include "platform/KeyRepeat.h"

#import <AppKit/AppKit.h>

namespace workpane::platform {

std::optional<KeyRepeat> KeyRepeat::system() {
    return KeyRepeat{static_cast<float>([NSEvent keyRepeatDelay]), static_cast<float>([NSEvent keyRepeatInterval])};
}

} // namespace workpane::platform
