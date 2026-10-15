# Keeps the number of every marker and squiggle after a deletion, which the library stores in a boolean and so gives every overlay after the first the number of the second.
# The configure step fails when neither the stored field nor its replacement is found, so a new pin of the library cannot skip it silently, and a source already fixed is left as it is.
file(READ "${SOURCE}" content)

set(broken [=[
			bool used = false;
			bool index = 0;
]=])

set(fixed [=[
			bool used = false;
			size_t index = 0;
]=])

string(FIND "${content}" "${broken}" found)
string(FIND "${content}" "${fixed}" patched)

if(found EQUAL -1 AND NOT patched EQUAL -1)
    return()
endif()

if(found EQUAL -1)
    message(FATAL_ERROR "The code editor library no longer holds the overlay numbers the renumbering patch fixes")
endif()

string(REPLACE "${broken}" "${fixed}" content "${content}")
file(WRITE "${SOURCE}" "${content}")
