set(module "${SOURCE}/cmake/dependencies.cmake")
file(READ "${module}" content)
set(previous [=[
CPMAddPackage(
    SYSTEM YES
    NAME lua
]=])
set(current [=[
workpane_patches(workpaneLuaPatches lua tar.gz <SOURCE_DIR> LuaNativeFilePaths.cmake)
CPMAddPackage(
    ${workpaneLuaPatches}
    SYSTEM YES
    NAME lua
]=])

string(FIND "${content}" "${current}" applied)

if(NOT applied EQUAL -1)
    return()
endif()

string(FIND "${content}" "${previous}" found)

if(found EQUAL -1)
    message(FATAL_ERROR "The pinned runtime no longer declares the Lua source the file path patch expects")
endif()

string(REPLACE "${previous}" "${current}" content "${content}")
file(WRITE "${module}" "${content}")
