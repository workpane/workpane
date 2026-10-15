local connections = include("connections")

local picker = {}

function picker.profile(profile, isCurrent, onScope, password)
    local scope = workpane.resources.scope()
    onScope(scope)

    local function acquire()
        return workpane.future(function() return connections.createPreview(profile) end)
    end

    local function release(current)
        return connections.closePreview(current.profile.id)
    end

    local accepted, chosen = pcall(function()
        local current = workpane.await(scope:acquire(acquire, release))

        if not isCurrent() then
            return nil
        end

        current.suppliedPassword = password ~= "" and password or nil
        password = nil
        local starting, failure = current:start()

        if starting == nil then
            error(failure, 0)
        end

        local outcome = workpane.await(starting)

        if outcome.failure ~= nil then
            if current.closed and current.problem == nil then
                return nil
            end

            error(outcome.failure, 0)
        end

        if not isCurrent() then
            return nil
        end

        return workpane.await(workpane.dialogs.selectFolder({ title = workpane.i18n.translate("remote-ssh.picker.title"), initial = current.profile.root, isCurrent = isCurrent, workspace = { open = "ssh.workspace.open", request = "ssh.workspace.request", close = "ssh.workspace.close", changed = "remote-ssh.workspace.changed", profile = current.profile.id, session = current.id, closedCodes = { "ssh_connection_closed", "ssh_workspace_unavailable" } } }))
    end)

    local _, cleanup = scope:close():await()

    if not accepted or cleanup ~= nil then
        error(not accepted and chosen or cleanup, 0)
    end

    return chosen
end

return picker
