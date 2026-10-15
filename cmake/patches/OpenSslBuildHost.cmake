set(module "${SOURCE}/cmake/openssl.cmake")
file(READ "${module}" content)
set(previous [=[
CPMAddPackage(
    NAME openssl-cmake
]=])
set(current [=[
workpane_patches(workpaneOpenSslPatches openssl-cmake tar.gz <SOURCE_DIR> OpenSslHostArchitecture.cmake OpenSslBuildSource.cmake)
CPMAddPackage(
    NAME openssl-cmake
    ${workpaneOpenSslPatches}
]=])

string(FIND "${content}" "${current}" applied)

if(NOT applied EQUAL -1)
    return()
endif()

string(FIND "${content}" "${previous}" found)

if(found EQUAL -1)
    message(FATAL_ERROR "The pinned runtime no longer declares the OpenSSL recipe the patch expects")
endif()

string(REPLACE "${previous}" "${current}" content "${content}")
file(WRITE "${module}" "${content}")
