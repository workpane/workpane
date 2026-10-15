local catalog = include("catalog")

local remote = {}

function remote.open(profile, session)
    local backend = workpane.await(workpane.workspace.open({ open = "ssh.workspace.open", request = "ssh.workspace.request", close = "ssh.workspace.close", changed = "remote-ssh.workspace.changed", profile = profile, session = session, closedCodes = { "ssh_connection_closed", "ssh_workspace_unavailable" } }))
    local resolved = {}
    local searches = {}

    backend.searching = function(language)
        return searches[language] == "searching"
    end

    backend.executable = function(language)
        if searches[language] == nil and backend.available then
            searches[language] = "searching"
            workpane.task(function()
                local accepted, failure = pcall(function()
                    for _, server in ipairs(catalog.servers()) do
                        if server.language == language then
                            for _, candidate in ipairs(server.candidates) do
                                local found = workpane.await(backend.process.find(candidate.executable))

                                if not backend.available then
                                    return
                                end

                                if found ~= nil then
                                    resolved[language] = { path = found, arguments = candidate.arguments or {} }
                                    return
                                end
                            end

                            return
                        end
                    end
                end)

                searches[language] = "finished"

                if not backend.available then
                    return
                end

                if not accepted then
                    workpane.log.warning("lsp", "The remote language server search failed", { language = language, code = type(failure) == "table" and failure.code or "workspace_request_failed" })
                end

                backend.serversChanged()
            end)
        end

        return resolved[language]
    end

    backend.hasServers = function()
        return backend.available and next(resolved) ~= nil
    end

    backend.serversChanged = function() end

    return backend
end

return remote
