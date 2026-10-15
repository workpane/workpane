local diagnostics = {}

local reasons = {
    ssh_client_unavailable = "client",
    ssh_connection_timeout = "connection-timeout",
    ssh_cleanup_failed = "cleanup",
    ssh_workspace_invalid = "workspace",
    ssh_connection_lost = "lost",
    ssh_authentication_refused = "authentication",
    ssh_host_key_changed = "host-key",
    ssh_host_unresolved = "host-unresolved",
    ssh_host_refused = "host-refused",
    ssh_transfer_unavailable = "transfer",
    ssh_platform_unavailable = "platform",
    ssh_profile_root_invalid = "root",
    ssh_terminal_failed = "terminal",
    ssh_terminal_limit = "terminal-limit",
    ssh_forward_invalid = "forward-invalid",
    ssh_forward_limit = "forward-limit",
    ssh_forward_bind_failed = "forward-bind",
    ssh_forward_failed = "forward",
    ssh_forward_refused = "forward-refused",
}

function diagnostics.key(problem)
    return "remote-ssh.error." .. (reasons[problem.code] or "connection")
end

function diagnostics.notify(problem)
    local translate = workpane.i18n.translate
    workpane.notify.error(translate("remote-ssh.plugin.title"), translate(diagnostics.key(problem)))
end

return diagnostics
