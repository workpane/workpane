set(TERMINAL_PLATFORM_ICON)
if(APPLE)
    set(TERMINAL_PLATFORM_ICON "${PROJECT_SOURCE_DIR}/extras/images/logo.icns")
    set_source_files_properties(
        "${TERMINAL_PLATFORM_ICON}"
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources"
    )
elseif(WIN32)
    set(TERMINAL_PLATFORM_ICON "${CMAKE_CURRENT_SOURCE_DIR}/Workpane.rc")
endif()

set(WORKPANE_CORE_SOURCES
    app/Application.cpp
    app/Application.h
    app/ConfigurationManager.cpp
    app/ConfigurationManager.h
    app/ApplicationSettingsStore.cpp
    app/ApplicationSettingsStore.h
    domain/ApplicationLanguage.cpp
    domain/ApplicationLanguage.h
    domain/ApplicationSettings.h
    filesystem/FileSystemService.cpp
    filesystem/FileSystemService.h
    persistence/DatabaseExecutor.cpp
    persistence/DatabaseExecutor.h
    persistence/ConfigurationTransfer.cpp
    persistence/ConfigurationTransfer.h
    persistence/CoreDatabaseSchema.cpp
    persistence/CoreDatabaseSchema.h
    persistence/PluginDatabase.h
    persistence/StateStore.cpp
    persistence/StateStore.h
    persistence/StoredValues.h
    plugins/CapabilityRegistry.cpp
    plugins/CapabilityRegistry.h
    plugins/LocalizationService.cpp
    plugins/LocalizationService.h
    plugins/CoreTranslations.h
    plugins/PluginCapability.h
    plugins/PluginInterface.h
    plugins/PluginManager.cpp
    plugins/PluginManager.h
    ui/AppStyle.cpp
    ui/AppStyle.h
    ui/ApplicationSettingsView.cpp
    ui/ApplicationSettingsView.h
    ui/ConfirmationDialog.cpp
    ui/ConfirmationDialog.h
    ui/MainWindow.cpp
    ui/MainWindow.h
    ui/ModeBar.cpp
    ui/ModeBar.h
    ui/SettingsView.cpp
    ui/SettingsView.h
    ui/ToastOverlay.cpp
    ui/ToastOverlay.h
    domain/Result.h
)

qt_add_library(WorkpaneCore STATIC ${WORKPANE_CORE_SOURCES})
target_include_directories(
    WorkpaneCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/../generated
)
target_link_libraries(WorkpaneCore PUBLIC Qt6::Concurrent Qt6::Core Qt6::Gui Qt6::Sql Qt6::Widgets WorkpaneAgent WorkpaneUi)

workpane_set_warnings(WorkpaneCore)
workpane_enable_coverage(WorkpaneCore)
workpane_enable_sanitizers(WorkpaneCore)

qt_add_executable(Workpane MACOSX_BUNDLE WIN32 main.cpp ${TERMINAL_PLATFORM_ICON})

# Qt names an application com.yourcompany when nothing declares it, and that name reaches the macOS bundle and the Windows manifest.
set_target_properties(
    Workpane
    PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "${WORKPANE_BUNDLE_IDENTIFIER}"
        QT_WINDOWS_APP_PROJECT_IDENTIFIER "${WORKPANE_BUNDLE_IDENTIFIER}"
)

if(APPLE)
    set_target_properties(Workpane PROPERTIES MACOSX_BUNDLE_ICON_FILE "logo.icns")
endif()

set(WORKPANE_APP_ICON "${PROJECT_SOURCE_DIR}/extras/images/logo.png")
set_source_files_properties(
    "${WORKPANE_APP_ICON}"
    PROPERTIES QT_RESOURCE_ALIAS "images/logo.png"
)
qt_add_resources(
    Workpane
    "workpane_assets"
    PREFIX "/"
    FILES "${WORKPANE_APP_ICON}"
)

target_link_libraries(Workpane PRIVATE WorkpaneCore)

workpane_set_warnings(Workpane)
workpane_enable_coverage(Workpane)
workpane_enable_sanitizers(Workpane)

if(APPLE)
    find_program(WORKPANE_DEVELOPMENT_CODESIGN_EXECUTABLE codesign REQUIRED)
    add_custom_target(
        WorkpaneBundledPlugins ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_DIR:Workpane>/Contents/PlugIns"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_DIR:Workpane>/Contents/Frameworks"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:hwinfo_battery>"
                "$<TARGET_FILE:hwinfo_cpu>"
                "$<TARGET_FILE:hwinfo_disk>"
                "$<TARGET_FILE:hwinfo_gpu>"
                "$<TARGET_FILE:hwinfo_mainboard>"
                "$<TARGET_FILE:hwinfo_os>"
                "$<TARGET_FILE:hwinfo_ram>"
                "$<TARGET_FILE:hwinfo_network>"
                "$<TARGET_BUNDLE_DIR:Workpane>/Contents/Frameworks/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:WorkpaneTerminalPlugin>"
                "$<TARGET_BUNDLE_DIR:Workpane>/Contents/PlugIns/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:WorkpaneWebServerPlugin>"
                "$<TARGET_BUNDLE_DIR:Workpane>/Contents/PlugIns/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:WorkpaneLogsPlugin>"
                "$<TARGET_BUNDLE_DIR:Workpane>/Contents/PlugIns/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:WorkpaneSystemInformationPlugin>"
                "$<TARGET_BUNDLE_DIR:Workpane>/Contents/PlugIns/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:WorkpaneDonatePlugin>"
                "$<TARGET_BUNDLE_DIR:Workpane>/Contents/PlugIns/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:WorkpaneAiPlugin>"
                "$<TARGET_BUNDLE_DIR:Workpane>/Contents/PlugIns/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:WorkpaneBrowserPlugin>"
                "$<TARGET_BUNDLE_DIR:Workpane>/Contents/PlugIns/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:WorkpaneCodeEditorPlugin>"
                "$<TARGET_BUNDLE_DIR:Workpane>/Contents/PlugIns/"
        COMMAND "${WORKPANE_DEVELOPMENT_CODESIGN_EXECUTABLE}"
                --force
                --deep
                --sign -
                "$<TARGET_BUNDLE_DIR:Workpane>"
        DEPENDS Workpane WorkpaneAiPlugin WorkpaneBrowserPlugin WorkpaneCodeEditorPlugin WorkpaneDonatePlugin WorkpaneLogsPlugin WorkpaneSystemInformationPlugin WorkpaneTerminalPlugin WorkpaneWebServerPlugin ${WORKPANE_HWINFO_TARGETS}
    )
else()
    add_custom_target(
        WorkpaneBundledPlugins ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Workpane>/plugins"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:hwinfo_battery>"
                "$<TARGET_FILE:hwinfo_cpu>"
                "$<TARGET_FILE:hwinfo_disk>"
                "$<TARGET_FILE:hwinfo_gpu>"
                "$<TARGET_FILE:hwinfo_mainboard>"
                "$<TARGET_FILE:hwinfo_os>"
                "$<TARGET_FILE:hwinfo_ram>"
                "$<TARGET_FILE:hwinfo_network>"
                "$<TARGET_FILE_DIR:Workpane>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:WorkpaneTerminalPlugin>"
                "$<TARGET_FILE_DIR:Workpane>/plugins/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:WorkpaneWebServerPlugin>"
                "$<TARGET_FILE_DIR:Workpane>/plugins/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:WorkpaneLogsPlugin>"
                "$<TARGET_FILE_DIR:Workpane>/plugins/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:WorkpaneSystemInformationPlugin>"
                "$<TARGET_FILE_DIR:Workpane>/plugins/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:WorkpaneDonatePlugin>"
                "$<TARGET_FILE_DIR:Workpane>/plugins/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:WorkpaneAiPlugin>"
                "$<TARGET_FILE_DIR:Workpane>/plugins/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:WorkpaneBrowserPlugin>"
                "$<TARGET_FILE_DIR:Workpane>/plugins/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:WorkpaneCodeEditorPlugin>"
                "$<TARGET_FILE_DIR:Workpane>/plugins/"
        DEPENDS Workpane WorkpaneAiPlugin WorkpaneBrowserPlugin WorkpaneCodeEditorPlugin WorkpaneDonatePlugin WorkpaneLogsPlugin WorkpaneSystemInformationPlugin WorkpaneTerminalPlugin WorkpaneWebServerPlugin ${WORKPANE_HWINFO_TARGETS}
    )
endif()

install(TARGETS Workpane BUNDLE DESTINATION . RUNTIME DESTINATION bin)

if(WIN32)
    # A Windows loader resolves a dependent library beside the executable, and nothing else installs these.
    install(
        FILES
            "$<TARGET_FILE:hwinfo_battery>"
            "$<TARGET_FILE:hwinfo_cpu>"
            "$<TARGET_FILE:hwinfo_disk>"
            "$<TARGET_FILE:hwinfo_gpu>"
            "$<TARGET_FILE:hwinfo_mainboard>"
            "$<TARGET_FILE:hwinfo_os>"
            "$<TARGET_FILE:hwinfo_ram>"
            "$<TARGET_FILE:hwinfo_network>"
        DESTINATION bin
    )
endif()

if(APPLE)
    find_program(WORKPANE_MACDEPLOYQT_EXECUTABLE macdeployqt REQUIRED)
    find_program(WORKPANE_CODESIGN_EXECUTABLE codesign REQUIRED)
    find_program(WORKPANE_QTPATHS_EXECUTABLE qtpaths6 REQUIRED)
    execute_process(
        COMMAND "${WORKPANE_QTPATHS_EXECUTABLE}" --plugin-dir
        OUTPUT_VARIABLE WORKPANE_QT_PLUGIN_DIRECTORY
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
    )
    file(REAL_PATH "${WORKPANE_QT_PLUGIN_DIRECTORY}/platforms/libqcocoa.dylib"
         WORKPANE_MACOS_PLATFORM_PLUGIN)
    set(WORKPANE_CODESIGN_HARDENING)
    if(NOT WORKPANE_CODESIGN_IDENTITY STREQUAL "-")
        set(WORKPANE_CODESIGN_HARDENING --options runtime --timestamp)
    endif()
    install(CODE "
        set(app_bundle \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/Workpane.app\")
        execute_process(
            COMMAND \"${WORKPANE_MACDEPLOYQT_EXECUTABLE}\"
                    \"\${app_bundle}\"
                    -no-plugins
                    -no-codesign
                    -appstore-compliant
                    -always-overwrite
            RESULT_VARIABLE deploy_result
        )
        if(NOT deploy_result EQUAL 0)
            message(FATAL_ERROR \"macdeployqt failed with exit code \${deploy_result}\")
        endif()

        file(MAKE_DIRECTORY \"\${app_bundle}/Contents/PlugIns/platforms\")
        file(COPY \"${WORKPANE_MACOS_PLATFORM_PLUGIN}\"
             DESTINATION \"\${app_bundle}/Contents/PlugIns/platforms\")

        execute_process(
            COMMAND \"${WORKPANE_CODESIGN_EXECUTABLE}\"
                    --force
                    --deep
                    ${WORKPANE_CODESIGN_HARDENING}
                    --sign \"${WORKPANE_CODESIGN_IDENTITY}\"
                    \"\${app_bundle}\"
            RESULT_VARIABLE codesign_result
        )
        if(NOT codesign_result EQUAL 0)
            message(FATAL_ERROR \"codesign failed with exit code \${codesign_result}\")
        endif()
    ")
else()
    # The Oracle driver and the serial GPS plugin ask for client libraries no machine running this product carries, and shipping a plugin that can never load only makes the package refuse to build its dependencies.
    qt_generate_deploy_script(
        TARGET Workpane
        OUTPUT_SCRIPT workpane_deploy_script
        CONTENT "
qt_deploy_runtime_dependencies(
    EXECUTABLE \"$<TARGET_FILE:Workpane>\"
    GENERATE_QT_CONF
    NO_TRANSLATIONS
    EXCLUDE_PLUGINS qsqloci qtposition_nmea
    ADDITIONAL_LIBRARIES
        \"$<TARGET_FILE:WorkpaneAiPlugin>\"
        \"$<TARGET_FILE:WorkpaneBrowserPlugin>\"
        \"$<TARGET_FILE:WorkpaneCodeEditorPlugin>\"
        \"$<TARGET_FILE:WorkpaneDonatePlugin>\"
        \"$<TARGET_FILE:WorkpaneLogsPlugin>\"
        \"$<TARGET_FILE:WorkpaneSystemInformationPlugin>\"
        \"$<TARGET_FILE:WorkpaneTerminalPlugin>\"
        \"$<TARGET_FILE:WorkpaneWebServerPlugin>\"
)
"
    )
    install(SCRIPT ${workpane_deploy_script})
endif()
