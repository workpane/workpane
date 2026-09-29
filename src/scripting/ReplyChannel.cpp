#include "scripting/ReplyChannel.h"

#include "scripting/HostReply.h"
#include "scripting/ScriptRuntime.h"

namespace workpane::scripting {

ReplyChannel::ReplyChannel(ScriptRuntime& runtime) : m_runtime(runtime) {}

void ReplyChannel::reply(std::int64_t request, const Result<nlohmann::json>& result) {
    nlohmann::json payload = result.hasValue() ? HostReply::success(result.value()) : HostReply::failure(result.error());
    payload["request"] = request;
    m_runtime.emit("workpane.reply", payload);
}

} // namespace workpane::scripting
