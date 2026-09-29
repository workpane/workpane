#pragma once

namespace workpane::process {

// The stream a program wrote to, which a plugin reads apart because a program writes its answers and its diagnostics to different ones.
enum class ProcessStream { Output, Error };

} // namespace workpane::process
