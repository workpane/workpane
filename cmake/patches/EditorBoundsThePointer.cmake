# Keeps the conversion of a pointer position to a row and a column inside the range of the integers it lands in.
# A pointer outside the window stands at the most negative float, which converts to an unsigned integer with undefined behavior before the editor checks it, so the position is bounded first.
# The configure step fails when neither the conversions nor the bounded ones are found, so a new pin of the library cannot skip it silently, and a source that already has them is left as it is.
file(READ "${SOURCE}" content)

set(conversions [=[
	size_t colNo = static_cast<size_t>(screenPos.x);
	size_t rowNo = static_cast<size_t>(screenPos.y);
]=])

set(bounded [=[
	size_t colNo = static_cast<size_t>(std::clamp(screenPos.x, 0.0f, 1.0e9f));
	size_t rowNo = static_cast<size_t>(std::clamp(screenPos.y, 0.0f, 1.0e9f));
]=])

string(FIND "${content}" "${bounded}" patched)

if(NOT patched EQUAL -1)
    return()
endif()

string(FIND "${content}" "${conversions}" found)

if(found EQUAL -1)
    message(FATAL_ERROR "The code editor library no longer converts the pointer where the pointer patch expects")
endif()

string(REPLACE "${conversions}" "${bounded}" content "${content}")
file(WRITE "${SOURCE}" "${content}")
