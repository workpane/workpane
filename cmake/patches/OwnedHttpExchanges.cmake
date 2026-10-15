set(module "${SOURCE}/modules/http/src/HttpClientModule.cpp")
set(header "${SOURCE}/modules/http/include/varn/http/HttpClientModule.h")
set(sources "${SOURCE}/modules/http/http.cmake")
file(READ "${module}" content)

string(FIND "${content}" "HttpClientExchangeLua::push" applied)

if(NOT applied EQUAL -1)
    return()
endif()

set(request [=[
int HttpClientModule::luaClientRequest(lua_State* L)
{
]=])
set(stream [=[
int HttpClientModule::luaClientStream(lua_State* L)
{
]=])
set(requestResult [=[
    luaClient(L).send(std::move(request), std::move(callbacks));
    Promise::push(L, promise);
]=])
set(streamResult [=[
    state->handle = luaClient(L).send(std::move(request), std::move(callbacks));
    Promise::push(L, promise);
]=])

foreach(expected IN ITEMS "${request}" "${stream}" "${requestResult}" "${streamResult}" "#include \"HttpClientPerform.h\"" "local streamRaw = client.streamRaw" "-- Streams the response body in chunks and invokes `onChunk` for each piece as it arrives." "    installPrelude(L);")
    string(FIND "${content}" "${expected}" found)

    if(found EQUAL -1)
        message(FATAL_ERROR "The pinned HTTP client no longer declares the request implementation the ownership patch expects")
    endif()
endforeach()

set(ownedRequest [=[
int HttpClientModule::luaClientRequest(lua_State* L)
{
    return startRequest(L, ResultKind::Promise);
}

int HttpClientModule::luaClientExchange(lua_State* L)
{
    return lua_isnoneornil(L, 2) ? startRequest(L, ResultKind::Exchange) : startStream(L, ResultKind::Exchange);
}

int HttpClientModule::startRequest(lua_State* L, ResultKind kind)
{
]=])
set(ownedStream [=[
int HttpClientModule::luaClientStream(lua_State* L)
{
    return startStream(L, ResultKind::Promise);
}

int HttpClientModule::startStream(lua_State* L, ResultKind kind)
{
]=])
set(ownedRequestResult [=[
    const auto handle = luaClient(L).send(std::move(request), std::move(callbacks));

    if (kind == ResultKind::Exchange)
    {
        HttpClientExchangeLua::push(L, promise, handle);
    }
    else
    {
        Promise::push(L, promise);
    }
]=])
set(ownedStreamResult [=[
    state->handle = luaClient(L).send(std::move(request), std::move(callbacks));

    if (kind == ResultKind::Exchange)
    {
        HttpClientExchangeLua::push(L, promise, state->handle);
    }
    else
    {
        Promise::push(L, promise);
    }
]=])
string(REPLACE "${request}" "${ownedRequest}" content "${content}")
string(REPLACE "${stream}" "${ownedStream}" content "${content}")
string(REPLACE "${requestResult}" "${ownedRequestResult}" content "${content}")
string(REPLACE "${streamResult}" "${ownedStreamResult}" content "${content}")
string(REPLACE "#include \"HttpClientPerform.h\"" "#include \"HttpClientExchangeLua.h\"\n#include \"HttpClientPerform.h\"" content "${content}")
string(REPLACE "local streamRaw = client.streamRaw" "local streamRaw = client.streamRaw\nlocal exchangeRaw = client.exchangeRaw" content "${content}")
set(streamPrelude [=[
-- Streams the response body in chunks and invokes `onChunk` for each piece as it arrives.
]=])
set(exchangePrelude [=[
function client.exchange(opts, onChunk)
    opts = opts or {}
    return exchangeRaw(buildOptions(opts), onChunk, opts.onResponse)
end

]=])
string(REPLACE "${streamPrelude}" "${exchangePrelude}${streamPrelude}" content "${content}")
string(REPLACE "    installPrelude(L);" "    lua_pushcfunction(L, &HttpClientModule::luaClientExchange);\n    lua_setfield(L, -2, \"exchangeRaw\");\n\n    installPrelude(L);" content "${content}")
file(WRITE "${module}" "${content}")

file(READ "${header}" declarations)
string(FIND "${declarations}" "    static int luaClientRequest(lua_State* L);" declared)

if(declared EQUAL -1)
    message(FATAL_ERROR "The pinned HTTP client no longer declares the module the ownership patch expects")
endif()

string(REPLACE "    static int luaClientRequest(lua_State* L);" "    enum class ResultKind { Promise, Exchange };\n\n    static int luaClientExchange(lua_State* L);\n    static int startRequest(lua_State* L, ResultKind kind);\n    static int startStream(lua_State* L, ResultKind kind);\n    static int luaClientRequest(lua_State* L);" declarations "${declarations}")
file(WRITE "${header}" "${declarations}")

file(WRITE "${SOURCE}/modules/http/src/HttpClientExchangeLua.h" [=[
#pragma once

#include "varn/async/Promise.h"
#include "varn/http/HttpClientHandle.h"

#include <lua.hpp>

#include <memory>

namespace varn::http {

class HttpClientExchangeLua final {
  public:
    static void push(lua_State* state, const std::shared_ptr<async::Promise>& promise, client::Handle handle);

  private:
    static constexpr const char* metatable{"varn.http.exchange"};

    explicit HttpClientExchangeLua(client::Handle handle);
    ~HttpClientExchangeLua();

    static int cancel(lua_State* state);
    static int collect(lua_State* state);

    client::Handle m_handle;
};

}
]=])

file(WRITE "${SOURCE}/modules/http/src/HttpClientExchangeLua.cpp" [=[
#include "HttpClientExchangeLua.h"

#include <new>
#include <utility>

namespace varn::http {

HttpClientExchangeLua::HttpClientExchangeLua(client::Handle handle) : m_handle(std::move(handle)) {}

HttpClientExchangeLua::~HttpClientExchangeLua() {
    m_handle.cancel();
}

void HttpClientExchangeLua::push(lua_State* state, const std::shared_ptr<async::Promise>& promise, client::Handle handle) {
    lua_createtable(state, 0, 2);
    async::Promise::push(state, promise);
    lua_setfield(state, -2, "promise");
    void* storage = lua_newuserdatauv(state, sizeof(HttpClientExchangeLua), 0);
    new (storage) HttpClientExchangeLua(std::move(handle));

    if (luaL_newmetatable(state, metatable)) {
        lua_pushcfunction(state, &HttpClientExchangeLua::collect);
        lua_setfield(state, -2, "__gc");
        lua_pushstring(state, metatable);
        lua_setfield(state, -2, "__metatable");
    }

    lua_setmetatable(state, -2);
    lua_pushcclosure(state, &HttpClientExchangeLua::cancel, 1);
    lua_setfield(state, -2, "cancel");
}

int HttpClientExchangeLua::cancel(lua_State* state) {
    auto* exchange = static_cast<HttpClientExchangeLua*>(luaL_checkudata(state, lua_upvalueindex(1), metatable));
    exchange->m_handle.cancel();

    return 0;
}

int HttpClientExchangeLua::collect(lua_State* state) {
    static_cast<HttpClientExchangeLua*>(luaL_checkudata(state, 1, metatable))->~HttpClientExchangeLua();

    return 0;
}

}
]=])

file(READ "${sources}" listed)
string(FIND "${listed}" "    \"\${CMAKE_CURRENT_LIST_DIR}/src/HttpClientModule.cpp\"" registered)

if(registered EQUAL -1)
    message(FATAL_ERROR "The pinned HTTP client no longer lists the source the ownership patch expects")
endif()

string(REPLACE "    \"\${CMAKE_CURRENT_LIST_DIR}/src/HttpClientModule.cpp\"" "    \"\${CMAKE_CURRENT_LIST_DIR}/src/HttpClientExchangeLua.cpp\"\n    \"\${CMAKE_CURRENT_LIST_DIR}/src/HttpClientModule.cpp\"" listed "${listed}")
file(WRITE "${sources}" "${listed}")
