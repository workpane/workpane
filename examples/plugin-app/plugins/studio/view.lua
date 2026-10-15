local view = {}

function view.build()
    local ui = workpane.ui
    local text = workpane.i18n.text

    return ui.column({ padding = 16, spacing = 12 }, {
        ui.pageHeader({ title = text("studio.view.title", workpane.app.name) }),
        ui.label({ text = text("studio.view.version", workpane.app.version) }),
        ui.alert({ tone = "information", text = text("studio.view.description") }),
        ui.formField({ label = text("studio.view.note") }, ui.textArea({ placeholder = text("studio.view.placeholder"), grow = 1, height = 180 })),
    })
end

return view
