file(READ "${CONFIG}" configuration)
string(JSON source GET "${configuration}" source)
string(JSON logo GET "${configuration}" logo)
string(JSON folders LENGTH "${configuration}" plugins)

file(REMOVE_RECURSE "${OUTPUT}/lua" "${OUTPUT}/plugins" "${OUTPUT}/fonts" "${OUTPUT}/images")
file(MAKE_DIRECTORY "${OUTPUT}/plugins" "${OUTPUT}/images")
file(COPY "${source}/lua/" DESTINATION "${OUTPUT}/lua")
file(COPY "${source}/assets/fonts/" DESTINATION "${OUTPUT}/fonts")

if(folders GREATER 0)
    math(EXPR last "${folders} - 1")

    foreach(index RANGE ${last})
        string(JSON folder GET "${configuration}" plugins ${index})
        file(COPY "${folder}/" DESTINATION "${OUTPUT}/plugins")
    endforeach()
endif()

if(NOT logo STREQUAL "")
    file(COPY_FILE "${logo}" "${OUTPUT}/images/logo.png")
endif()

file(TOUCH "${STAMP}")
