# Centers every row of text of the code editor in its line height, which the library leaves at the top of a spaced row.
# The configure step fails when the line this patch replaces is missing, so a new pin of the library cannot skip it silently, and a source that already carries the patch is left as it is.
file(READ "${SOURCE}" content)

set(rows "ImVec2 rowScreenPos = cursorScreenPos + ImVec2(textLeftOffset, firstVisibleRow * glyphSize.y);\n\tauto firstRenderableColumn")
set(centeredRows "ImVec2 rowScreenPos = cursorScreenPos + ImVec2(textLeftOffset, firstVisibleRow * glyphSize.y + std::floor((glyphSize.y - fontSize) * 0.5f));\n\tauto firstRenderableColumn")

string(FIND "${content}" "${centeredRows}" patched)

if(NOT patched EQUAL -1)
    return()
endif()

string(FIND "${content}" "${rows}" found)

if(found EQUAL -1)
    message(FATAL_ERROR "The code editor library no longer holds the line the row centering patch replaces: ${rows}")
endif()

string(REPLACE "${rows}" "${centeredRows}" content "${content}")
file(WRITE "${SOURCE}" "${content}")
