set(module "${SOURCE}/cmake/FindVcvarsall.cmake")
file(READ "${module}" content)
set(previous [=[
        elseif(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "AMD64")
            set(VCVARSALL_ARCH x64)
        endif()
]=])
set(current [=[
        elseif(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "AMD64")
            set(VCVARSALL_ARCH x64)
        elseif(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "ARM64")
            set(VCVARSALL_ARCH arm64)
        endif()
]=])

string(FIND "${content}" "${current}" applied)

if(NOT applied EQUAL -1)
    return()
endif()

string(FIND "${content}" "${previous}" found)

if(found EQUAL -1)
    message(FATAL_ERROR "The pinned OpenSSL recipe no longer declares the host architecture selection the patch expects")
endif()

string(REPLACE "${previous}" "${current}" content "${content}")
file(WRITE "${module}" "${content}")
