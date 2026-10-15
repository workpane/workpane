# Keeps the editor on the lines its document still holds after the text shrinks.
# The first visible line is read from the rows laid out for the previous text before they are laid out again, so it is taken from the row alone and bounded by the document, and a line to scroll to is bounded by the last line instead of the line count.
# The configure step fails when neither the reads nor the bounded ones are found, so a new pin of the library cannot skip it silently, and a source that already has them is left as it is.
file(READ "${SOURCE}" content)

set(firstLine [=[
	auto previousFirstLine = visPos2DocPos(VisPos(firstVisibleRow, 0)).line;
]=])

set(boundedFirstLine [=[
	auto previousFirstLine = firstVisibleRow < typeSetter.size() ? std::min(typeSetter[firstVisibleRow].line, document.size() - 1) : 0;
]=])

set(scrollLine [=[
	scrollToLineNumber = std::min(line, document.size());
	scrollToAlignment = alignment;
	scrollToFraction = fraction;

	if (config.lineFolding) {
		lineFold.unfoldAroundLine(document, line);
	}
]=])

set(boundedScrollLine [=[
	scrollToLineNumber = std::min(line, document.size() - 1);
	scrollToAlignment = alignment;
	scrollToFraction = fraction;

	if (config.lineFolding) {
		lineFold.unfoldAroundLine(document, scrollToLineNumber);
	}
]=])

string(FIND "${content}" "${boundedFirstLine}" firstPatched)
string(FIND "${content}" "${boundedScrollLine}" scrollPatched)

if(NOT firstPatched EQUAL -1 AND NOT scrollPatched EQUAL -1)
    return()
endif()

string(FIND "${content}" "${firstLine}" firstFound)
string(FIND "${content}" "${scrollLine}" scrollFound)

if(firstFound EQUAL -1 OR scrollFound EQUAL -1)
    message(FATAL_ERROR "The code editor library no longer reads its first visible line or bounds its scroll where the lines patch expects")
endif()

string(REPLACE "${firstLine}" "${boundedFirstLine}" content "${content}")
string(REPLACE "${scrollLine}" "${boundedScrollLine}" content "${content}")
file(WRITE "${SOURCE}" "${content}")
