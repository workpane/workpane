#include "ui/components/views/CodeText.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace workpane::ui {

// A text its plugin gives is loaded exactly as written, tabs included, whatever the tab key inserts while the reader types.
void CodeText::load(std::string_view text) {
    const bool spaces = config.insertSpacesOnTabs;
    config.insertSpacesOnTabs = false;
    SetText(text);
    config.insertSpacesOnTabs = spaces;
    m_loaded = true;
}

// A loaded text is not an edit of the reader, so the change it starts is settled after the frame that loaded it, unless the reader already edited it in that frame.
void CodeText::settleLoad() {
    if (std::exchange(m_loaded, false) && GetUndoIndex() == 0) {
        settleChange();
    }
}

bool CodeText::holds(TextEditor::DocPos position) const {
    return position.line < document.size() && position.index <= document[position.line].size();
}

// The language colors the lines that changed first, so the roles painted afterwards stay until those lines change again.
// Replacing the highlights colors the whole document again first, so a range that lost its role takes the color of its language back.
void CodeText::paint(const std::vector<Highlight>& highlights, bool recolor) {
    if (recolor) {
        recolorDocument();
    }

    colorizer.update(config, document);

    for (const auto& highlight : highlights) {
        if (highlight.line >= document.size()) {
            continue;
        }

        auto& line = document[highlight.line];
        const std::size_t end = std::min(line.size(), highlight.column + highlight.length);

        for (std::size_t index = highlight.column; index < end; ++index) {
            line[index].color = highlight.role;
        }
    }
}

// Every glyph takes the color of its language again, or of plain text without one, and the bracket pairs are found and colored again over them.
void CodeText::recolorDocument() {
    const Language* language = config.language;
    config.language = nullptr;
    colorizer.update(config, document);
    config.language = language;
    colorizer.update(config, document);

    if (language == nullptr) {
        for (auto& line : document) {
            for (auto& glyph : line) {
                glyph.color = TextEditor::Color::text;
            }
        }
    }

    const bool brackets = config.showMatchingBrackets;
    config.showMatchingBrackets = false;
    bracketeer.update(config, document);
    config.showMatchingBrackets = brackets;
    bracketeer.update(config, document);
}

float CodeText::textLeft() const {
    return textLeftOffset;
}

bool CodeText::changePending() const {
    return delayedChangeDetected;
}

// The report is due just after its time, because the library reports only once the clock has passed it.
double CodeText::secondsUntilChange() const {
    const std::chrono::duration<double> remaining = delayedChangeReportTime - std::chrono::system_clock::now();
    return std::max(0.0, remaining.count()) + reportMargin;
}

void CodeText::settleChange() {
    delayedChangeDetected = false;
}

// Every range is replaced in one step of the undo history, from the last to the first so the ranges before keep their places, and a range overlapping the one before it is left alone.
void CodeText::replaceEvery(const std::vector<TextEditor::DocSelection>& ranges, std::string_view text) {
    std::vector<TextEditor::DocSelection> kept;

    for (const TextEditor::DocSelection& range : ranges) {
        if (kept.empty() || range.start >= kept.back().end) {
            kept.push_back(range);
        }
    }

    if (kept.empty() || config.readOnly) {
        return;
    }

    auto transaction = startTransaction();

    for (auto range = kept.rbegin(); range != kept.rend(); ++range) {
        deleteText(transaction, range->start, range->end);
        insertText(transaction, range->start, text);
    }

    cursors.clearAdditional();
    endTransaction(transaction);
}

// A range covers one rectangle on every row it spans that the last frame drew, each as wide as the glyphs of the range on that row.
std::vector<ImRect> CodeText::rangeRects(TextEditor::DocPos start, TextEditor::DocPos end) const {
    std::vector<ImRect> rects;
    const TextEditor::VisPos from = docPos2VisPos(normalizePos(start));
    const TextEditor::VisPos to = docPos2VisPos(normalizePos(end));

    for (std::size_t row = std::max(from.row, firstVisibleRow); row <= std::min(to.row, lastVisibleRow); ++row) {
        const std::size_t left = row == from.row ? from.column : 0;
        const std::size_t right = row == to.row ? to.column : docPos2VisPos(visPos2DocPos(TextEditor::VisPos(row, rowEndColumn))).column;
        const float x = cursorScreenPos.x + textLeftOffset;
        const float y = cursorScreenPos.y + static_cast<float>(row) * glyphSize.y;
        rects.emplace_back(x + static_cast<float>(left) * glyphSize.x, y, x + static_cast<float>(std::max(right, left + 1)) * glyphSize.x, y + glyphSize.y);
    }

    return rects;
}

} // namespace workpane::ui
