qt_add_library(
    WorkpaneAiObjects
    OBJECT
    AiAgentPrompt.cpp
    AiAgentPrompt.h
    AiAgentSettingsView.cpp
    AiAgentSettingsView.h
    AiChatClient.cpp
    AiChatClient.h
    AiCliChatClient.cpp
    AiCliChatClient.h
    AiCommandRunner.cpp
    AiCommandRunner.h
    AiConnectionDialog.cpp
    AiConnectionDialog.h
    AiConnectionSettingsView.cpp
    AiConnectionSettingsView.h
    AiConversationView.cpp
    AiConversationView.h
    AiMcpSettingsView.cpp
    AiMcpSettingsView.h
    AiModelConnection.cpp
    AiModelConnection.h
    AiModelDiscovery.cpp
    AiModelDiscovery.h
    AiPlugin.cpp
    AiPlugin.h
    AiProviderCatalog.cpp
    AiProviderCatalog.h
    AiProviderScope.cpp
    AiProviderScope.h
    AiRateLimitSettingsView.cpp
    AiRateLimitSettingsView.h
    AiRequestGate.cpp
    AiRequestGate.h
    AiSecret.cpp
    AiSecret.h
    AiServiceSettingsView.cpp
    AiServiceSettingsView.h
    AiTaskDialog.cpp
    AiTaskDialog.h
    AiTaskInfoDialog.cpp
    AiTaskInfoDialog.h
    AiTaskRepository.cpp
    AiTaskRepository.h
    AiTasksView.cpp
    AiTasksView.h
    AiToolContract.cpp
    AiToolContract.h
    AiToolRegistry.cpp
    AiToolRegistry.h
    AiTranslations.h
    CronExpression.cpp
    CronExpression.h
)
qt_add_resources(WorkpaneAiObjects "ai-assets" PREFIX "/workpane/ai" FILES assets/models.json assets/providers.json)
target_include_directories(WorkpaneAiObjects PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(WorkpaneAiObjects PUBLIC Qt6::Concurrent Qt6::Core Qt6::Gui Qt6::Network Qt6::Widgets WorkpaneAgent WorkpaneUi)
set_target_properties(WorkpaneAiObjects PROPERTIES POSITION_INDEPENDENT_CODE TRUE CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN TRUE)
workpane_set_warnings(WorkpaneAiObjects)
workpane_enable_coverage(WorkpaneAiObjects)
workpane_enable_sanitizers(WorkpaneAiObjects)

qt_add_library(WorkpaneAiPlugin SHARED)
target_link_libraries(WorkpaneAiPlugin PRIVATE WorkpaneAiObjects)
set_target_properties(WorkpaneAiPlugin PROPERTIES OUTPUT_NAME workpane-ai)
workpane_set_warnings(WorkpaneAiPlugin)
workpane_enable_coverage(WorkpaneAiPlugin)
workpane_enable_sanitizers(WorkpaneAiPlugin)

if(NOT APPLE)
    install(TARGETS WorkpaneAiPlugin LIBRARY DESTINATION bin/plugins RUNTIME DESTINATION bin/plugins)
endif()
