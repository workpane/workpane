qt_add_library(
    WorkpaneCodeEditorObjects
    OBJECT
    CodeDocument.cpp
    CodeDocument.h
    CodeEditorPlugin.cpp
    CodeEditorPlugin.h
    CodeEditorRepository.cpp
    CodeEditorRepository.h
    CodeEditorState.h
    CodeEditorTranslations.h
    CodeEditorView.cpp
    CodeEditorView.h
    CodeEditorWidget.cpp
    CodeEditorWidget.h
    CodeColorScheme.cpp
    CodeColorScheme.h
    CodeSyntaxHighlighter.cpp
    CodeSyntaxHighlighter.h
    CodeWorkspaceView.cpp
    CodeWorkspaceView.h
    EditorConfig.cpp
    EditorConfig.h
    FileFinder.cpp
    FileFinder.h
    WorkspaceSearch.cpp
    WorkspaceSearch.h
    FileSystemFailure.cpp
    FileSystemFailure.h
    FileWatch.cpp
    FileWatch.h
    LanguageRegistry.cpp
    LanguageRegistry.h
    LanguageServerClient.cpp
    LanguageServerClient.h
    LanguageServerTransport.cpp
    LanguageServerTransport.h
)
qt_add_resources(WorkpaneCodeEditorObjects "code-editor-assets" PREFIX "/workpane/code-editor" FILES assets/languages.json assets/schemes.json)
target_include_directories(WorkpaneCodeEditorObjects PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(WorkpaneCodeEditorObjects PUBLIC Qt6::Concurrent Qt6::Core Qt6::Gui Qt6::Widgets WorkpaneUi)
set_target_properties(WorkpaneCodeEditorObjects PROPERTIES POSITION_INDEPENDENT_CODE TRUE CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN TRUE)
workpane_set_warnings(WorkpaneCodeEditorObjects)
workpane_enable_coverage(WorkpaneCodeEditorObjects)
workpane_enable_sanitizers(WorkpaneCodeEditorObjects)

qt_add_library(WorkpaneCodeEditorPlugin SHARED)
target_link_libraries(WorkpaneCodeEditorPlugin PRIVATE WorkpaneCodeEditorObjects)
set_target_properties(WorkpaneCodeEditorPlugin PROPERTIES OUTPUT_NAME workpane-code-editor)
workpane_set_warnings(WorkpaneCodeEditorPlugin)
workpane_enable_coverage(WorkpaneCodeEditorPlugin)
workpane_enable_sanitizers(WorkpaneCodeEditorPlugin)

if(NOT APPLE)
    install(TARGETS WorkpaneCodeEditorPlugin LIBRARY DESTINATION bin/plugins RUNTIME DESTINATION bin/plugins)
endif()
