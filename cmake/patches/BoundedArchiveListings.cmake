set(module "${SOURCE}/modules/zip/src/ZipModule.cpp")
set(header "${SOURCE}/modules/zip/include/varn/zip/ZipModule.h")
file(READ "${module}" content)

string(FIND "${content}" "The archive exceeds its listing entry bound" applied)

if(NOT applied EQUAL -1)
    return()
endif()

set(signature "ZipModule::performList(const std::string& zipPath)")
set(allocation [=[
    std::vector<ListedEntry> entries;
    entries.reserve(static_cast<std::size_t>(count));
]=])
set(entry [=[
        if (zip_stat_index(za.get(), i, ZIP_FL_ENC_GUESS, &stat) != 0 || (stat.valid & ZIP_STAT_NAME) == 0)
]=])
set(start [=[
int ZipModule::luaList(lua_State* L)
{
#if defined(VARN_HAVE_LIBZIP) && VARN_HAVE_LIBZIP
]=])
set(task [=[
    return varn::async::AsyncTask::runOnPool(L, rt, rt.taskPool(), "ZipModule", [zipPath](Promise& promise)
    {
        std::vector<ListedEntry> entries = performList(zipPath);
]=])

foreach(expected IN ITEMS "${signature}" "${allocation}" "${entry}" "${start}" "${task}")
    string(FIND "${content}" "${expected}" found)

    if(found EQUAL -1)
        message(FATAL_ERROR "The pinned ZIP module no longer declares the listing the bound patch expects")
    endif()
endforeach()

set(boundedAllocation [=[
    if (static_cast<std::uint64_t>(count) > limit)
    {
        throw std::runtime_error("[ZipModule] The archive exceeds its listing entry bound.");
    }

    std::vector<ListedEntry> entries;
    entries.reserve(static_cast<std::size_t>(count));
]=])
set(boundedStart [=[
int ZipModule::luaList(lua_State* L)
{
#if defined(VARN_HAVE_LIBZIP) && VARN_HAVE_LIBZIP
    if (!lua_isnoneornil(L, 2) && !lua_isinteger(L, 2))
    {
        return luaL_error(L, "[ZipModule] The listing entry bound must be an integer.");
    }

    const lua_Integer limit = luaL_optinteger(L, 2, maximumListedEntries);

    if (limit < 1 || limit > maximumListedEntries)
    {
        return luaL_error(L, "[ZipModule] The listing entry bound is outside its permitted range.");
    }

]=])
set(boundedTask [=[
    return varn::async::AsyncTask::runOnPool(L, rt, rt.taskPool(), "ZipModule", [zipPath, limit](Promise& promise)
    {
        std::vector<ListedEntry> entries = performList(zipPath, static_cast<std::uint64_t>(limit));
]=])
string(REPLACE "${signature}" "ZipModule::performList(const std::string& zipPath, std::uint64_t limit)" content "${content}")
string(REPLACE "${allocation}" "${boundedAllocation}" content "${content}")
string(REPLACE "${entry}" "        if (zip_stat_index(za.get(), i, ZIP_FL_ENC_GUESS, &stat) != 0 || (stat.valid & ZIP_STAT_NAME) == 0 || (stat.valid & ZIP_STAT_SIZE) == 0 || (stat.valid & ZIP_STAT_COMP_SIZE) == 0)\n" content "${content}")
string(REPLACE "${start}" "${boundedStart}" content "${content}")
string(REPLACE "${task}" "${boundedTask}" content "${content}")
file(WRITE "${module}" "${content}")

file(READ "${header}" declarations)
set(declaration "    static std::vector<ListedEntry> performList(const std::string& zipPath);")
string(FIND "${declarations}" "${declaration}" declared)

if(declared EQUAL -1)
    message(FATAL_ERROR "The pinned ZIP module no longer declares the method the bound patch expects")
endif()

string(REPLACE "${declaration}" "    static constexpr std::int64_t maximumListedEntries{100000};\n\n    static std::vector<ListedEntry> performList(const std::string& zipPath, std::uint64_t limit);" declarations "${declarations}")
file(WRITE "${header}" "${declarations}")
