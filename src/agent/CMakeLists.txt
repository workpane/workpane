set(WORKPANE_AGENT_SOURCES
    AgentResourceCatalog.cpp
    AgentResourceCatalog.h
    BoundedReply.cpp
    BoundedReply.h
    AgentResources.cpp
    AgentResources.h
    mcp/McpClient.cpp
    mcp/McpClient.h
    mcp/McpTransport.cpp
    mcp/McpTransport.h
)

qt_add_library(WorkpaneAgent STATIC ${WORKPANE_AGENT_SOURCES})
target_include_directories(WorkpaneAgent PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(WorkpaneAgent PUBLIC Qt6::Concurrent Qt6::Core Qt6::Gui Qt6::Network Qt6::Sql Qt6::Widgets)
set_target_properties(
    WorkpaneAgent
    PROPERTIES
        POSITION_INDEPENDENT_CODE TRUE
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN TRUE
)

workpane_set_warnings(WorkpaneAgent)
workpane_enable_coverage(WorkpaneAgent)
workpane_enable_sanitizers(WorkpaneAgent)
