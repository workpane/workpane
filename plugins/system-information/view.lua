-- Shows the machine the product runs on in themed cards, collected by the host on a worker when the page first opens and whenever the reader refreshes it.
local ui = workpane.ui
local text = workpane.i18n.text
local number = workpane.i18n.number
local translate = workpane.i18n.translate

local view = {}

local captionWidth = 150
local unavailable = text("system-information.common.unavailable")

local snapshot
local collecting = false
local header
local refreshButton
local body

-- Sizes are written in binary units, the way the operating system itself counts memory and disks.
local function bytes(value)
    if value == nil or value <= 0 then
        return unavailable
    end

    local units = {
        { 1024 ^ 4, "system-information.unit.tebibytes", 2 },
        { 1024 ^ 3, "system-information.unit.gibibytes", 2 },
        { 1024 ^ 2, "system-information.unit.mebibytes", 1 },
        { 1024, "system-information.unit.kibibytes", 1 },
    }

    for _, unit in ipairs(units) do
        if value >= unit[1] then
            return text(unit[2], number(value / unit[1], unit[3]))
        end
    end

    return text("system-information.unit.bytes", number(value, 0))
end

local function frequency(hertz)
    if hertz == nil or hertz <= 0 then
        return unavailable
    end

    if hertz >= 1e9 then
        return text("system-information.unit.gigahertz", number(hertz / 1e9, 2))
    end

    return text("system-information.unit.megahertz", number(hertz / 1e6, 0))
end

local function count(value)
    if value == nil or value <= 0 then
        return unavailable
    end

    return text("system-information.common.count", number(value, 0))
end

local function plain(value)
    if value == nil or value == "" then
        return unavailable
    end

    return value
end

local function percent(ratio)
    return text("system-information.common.percentage", number(math.max(0, math.min(1, ratio)) * 100, 0))
end

local function field(caption, value)
    return ui.row({ spacing = 12 }, {
        ui.label({ text = text(caption), style = "muted", width = captionWidth }),
        ui.label({ text = value, selectable = true, wrap = true, grow = 1 }),
    })
end

-- A field holding several lines keeps each one on its own label, because every line is a sentence of its own.
local function lines(caption, values)
    local labels = {}

    for index, value in ipairs(values) do
        labels[index] = ui.label({ text = value, selectable = true, wrap = true })
    end

    return ui.row({ spacing = 12 }, {
        ui.label({ text = text(caption), style = "muted", width = captionWidth, align = "start" }),
        ui.column({ spacing = 2, grow = 1 }, labels),
    })
end

local function progress(caption, ratio)
    return ui.row({ spacing = 12 }, {
        ui.label({ text = text(caption), style = "muted", width = captionWidth }),
        ui.progress({ value = math.max(0, math.min(1, ratio)), text = percent(ratio), showText = true, grow = 1 }),
    })
end

local function device(key, index)
    return ui.label({ text = text(key, index), style = "strong", selectable = true })
end

local function empty()
    return ui.label({ text = text("system-information.common.not-detected"), style = "muted" })
end

local function card(titleKey, icon, children)
    return ui.card({ title = text(titleKey), icon = icon, spacing = 8, padding = { 12, 14, 12, 14 } }, children)
end

-- Memory whose available amount the system did not report has no share in use, rather than reading as full.
local function usedMemory(memory)
    if memory.total <= 0 or memory.available <= 0 or memory.available > memory.total then
        return nil
    end

    return (memory.total - memory.available) / memory.total
end

local function overview(machine)
    local first = machine.processors[1]
    local rows = {
        field("system-information.field.name", plain(machine.os.name)),
        field("system-information.field.model", first ~= nil and plain(first.model) or unavailable),
        field("system-information.field.total", bytes(machine.memory.total)),
    }

    if machine.processorUsage.utilization ~= nil then
        rows[#rows + 1] = progress("system-information.field.utilization", machine.processorUsage.utilization)
    end

    local used = usedMemory(machine.memory)

    if used ~= nil then
        rows[#rows + 1] = progress("system-information.field.used", used)
    end

    return card("system-information.section.overview", "system", rows)
end

local function operatingSystem(machine)
    local os = machine.os
    local architecture = os.architectureBits > 0 and text("system-information.unit.bits", number(os.architectureBits, 0)) or unavailable
    local byteOrders = { ["big-endian"] = "system-information.common.big-endian", ["little-endian"] = "system-information.common.little-endian" }
    local byteOrder = byteOrders[os.byteOrder] ~= nil and text(byteOrders[os.byteOrder]) or unavailable

    return card("system-information.section.operating-system", "system", {
        field("system-information.field.host-name", plain(os.hostName)),
        field("system-information.field.name", plain(os.name)),
        field("system-information.field.version", plain(os.version)),
        field("system-information.field.kernel", plain(os.kernel)),
        field("system-information.field.architecture", architecture),
        field("system-information.field.byte-order", byteOrder),
    })
end

local function processors(machine)
    local rows = {}
    local usage = machine.processorUsage

    if #machine.processors == 0 then
        rows[1] = empty()
    end

    if usage.utilization ~= nil then
        rows[#rows + 1] = progress("system-information.field.utilization", usage.utilization)
    end

    if usage.threads ~= nil and #usage.threads > 0 then
        local threads = {}

        for index, thread in ipairs(usage.threads) do
            threads[index] = text("system-information.common.thread-format", index - 1, percent(thread.utilization), frequency(thread.frequency))
        end

        rows[#rows + 1] = lines("system-information.field.threads", threads)
    end

    for index, processor in ipairs(machine.processors) do
        rows[#rows + 1] = device("system-information.common.processor-name", index)
        rows[#rows + 1] = field("system-information.field.vendor", plain(processor.vendor))
        rows[#rows + 1] = field("system-information.field.model", plain(processor.model))
        rows[#rows + 1] = field("system-information.field.physical-cores", count(processor.physicalCores))
        rows[#rows + 1] = field("system-information.field.logical-cores", count(processor.logicalCores))

        if #processor.cores > 0 then
            local cores = {}

            for position, core in ipairs(processor.cores) do
                local threading = text(core.smt and "system-information.common.smt" or "system-information.common.no-smt")
                cores[position] = text("system-information.common.core-format", core.id, frequency(core.maximumFrequency), bytes(core.l1Data + core.l1Instruction), bytes(core.l2), bytes(core.l3), threading)
            end

            rows[#rows + 1] = lines("system-information.field.core-details", cores)
        end

        if #processor.flags > 0 then
            rows[#rows + 1] = field("system-information.field.capabilities", table.concat(processor.flags, ", "))
        end
    end

    return card("system-information.section.processor", "processor", rows)
end

local function memory(machine)
    local rows = {
        field("system-information.field.total", bytes(machine.memory.total)),
        field("system-information.field.available", bytes(machine.memory.available)),
        field("system-information.field.free", bytes(machine.memory.free)),
    }

    for index, module in ipairs(machine.memory.modules) do
        rows[#rows + 1] = device("system-information.common.module-name", index)
        rows[#rows + 1] = field("system-information.field.vendor", plain(module.vendor))
        rows[#rows + 1] = field("system-information.field.name", plain(module.name))
        rows[#rows + 1] = field("system-information.field.model", plain(module.model))
        rows[#rows + 1] = field("system-information.field.serial-number", plain(module.serial))
        rows[#rows + 1] = field("system-information.field.size", bytes(module.size))
        rows[#rows + 1] = field("system-information.field.frequency", frequency(module.frequency))
    end

    return card("system-information.section.memory", "memory", rows)
end

local function graphics(machine)
    local rows = {}

    if #machine.graphics == 0 then
        rows[1] = empty()
    end

    for index, adapter in ipairs(machine.graphics) do
        rows[#rows + 1] = device("system-information.common.graphics-name", index)
        rows[#rows + 1] = field("system-information.field.vendor", plain(adapter.vendor))
        rows[#rows + 1] = field("system-information.field.model", plain(adapter.name))
        rows[#rows + 1] = field("system-information.field.driver", plain(adapter.driver))
        rows[#rows + 1] = field("system-information.field.vendor-id", plain(adapter.vendorId))
        rows[#rows + 1] = field("system-information.field.device-id", plain(adapter.deviceId))
        rows[#rows + 1] = field("system-information.field.dedicated-memory", bytes(adapter.dedicatedMemory))
        rows[#rows + 1] = field("system-information.field.shared-memory", bytes(adapter.sharedMemory))
        rows[#rows + 1] = field("system-information.field.frequency", frequency(adapter.frequency))
        rows[#rows + 1] = field("system-information.field.cores", count(adapter.cores))
    end

    for index, display in ipairs(machine.displays) do
        rows[#rows + 1] = device("system-information.common.display-name", index)
        rows[#rows + 1] = field("system-information.field.model", plain(display.name))
        rows[#rows + 1] = field("system-information.field.resolution", text("system-information.common.resolution", number(display.width, 0), number(display.height, 0)))
        rows[#rows + 1] = field("system-information.field.scale", display.scale > 0 and text("system-information.common.percentage", number(display.scale * 100, 0)) or unavailable)
        rows[#rows + 1] = field("system-information.field.pixel-density", display.density > 0 and text("system-information.common.dots-per-inch", number(display.density, 0)) or unavailable)
        rows[#rows + 1] = field("system-information.field.refresh-rate", display.refreshRate > 0 and text("system-information.common.hertz", number(display.refreshRate, 0)) or unavailable)
    end

    return card("system-information.section.graphics", "graphics", rows)
end

local function mainboard(machine)
    local board = machine.mainboard

    return card("system-information.section.mainboard", "mainboard", {
        field("system-information.field.vendor", plain(board.vendor)),
        field("system-information.field.model", plain(board.name)),
        field("system-information.field.version", plain(board.version)),
        field("system-information.field.serial-number", plain(board.serial)),
    })
end

local function storage(machine)
    local rows = {}

    if #machine.disks == 0 then
        rows[1] = empty()
    end

    for index, disk in ipairs(machine.disks) do
        local volumes = {}

        for position, volume in ipairs(disk.volumes) do
            volumes[position] = text("system-information.common.volume-format", volume.mountPoint, bytes(volume.free))
        end

        rows[#rows + 1] = device("system-information.common.disk-name", index)
        rows[#rows + 1] = field("system-information.field.vendor", plain(disk.vendor))
        rows[#rows + 1] = field("system-information.field.model", plain(disk.model))
        rows[#rows + 1] = field("system-information.field.serial-number", plain(disk.serial))
        rows[#rows + 1] = field("system-information.field.interface", plain(disk.interface))
        rows[#rows + 1] = field("system-information.field.size", bytes(disk.size))
        rows[#rows + 1] = #volumes > 0 and lines("system-information.field.volumes", volumes) or field("system-information.field.volumes", unavailable)
    end

    return card("system-information.section.storage", "storage", rows)
end

local function batteries(machine)
    local rows = {}
    local states = { charging = "system-information.common.charging", discharging = "system-information.common.discharging", ["not-charging"] = "system-information.common.not-charging" }

    if #machine.batteries == 0 then
        rows[1] = empty()
    end

    for index, battery in ipairs(machine.batteries) do
        rows[#rows + 1] = device("system-information.common.battery-name", index)
        rows[#rows + 1] = field("system-information.field.vendor", plain(battery.vendor))
        rows[#rows + 1] = field("system-information.field.model", plain(battery.model))
        rows[#rows + 1] = field("system-information.field.serial-number", plain(battery.serial))
        rows[#rows + 1] = field("system-information.field.technology", plain(battery.technology))
        rows[#rows + 1] = field("system-information.field.state", text(states[battery.state] or "system-information.common.unknown"))

        if battery.capacity ~= nil then
            rows[#rows + 1] = progress("system-information.field.capacity", battery.capacity)
        end
    end

    return card("system-information.section.batteries", "battery", rows)
end

local function network(machine)
    local rows = {}

    if #machine.networkInterfaces == 0 then
        rows[1] = empty()
    end

    for index, interface in ipairs(machine.networkInterfaces) do
        rows[#rows + 1] = device("system-information.common.network-name", index)
        rows[#rows + 1] = field("system-information.field.index", count(interface.index))
        rows[#rows + 1] = field("system-information.field.description", plain(interface.description))
        rows[#rows + 1] = field("system-information.field.mac-address", plain(interface.mac))
        rows[#rows + 1] = field("system-information.field.ipv4-address", plain(interface.ipv4))
        rows[#rows + 1] = field("system-information.field.ipv6-address", plain(interface.ipv6))
    end

    return card("system-information.section.network", "network", rows)
end

local function cards(machine)
    return { overview(machine), operatingSystem(machine), processors(machine), memory(machine), graphics(machine), mainboard(machine), storage(machine), batteries(machine), network(machine) }
end

local function stateLabel(key)
    return { ui.label({ text = text(key), style = "muted", textAlign = "center" }) }
end

-- Only one collection runs at a time, and a failed one keeps the snapshot already shown and tells the reader in a notification.
local function refresh()
    if collecting then
        return
    end

    collecting = true
    refreshButton:set({ enabled = false })
    local collected, failure = workpane.system.information():await()
    collecting = false
    refreshButton:set({ enabled = true })

    if failure ~= nil then
        workpane.log.error("collection", tostring(failure), { code = failure.code, detail = failure.detail })
        workpane.notify.error(translate("system-information.error.title"), translate("system-information.error.message"))

        if snapshot == nil then
            body:setChildren(stateLabel("system-information.error.message"))
        end

        return
    end

    snapshot = collected
    header:set({ caption = text("system-information.view.updated", workpane.time.localPresentation(snapshot.capturedAt)) })
    body:setChildren(cards(snapshot))
end

function view.build()
    refreshButton = ui.button({ text = text("system-information.view.refresh"), icon = "refresh", onClick = refresh })
    header = ui.pageHeader({ title = text("system-information.plugin.title") }, { refreshButton })
    body = ui.column({ padding = 14, spacing = 12 }, stateLabel("system-information.view.collecting"))
    workpane.task(refresh)

    return ui.column({}, {
        header,
        ui.scroll({ grow = 1 }, body),
    })
end

return view
