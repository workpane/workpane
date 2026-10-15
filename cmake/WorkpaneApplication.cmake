include_guard(GLOBAL)

function(workpane_application_quote output value)
    string(REPLACE "\\" "\\\\" escaped "${value}")
    string(REPLACE "\"" "\\\"" escaped "${escaped}")
    set(${output} "\"${escaped}\"" PARENT_SCOPE)
endfunction()

function(workpane_add_application target)
    cmake_parse_arguments(PARSE_ARGV 1 APP "" "NAME;VERSION;BUILD;ORGANIZATION;IDENTIFIER;DATA_DIRECTORY;LOGO;ICON" "PLUGINS")

    if(APP_UNPARSED_ARGUMENTS OR APP_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "The Workpane application arguments contain an unknown or incomplete option")
    endif()

    if(NOT target MATCHES "^[A-Za-z][A-Za-z0-9_-]*$" OR TARGET "${target}")
        message(FATAL_ERROR "The Workpane application target must be a unique portable executable name")
    endif()

    string(TOLOWER "${target}" resourceName)
    get_property(applicationNames GLOBAL PROPERTY WORKPANE_APPLICATION_NAMES)

    if(resourceName IN_LIST applicationNames)
        message(FATAL_ERROR "Workpane application target names must remain unique when case is ignored: ${target}")
    endif()

    foreach(required IN ITEMS NAME VERSION IDENTIFIER)
        if(NOT DEFINED APP_${required} OR APP_${required} STREQUAL "")
            message(FATAL_ERROR "The Workpane application requires ${required}")
        endif()
    endforeach()

    if(NOT APP_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$" OR NOT APP_IDENTIFIER MATCHES "^[A-Za-z0-9][A-Za-z0-9._-]*$")
        message(FATAL_ERROR "The Workpane application version or identifier is invalid")
    endif()

    if(NOT DEFINED APP_BUILD)
        set(APP_BUILD "1")
    endif()

    if(NOT APP_BUILD MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "The Workpane application build must be a positive integer")
    endif()

    if(NOT DEFINED APP_ORGANIZATION)
        set(APP_ORGANIZATION "${APP_NAME}")
    endif()

    if(NOT DEFINED APP_DATA_DIRECTORY)
        set(APP_DATA_DIRECTORY "${APP_IDENTIFIER}")
    endif()

    if(NOT APP_DATA_DIRECTORY MATCHES "^[A-Za-z0-9][A-Za-z0-9._-]*$" OR APP_DATA_DIRECTORY STREQUAL "." OR APP_DATA_DIRECTORY STREQUAL "..")
        message(FATAL_ERROR "The Workpane application data directory must be a portable directory name")
    endif()

    foreach(text IN ITEMS APP_NAME APP_ORGANIZATION)
        if(${text} MATCHES "[\n\r\t]" OR ${text} STREQUAL "")
            message(FATAL_ERROR "The Workpane application identity contains invalid text")
        endif()
    endforeach()

    get_target_property(source WorkpaneCore WORKPANE_SOURCE_DIRECTORY)
    set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated/workpane-applications/${target}")
    set(runtime "${CMAKE_BINARY_DIR}/bin")

    foreach(field IN ITEMS NAME VERSION BUILD ORGANIZATION IDENTIFIER)
        workpane_application_quote(WORKPANE_APP_${field}_LITERAL "${APP_${field}}")
    endforeach()

    workpane_application_quote(WORKPANE_APP_RESOURCE_LITERAL "${resourceName}")
    workpane_application_quote(WORKPANE_APP_DATA_LITERAL "${APP_DATA_DIRECTORY}")
    configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ApplicationMain.cpp.in" "${generated}/main.cpp" @ONLY)

    set(executableResources "")

    if(WIN32)
        string(REPLACE "." "," WORKPANE_APP_NUMERIC_VERSION "${APP_VERSION}")
        workpane_application_quote(WORKPANE_APP_FILE_VERSION_LITERAL "${APP_VERSION}.${APP_BUILD}")
        configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ApplicationVersion.rc.in" "${generated}/version.rc" @ONLY)
        list(APPEND executableResources "${source}/extras/windows/Workpane.manifest" "${generated}/version.rc")
    endif()

    add_executable(${target} MACOSX_BUNDLE WIN32 "${generated}/main.cpp" ${executableResources})
    target_link_libraries(${target} PRIVATE Workpane::Core)
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${runtime}" CXX_EXTENSIONS OFF)
    workpane_set_warnings(${target})
    workpane_enable_coverage(${target})
    workpane_enable_sanitizers(${target})
    add_dependencies(${target} WorkpaneAskpass)
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "$<TARGET_FILE:WorkpaneAskpass>" "$<TARGET_FILE:varn::varn>")

    if(MSVC)
        target_link_options(${target} PRIVATE /ENTRY:mainCRTStartup)
    endif()

    if(APPLE)
        set(resources "${runtime}/${target}.app/Contents/Resources")
        string(REPLACE "&" "&amp;" displayName "${APP_NAME}")
        string(REPLACE "<" "&lt;" displayName "${displayName}")
        string(REPLACE ">" "&gt;" displayName "${displayName}")
        set(WORKPANE_PRODUCT_NAME "${displayName}")
        set(plist "${generated}/Info.plist.in")
        configure_file("${source}/extras/macos/Info.plist.in" "${plist}" @ONLY)
        set_target_properties(${target} PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${plist}" MACOSX_BUNDLE_GUI_IDENTIFIER "${APP_IDENTIFIER}" MACOSX_BUNDLE_BUNDLE_NAME "${displayName}" MACOSX_BUNDLE_BUNDLE_VERSION "${APP_BUILD}" MACOSX_BUNDLE_SHORT_VERSION_STRING "${APP_VERSION}" INSTALL_RPATH "@executable_path/../Frameworks" BUILD_WITH_INSTALL_RPATH ON)
        add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Frameworks" COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:varn::varn>" "$<TARGET_SONAME_FILE:varn::varn>" "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Frameworks/" COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:WorkpaneAskpass>" "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/MacOS/" VERBATIM)
    else()
        set(resources "${CMAKE_BINARY_DIR}/share/${resourceName}")
        set_target_properties(${target} PROPERTIES INSTALL_RPATH "$ORIGIN/../lib" BUILD_WITH_INSTALL_RPATH ON)

        if(WIN32)
            add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:varn::varn>" "$<TARGET_FILE:WorkpaneAskpass>" "$<TARGET_FILE_DIR:${target}>/" VERBATIM)
        else()
            add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/lib" COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:varn::varn>" "$<TARGET_SONAME_FILE:varn::varn>" "${CMAKE_BINARY_DIR}/lib/" COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:WorkpaneAskpass>" "$<TARGET_FILE_DIR:${target}>/" VERBATIM)
        endif()
    endif()

    if(UNIX)
        add_dependencies(${target} WorkpaneTerminal)
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "$<TARGET_FILE:WorkpaneTerminal>")

        if(APPLE)
            add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:WorkpaneTerminal>" "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/MacOS/" VERBATIM)
        else()
            add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:WorkpaneTerminal>" "$<TARGET_FILE_DIR:${target}>/" VERBATIM)
        endif()
    endif()

    if(DEFINED APP_ICON)
        get_filename_component(icon "${APP_ICON}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

        if(NOT EXISTS "${icon}" OR NOT APPLE)
            message(FATAL_ERROR "The Workpane application icon requires an existing macOS icon file")
        endif()

        set_target_properties(${target} PROPERTIES MACOSX_BUNDLE_ICON_FILE "application.icns")
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${icon}")
        add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${icon}" "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Resources/application.icns" VERBATIM)
    endif()

    workpane_application_quote(sourceJson "${source}")
    set(configuration "{\"source\":${sourceJson},\"logo\":\"\",\"plugins\":[]}")
    file(GLOB_RECURSE files CONFIGURE_DEPENDS "${source}/lua/*" "${source}/assets/fonts/*")
    set(pluginNames "")
    set(index 0)

    foreach(folder IN LISTS APP_PLUGINS)
        get_filename_component(folder "${folder}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

        if(NOT IS_DIRECTORY "${folder}")
            message(FATAL_ERROR "The Workpane application plugin folder does not exist: ${folder}")
        endif()

        file(GLOB children CONFIGURE_DEPENDS LIST_DIRECTORIES true "${folder}/*")

        foreach(child IN LISTS children)
            if(IS_DIRECTORY "${child}")
                get_filename_component(name "${child}" NAME)

                if(name IN_LIST pluginNames)
                    message(FATAL_ERROR "The Workpane application supplies the plugin more than once: ${name}")
                endif()

                list(APPEND pluginNames "${name}")
            endif()
        endforeach()

        workpane_application_quote(folderJson "${folder}")
        string(JSON configuration SET "${configuration}" plugins ${index} "${folderJson}")
        math(EXPR index "${index} + 1")
        file(GLOB_RECURSE pluginFiles CONFIGURE_DEPENDS "${folder}/*")
        list(APPEND files ${pluginFiles})
    endforeach()

    if(DEFINED APP_LOGO)
        get_filename_component(logo "${APP_LOGO}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

        if(NOT EXISTS "${logo}" OR IS_DIRECTORY "${logo}")
            message(FATAL_ERROR "The Workpane application logo does not exist")
        endif()

        workpane_application_quote(logoJson "${logo}")
        string(JSON configuration SET "${configuration}" logo "${logoJson}")
        list(APPEND files "${logo}")
    endif()

    list(SORT files)
    file(GENERATE OUTPUT "${generated}/resources.list" CONTENT "${files}\n${configuration}\n")
    file(GENERATE OUTPUT "${generated}/resources.json" CONTENT "${configuration}\n")
    add_custom_command(OUTPUT "${generated}/resources.stamp" COMMAND ${CMAKE_COMMAND} "-DCONFIG=${generated}/resources.json" "-DOUTPUT=${resources}" "-DSTAMP=${generated}/resources.stamp" -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/StageResources.cmake" DEPENDS ${files} "${generated}/resources.list" "${generated}/resources.json" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/StageResources.cmake" VERBATIM)
    add_custom_target(${target}Resources DEPENDS "${generated}/resources.stamp")
    add_dependencies(${target} ${target}Resources)
    set_target_properties(${target} PROPERTIES WORKPANE_RESOURCES_DIRECTORY "${resources}" WORKPANE_RESOURCE_NAME "${resourceName}")
    set_property(GLOBAL APPEND PROPERTY WORKPANE_APPLICATION_NAMES "${resourceName}")
endfunction()

function(workpane_install_application target)
    cmake_parse_arguments(PARSE_ARGV 1 INSTALL "" "COMPONENT" "")

    if(INSTALL_UNPARSED_ARGUMENTS OR INSTALL_KEYWORDS_MISSING_VALUES OR NOT TARGET "${target}")
        message(FATAL_ERROR "The Workpane application installation arguments are invalid")
    endif()

    if(NOT DEFINED INSTALL_COMPONENT)
        set(INSTALL_COMPONENT "${target}")
    endif()

    get_target_property(resources ${target} WORKPANE_RESOURCES_DIRECTORY)
    get_target_property(name ${target} WORKPANE_RESOURCE_NAME)

    if(NOT resources OR NOT name)
        message(FATAL_ERROR "The target was not created as a Workpane application")
    endif()

    if(APPLE)
        get_target_property(source WorkpaneCore WORKPANE_SOURCE_DIRECTORY)
        install(TARGETS ${target} BUNDLE DESTINATION . COMPONENT ${INSTALL_COMPONENT})
        install(CODE "execute_process(COMMAND \"${CMAKE_COMMAND}\" \"-DBUNDLE=\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${target}.app\" \"-DIDENTITY=${WORKPANE_CODESIGN_IDENTITY}\" \"-DENTITLEMENTS=${source}/extras/macos/Workpane.entitlements\" -P \"${source}/cmake/SignApplication.cmake\" COMMAND_ERROR_IS_FATAL ANY)" COMPONENT ${INSTALL_COMPONENT})
        return()
    endif()

    install(TARGETS ${target} WorkpaneAskpass RUNTIME DESTINATION bin COMPONENT ${INSTALL_COMPONENT})
    install(DIRECTORY "${resources}/" DESTINATION "share/${name}" COMPONENT ${INSTALL_COMPONENT})

    if(WIN32)
        install(FILES "$<TARGET_FILE:varn::varn>" DESTINATION bin COMPONENT ${INSTALL_COMPONENT})
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_SKIP TRUE)
        include(InstallRequiredSystemLibraries)
        install(FILES ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION bin COMPONENT ${INSTALL_COMPONENT})
        return()
    endif()

    install(TARGETS WorkpaneTerminal RUNTIME DESTINATION bin COMPONENT ${INSTALL_COMPONENT})
    install(FILES "$<TARGET_FILE:varn::varn>" "$<TARGET_SONAME_FILE:varn::varn>" DESTINATION lib COMPONENT ${INSTALL_COMPONENT})
endfunction()
