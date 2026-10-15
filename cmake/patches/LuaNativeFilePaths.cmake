set(module "${SOURCE}/lauxlib.c")
file(READ "${module}" content)
set(header "#include <string.h>\n")
set(open "    lf.f = fopen(filename, \"r\");")
set(reopen "      lf.f = freopen(filename, \"rb\", lf.f);")
set(native [=[
#include <string.h>

#if defined(_WIN32)
#include <filesystem>
#include <string>

static FILE *lua_native_file(const char *filename, const wchar_t *mode, FILE *previous) {
  const std::filesystem::path path(std::u8string(filename, filename + strlen(filename)));
  return previous == NULL ? _wfopen(path.c_str(), mode) : _wfreopen(path.c_str(), mode, previous);
}
#endif
]=])
set(nativeOpen [=[
#if defined(_WIN32)
    lf.f = lua_native_file(filename, L"r", NULL);
#else
    lf.f = fopen(filename, "r");
#endif
]=])
set(nativeReopen [=[
#if defined(_WIN32)
      lf.f = lua_native_file(filename, L"rb", lf.f);
#else
      lf.f = freopen(filename, "rb", lf.f);
#endif
]=])

string(FIND "${content}" "static FILE *lua_native_file" applied)

if(NOT applied EQUAL -1)
    return()
endif()

foreach(expected IN ITEMS "${header}" "${open}" "${reopen}")
    string(FIND "${content}" "${expected}" found)

    if(found EQUAL -1)
        message(FATAL_ERROR "The pinned Lua auxiliary library no longer declares the file loader the patch expects")
    endif()
endforeach()

string(REPLACE "${header}" "${native}" content "${content}")
string(REPLACE "${open}" "${nativeOpen}" content "${content}")
string(REPLACE "${reopen}" "${nativeReopen}" content "${content}")
file(WRITE "${module}" "${content}")
