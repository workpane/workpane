# Moves to the start or the end of the line with Command and the left or the right arrow on macOS, as every text view of macOS does, and selects up to there with Shift.
# The configure step fails when neither the word movements the keys follow nor the keys themselves are found, so a new pin of the library cannot skip it silently, and a source that already has them is left as it is.
file(READ "${SOURCE}" content)

set(anchor [=[
		else if (ImGui::Shortcut((macOS ? ImGuiMod_Alt : ImGuiMod_Ctrl) | ImGuiMod_Shift | ImGuiKey_RightArrow, ImGuiInputFlags_Repeat)) { moveRight(true, true); }
]=])

set(keys [=[
		else if (macOS && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_LeftArrow)) { moveToStartOfLine(false); }
		else if (macOS && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_LeftArrow)) { moveToStartOfLine(true); }
		else if (macOS && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_RightArrow)) { moveToEndOfLine(false); }
		else if (macOS && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_RightArrow)) { moveToEndOfLine(true); }
]=])

string(FIND "${content}" "${anchor}${keys}" patched)

if(NOT patched EQUAL -1)
    return()
endif()

string(FIND "${content}" "${anchor}" found)

if(found EQUAL -1)
    message(FATAL_ERROR "The code editor library no longer moves by word where the Command arrows patch expects")
endif()

string(REPLACE "${anchor}" "${anchor}${keys}" content "${content}")
file(WRITE "${SOURCE}" "${content}")
