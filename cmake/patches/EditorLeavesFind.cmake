# Leaves finding to the find bar of the product, so the keys of the editor library no longer open its own find window or step through its own search.
# The configure step fails when the find keys remain in another form, so a new pin of the library cannot skip it silently, and a source that already lost them is left as it is.
file(READ "${SOURCE}" content)

set(shortcuts [=[
		// find/replace support
		else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_F)) {
			if (autocomplete.isActive()) {
				autocomplete.cancel();
				findCancelledAutocomplete = true;
			}

			openFindReplace();
		}

		else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_F)) { findAll(); }
		else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_G, ImGuiInputFlags_Repeat)) { findNext(); }
]=])

string(FIND "${content}" "${shortcuts}" found)
string(FIND "${content}" "openFindReplace();\n\t\t}" reachable)

if(found EQUAL -1 AND reachable EQUAL -1)
    return()
endif()

if(found EQUAL -1)
    message(FATAL_ERROR "The code editor library no longer holds the find keys the find patch removes")
endif()

string(REPLACE "${shortcuts}" "" content "${content}")
file(WRITE "${SOURCE}" "${content}")
