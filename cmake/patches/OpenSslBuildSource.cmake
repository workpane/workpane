set(module "${SOURCE}/cmake/FetchOpenSSL.cmake")
file(READ "${module}" content)
set(previous "if(DEFINED CACHE{openssl-source_SOURCE_DIR_OLD} AND NOT openssl-source_SOURCE_DIR STREQUAL openssl-source_SOURCE_DIR_OLD)")
set(current "if(NOT openssl-source_SOURCE_DIR STREQUAL openssl-source_SOURCE_DIR_OLD)")

string(FIND "${content}" "${current}" applied)

if(NOT applied EQUAL -1)
    return()
endif()

string(FIND "${content}" "${previous}" found)

if(found EQUAL -1)
    message(FATAL_ERROR "The pinned OpenSSL recipe no longer declares the source identity check the patch expects")
endif()

string(REPLACE "${previous}" "${current}" content "${content}")
file(WRITE "${module}" "${content}")
