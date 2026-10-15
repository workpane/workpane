#include "process/ProcessText.h"

#include "text/Utf8Helper.h"

#include <cstddef>

namespace workpane::process {

// Complete characters are answered at once, and a character whose remaining bytes have not arrived yet waits for the next read.
std::string ProcessText::take(std::string_view bytes) {
    m_pending.append(bytes);
    std::string assembled;
    std::size_t index = 0;

    while (index < m_pending.size()) {
        const auto lead = static_cast<unsigned char>(m_pending[index]);
        const std::size_t length = text::Utf8Helper::sequenceLength(lead);
        std::size_t matched = length == 0 ? 0 : 1;

        while (matched > 0 && matched < length && index + matched < m_pending.size() && text::Utf8Helper::follows(lead, matched, static_cast<unsigned char>(m_pending[index + matched]))) {
            ++matched;
        }

        if (length > 0 && matched == length) {
            assembled.append(m_pending, index, length);
            index += length;
            continue;
        }

        // A sequence cut short only by the end of what arrived so far is kept, and anything else is not UTF-8.
        if (length > 0 && index + matched == m_pending.size()) {
            break;
        }

        assembled += replacement;
        index += matched == 0 ? 1 : matched;
    }

    m_pending.erase(0, index);

    return assembled;
}

// The end of a program leaves any unfinished character, which is written as one replacement.
std::string ProcessText::finish() {
    if (m_pending.empty()) {
        return {};
    }

    m_pending.clear();

    return std::string(replacement);
}

} // namespace workpane::process
