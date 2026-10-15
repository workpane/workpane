# Draws the guide of a pair of brackets only beside the visible lines indented past the pair, as the indent guides of Visual Studio Code, so a block whose body is not indented, such as a namespace, draws none.
# A blank line counts as indented as its neighbors are, or just past the shallower one when they differ, so a guide crosses the blank lines inside a body and stops at those between two blocks.
# The configure step fails when neither the guide loop nor its replacement is found, so a new pin of the library cannot skip it silently, and a source already patched is left as it is.
file(READ "${SOURCE}" content)

set(broken [=[
		// render bracket pair lines
		for (auto bracket = bracketeer.begin(); bracket < bracketeer.end(); bracket++) {
			if (bracket->visible && bracket->end.line - bracket->start.line > 1) {
				auto column = std::min(docPos2VisPos(bracket->start).column, docPos2VisPos(bracket->end).column);

				for (size_t i = bracket->start.line + 1; i < bracket->end.line; i++) {
					const auto& line = document[i];

					if (line.foldingState != FoldingState::hidden) {
]=])

set(fixed [=[
		// render bracket pair lines only beside the visible lines indented past their pair
		auto firstLine = typeSetter[std::min(firstVisibleRow, lastVisibleRow)].line;
		auto lastLine = typeSetter[lastVisibleRow].line;

		auto blank = [this](size_t index) {
			for (auto& glyph : document[index]) {
				if (glyph.codepoint != ' ' && glyph.codepoint != '\t') {
					return false;
				}
			}

			return true;
		};

		auto indent = [&](size_t index, size_t first, size_t last) {
			if (!blank(index)) {
				return document[index].indent;
			}

			auto above = index;
			auto below = index;

			while (above > first && blank(above)) {
				above--;
			}

			while (below < last && blank(below)) {
				below++;
			}

			auto top = document[above].indent;
			auto bottom = document[below].indent;
			return top == bottom ? top : std::min(top, bottom) + 1;
		};

		for (auto bracket = bracketeer.begin(); bracket < bracketeer.end(); bracket++) {
			if (bracket->visible && bracket->end.line - bracket->start.line > 1) {
				auto column = std::min(docPos2VisPos(bracket->start).column, docPos2VisPos(bracket->end).column);

				for (size_t i = std::max(bracket->start.line + 1, firstLine); i < std::min(bracket->end.line, lastLine + 1); i++) {
					const auto& line = document[i];

					if (line.foldingState != FoldingState::hidden && indent(i, bracket->start.line, bracket->end.line) > column) {
]=])

string(FIND "${content}" "${broken}" found)
string(FIND "${content}" "${fixed}" patched)

if(found EQUAL -1 AND NOT patched EQUAL -1)
    return()
endif()

if(found EQUAL -1)
    message(FATAL_ERROR "The code editor library no longer draws its bracket guides where the indentation patch expects")
endif()

string(REPLACE "${broken}" "${fixed}" content "${content}")
file(WRITE "${SOURCE}" "${content}")
