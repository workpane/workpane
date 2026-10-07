-- The browser page: a toolbar that follows the page on screen, the bookmarks panel beside the tabs and one native page per tab.
local address = include("address")
local preferences = include("preferences")
local session = include("session")
local panel = include("views/bookmarks")

local ui = workpane.ui
local text = workpane.i18n.text
local translate = workpane.i18n.translate

local view = {}

local pages = {}
local states = {}
local popups = {}
local shown = ""
local controls
local addressShown = { tab = nil, url = nil }

local function notify(key)
    workpane.notify.error(translate("browser.plugin.title"), translate(key))
end

-- A download that ended tells the reader which file was saved in the downloads folder or could not be, and a failure keeps its reason in the log.
local function downloaded(event)
    local name = event.path:match("[^/]+$") or event.path

    if event.finished then
        workpane.notify.success(translate("browser.download.finished-title"), translate("browser.download.finished-message", name))
        return
    end

    workpane.log.warning("download", "A download failed", { path = event.path, message = event.message })
    workpane.notify.error(translate("browser.download.failed-title"), translate("browser.download.failed-message", name))
end

-- The toolbar shows the address, the history and the loading of the active tab, and turns Reload into Stop while it loads.
-- The address is written only when the active tab or its address changes, so what the reader types survives the other tabs loading.
local function renderToolbar()
    local tab = session.active()
    local state = tab ~= nil and states[tab.id] or nil
    local id = tab ~= nil and tab.id or ""
    local url = tab ~= nil and tab.url or ""

    if addressShown.tab ~= id or addressShown.url ~= url then
        addressShown = { tab = id, url = url }
        controls.address:set({ value = url })
    end

    controls.back:set({ enabled = state ~= nil and state.canGoBack })
    controls.forward:set({ enabled = state ~= nil and state.canGoForward })
    controls.reload:set({ enabled = tab ~= nil, visible = state == nil or not state.loading })
    controls.stop:set({ visible = state ~= nil and state.loading })
    controls.home:set({ enabled = tab ~= nil })
end

local function page(tab)
    if pages[tab.id] ~= nil then
        return pages[tab.id]
    end

    local id = tab.id

    -- A page reports where it arrived, which the session keeps, the strip shows only when the address, the title or the icon changed and the toolbar shows for the active tab.
    local function navigated(event)
        local previous = states[id]
        states[id] = event
        local changed = session.update(id, event.url, event.title)

        if changed or previous == nil or previous.icon ~= event.icon then
            view.render()
            return
        end

        local active = session.active()

        if active ~= nil and active.id == id then
            renderToolbar()
        end
    end

    -- A page asking for the camera or the microphone waits for the reader, who allows it or not in a dialog naming the page and what it asks for.
    local function asked(event)
        local wanted = event.camera and event.microphone and "both" or event.camera and "camera" or "microphone"
        local allowed = workpane.await(workpane.dialogs.confirm({ title = translate("browser.permission." .. wanted .. "-title"), message = translate("browser.permission." .. wanted .. "-message", event.origin), confirmText = translate("browser.permission.allow"), cancelText = translate("browser.permission.deny") }))

        if pages[id] ~= nil then
            pages[id]:command("answer-permission", { request = event.request, allowed = allowed })
        end
    end

    -- A tab opened for a window of a page shows that window, which keeps talking to the page that opened it.
    pages[id] = ui.webView({ url = popups[id] == nil and tab.url or nil, popup = popups[id], grow = 1, onNavigation = navigated, onOpenRequest = function(event)
        view.open(event.url, true, event.background)
    end, onPopup = function(event)
        view.adopt(event.popup)
    end, onCloseRequest = function()
        view.close(id)
    end, onDownload = downloaded, onPermissionRequest = asked })

    return pages[id]
end

-- The strip and its pages are built from the session, and a tab that stays keeps its page and everything loaded in it.
function view.render()
    if controls == nil then
        return
    end

    local items = {}
    local children = {}
    local order = {}
    local current = ""

    for index, tab in ipairs(session.list()) do
        local state = states[tab.id]
        items[index] = { id = tab.id, text = tab.title, tooltip = tab.url, image = state ~= nil and state.icon ~= "" and state.icon or nil }
        children[index] = page(tab)
        order[index] = tab.id
        current = tab.active and tab.id or current
    end

    for id in pairs(pages) do
        if session.find(id) == nil then
            pages[id] = nil
            states[id] = nil
            popups[id] = nil
        end
    end

    -- The pages are sent again only when the tabs changed, together with the tabs, because a page that only moved on keeps its place.
    local declared = { items = items, current = current, visible = #items > 0 }

    if table.concat(order, " ") == shown then
        controls.tabs:set(declared)
    else
        shown = table.concat(order, " ")
        controls.tabs:setChildren(children, declared)
    end

    controls.empty:set({ visible = #items == 0 })
    renderToolbar()
end

function view.current()
    local tab = session.active()
    return tab ~= nil and { title = (states[tab.id] or {}).title or tab.title, url = tab.url } or { title = "", url = "" }
end

function view.command(name, arguments)
    local tab = session.active()

    if tab ~= nil and pages[tab.id] ~= nil then
        pages[tab.id]:command(name, arguments)
    end
end

-- An address opens in the active tab, or in a new tab when asked or when no tab is open, which stays behind the active one when the reader opened it in the background.
function view.open(url, inNewTab, background)
    local normalized = address.normalize(url)

    if normalized == nil then
        notify("browser.error.invalid-address")
        return
    end

    if inNewTab or session.active() == nil then
        local id = session.create(normalized, not background)

        if id == nil then
            notify("browser.error.operation")
        end

        view.render()
        return
    end

    view.command("navigate", { url = normalized })
end

-- A window a page opened becomes a new active tab, which starts blank until the window loads its own page.
function view.adopt(popup)
    local id = session.create("about:blank", true)

    if id == nil then
        notify("browser.error.operation")
        return
    end

    popups[id] = popup
    view.render()
end

-- A page that asks to close its window closes its tab.
function view.close(id)
    if session.close(id) then
        view.render()
    end
end

function view.newTab()
    view.open(preferences.homepage(), true)
end

function view.closeActive()
    local tab = session.active()

    if tab ~= nil then
        session.close(tab.id)
        view.render()
    end
end

-- The tab beside the active one takes its place, going round past either end.
function view.step(direction)
    local tabs = session.list()
    local active = session.active()

    if active == nil or #tabs < 2 then
        return
    end

    local _, index = session.find(active.id)
    session.activate(tabs[(index - 1 + direction) % #tabs + 1].id)
    view.render()
end

function view.build()
    addressShown = { tab = nil, url = nil }
    controls = {
        back = ui.button({ icon = "back", variant = "toolbar", tooltip = text("browser.actions.back"), enabled = false, onClick = function()
            view.command("back")
        end }),
        forward = ui.button({ icon = "forward", variant = "toolbar", tooltip = text("browser.actions.forward"), enabled = false, onClick = function()
            view.command("forward")
        end }),
        reload = ui.button({ icon = "refresh", variant = "toolbar", tooltip = text("browser.actions.reload"), onClick = function()
            view.command("reload")
        end }),
        stop = ui.button({ icon = "stop", variant = "toolbar", tooltip = text("browser.actions.stop"), visible = false, onClick = function()
            view.command("stop")
        end }),
        home = ui.button({ icon = "home", variant = "toolbar", tooltip = text("browser.actions.home"), onClick = function()
            view.open(preferences.homepage(), false)
        end }),
        address = ui.textField({ placeholder = text("browser.address.placeholder"), clearButton = true, grow = 1, onSubmit = function(event)
            view.open(event.value, false)
        end }),
        tabs = ui.tabs({ items = {}, closable = true, movable = true, grow = 1, onSelect = function(event)
            session.activate(event.id)
            renderToolbar()
        end, onClose = function(event)
            session.close(event.id)
            view.render()
        end, onMove = function(event)
            session.move(event.id, event.index)
        end }),
        empty = ui.column({ grow = 1, spacing = 14, justify = "center", visible = false }, {
            ui.emptyState({ text = text("browser.tabs.empty"), icon = "browser" }),
            ui.button({ text = text("browser.actions.new-tab"), icon = "add", variant = "primary", align = "center", onClick = function()
                view.newTab()
            end }),
        }),
    }

    local bookmarksPanel = panel.build({ current = view.current, open = view.open })
    local toggle = ui.button({ icon = "bookmark", variant = "toolbar", tooltip = text("browser.bookmarks.toggle"), checked = false })

    toggle:on("click", function()
        local open = not toggle:get("checked")
        toggle:set({ checked = open })
        bookmarksPanel:set({ visible = open })
    end)

    session.listen(view.render)
    view.render()

    return ui.column({}, {
        ui.row({ padding = { 6, 8 }, spacing = 4, background = "panel", borders = { "bottom" } }, {
            controls.back,
            controls.forward,
            controls.reload,
            controls.stop,
            controls.home,
            toggle,
            controls.address,
            ui.button({ icon = "add", variant = "toolbar", tooltip = text("browser.actions.new-tab"), onClick = function()
                view.newTab()
            end }),
        }),
        ui.splitter({ orientation = "horizontal", ratio = 0.22, firstMinimum = 240, secondMinimum = 320, grow = 1 }, bookmarksPanel, ui.column({}, { controls.tabs, controls.empty })),
    })
end

return view
