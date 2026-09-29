-- The dialogs of the operating system: opening files, choosing a folder, saving a file, message boxes and system notifications.
local gallery = include("views/gallery")
local ui = workpane.ui
local text = workpane.i18n.text
local translate = workpane.i18n.translate

return function(report)
    local filters = {
        { name = translate("components.native-dialogs.filter-lua"), patterns = { "*.lua" } },
        { name = translate("components.native-dialogs.filter-all"), patterns = { "*" } },
    }

    -- A dialog the operating system could not show is reported like any answer, so the page shows why nothing opened.
    local function run(name, future, describe)
        local value, failure = future:await()

        if failure ~= nil then
            report(name, { failure = tostring(failure) })
            return
        end

        report(name, describe(value))
    end

    local function openFiles()
        run("open", workpane.dialogs.openFile({ title = translate("components.native-dialogs.open-title"), filters = filters, multiple = true }), function(paths)
            return { count = #paths, first = paths[1] or "nil" }
        end)
    end

    local function selectFolder()
        run("folder", workpane.dialogs.selectFolder({ title = translate("components.native-dialogs.folder-title") }), function(path)
            return { path = path or "nil" }
        end)
    end

    local function saveFile()
        run("save", workpane.dialogs.saveFile({ title = translate("components.native-dialogs.save-title"), initial = "notes.lua", filters = filters }), function(path)
            return { path = path or "nil" }
        end)
    end

    local function message(kind, buttons)
        return function()
            run("message", workpane.dialogs.message({ title = translate("components.native-dialogs.message-title"), message = translate("components.native-dialogs.message-" .. kind), kind = kind, buttons = buttons }), function(button)
                return { button = button }
            end)
        end
    end

    local function notifySystem()
        workpane.notify.system(translate("components.native-dialogs.notify-title"), translate("components.native-dialogs.notify-message"), "information")
        report("notify", { sent = true })
    end

    return {
        gallery.section("components.native-dialogs.files", {
            ui.row({ spacing = 8 }, {
                ui.button({ text = text("components.native-dialogs.open"), icon = "folder", onClick = openFiles }),
                ui.button({ text = text("components.native-dialogs.folder"), icon = "folder", onClick = selectFolder }),
                ui.button({ text = text("components.native-dialogs.save"), icon = "export", onClick = saveFile }),
            }),
        }),
        gallery.section("components.native-dialogs.messages", {
            ui.row({ spacing = 8 }, {
                ui.button({ text = text("components.native-dialogs.information"), icon = "information", onClick = message("information", "ok") }),
                ui.button({ text = text("components.native-dialogs.warning"), icon = "warning", onClick = message("warning", "ok-cancel") }),
                ui.button({ text = text("components.native-dialogs.error"), icon = "error", onClick = message("error", "ok") }),
                ui.button({ text = text("components.native-dialogs.question"), icon = "chat", onClick = message("question", "yes-no-cancel") }),
            }),
        }),
        gallery.section("components.native-dialogs.system", {
            ui.button({ text = text("components.native-dialogs.notify"), icon = "bell", onClick = notifySystem }),
        }),
    }
end
