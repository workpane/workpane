set(module "${SOURCE}/cmake/openssl.cmake")
file(READ "${module}" content)
set(previous [=[
    VERSION 3.6.2
    URL https://github.com/openssl/openssl/releases/download/openssl-3.6.2/openssl-3.6.2.tar.gz
    URL_HASH SHA256=aaf51a1fe064384f811daeaeb4ec4dce7340ec8bd893027eee676af31e83a04f
]=])
set(current [=[
    VERSION 3.6.5
    URL https://github.com/openssl/openssl/releases/download/openssl-3.6.5/openssl-3.6.5.tar.gz
    URL_HASH SHA256=a2157c2830efdec3788939b00c9b0638306d3f0bbb76dc4832ee503bb397df98
]=])
set(previousOptions "\"OPENSSL_TARGET_VERSION 3.6.2\"")
set(currentOptions "\"OPENSSL_TARGET_VERSION 3.6.5\"")

string(FIND "${content}" "${current}" applied)
string(FIND "${content}" "${currentOptions}" configured)

if(NOT applied EQUAL -1 AND NOT configured EQUAL -1)
    return()
endif()

foreach(expected IN ITEMS "${previous}" "${previousOptions}")
    string(FIND "${content}" "${expected}" found)

    if(found EQUAL -1)
        message(FATAL_ERROR "The pinned runtime no longer declares the OpenSSL source release the patch expects")
    endif()
endforeach()

string(REPLACE "${previous}" "${current}" content "${content}")
string(REPLACE "${previousOptions}" "${currentOptions}" content "${content}")
file(WRITE "${module}" "${content}")
