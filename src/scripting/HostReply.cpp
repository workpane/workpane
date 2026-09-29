#include "scripting/HostReply.h"

#include <utility>

namespace workpane::scripting {

nlohmann::json HostReply::success(nlohmann::json value) {
    return {{"ok", true}, {"value", std::move(value)}};
}

nlohmann::json HostReply::failure(const Error& error) {
    return {{"ok", false}, {"error", {{"code", error.code}, {"message", error.message}, {"detail", error.detail}}}};
}

} // namespace workpane::scripting
