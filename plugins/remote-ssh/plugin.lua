local connections = include("connections")
local preferences = include("preferences")
local view = include("view")
local workspace = include("workspace")

return {
    id = "remote-ssh",
    sdk = 6,
    titleKey = "remote-ssh.plugin.title",
    descriptionKey = "remote-ssh.plugin.description",
    navigation = {
        { id = "connections", titleKey = "remote-ssh.navigation.connections", icon = "terminal", placement = "primary", order = 450, view = view.build },
    },
    start = function()
        preferences.define()
        connections.start()
        workspace.start()
        workpane.capabilities.provide("ssh.connections.list", connections.list, { summaryKey = "remote-ssh.contract.connections-list", payload = { type = "object", properties = {}, additionalProperties = false } })
        workpane.capabilities.provide("ssh.workspace.open", workspace.open, { summaryKey = "remote-ssh.contract.workspace-open", payload = { type = "object", properties = { profile = { type = "string" }, session = { type = "string" } }, required = { "profile", "session" }, additionalProperties = false } })
        workpane.capabilities.provide("ssh.workspace.request", workspace.request, { summaryKey = "remote-ssh.contract.workspace-request", payload = { type = "object", properties = { workspace = { type = "string" }, method = { type = "string" }, parameters = { type = "object" } }, required = { "workspace", "method", "parameters" }, additionalProperties = false } })
        workpane.capabilities.provide("ssh.workspace.close", workspace.close, { summaryKey = "remote-ssh.contract.workspace-close", payload = { type = "object", properties = { workspace = { type = "string" } }, required = { "workspace" }, additionalProperties = false } })
    end,
    stop = function()
        workspace.stop()
        connections.stop()
    end,
}
