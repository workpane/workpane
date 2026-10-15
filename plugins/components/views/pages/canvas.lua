-- The canvas drawing shapes and text, animating a picture at its frame rate and following the pointer and the keys of the reader.
local gallery = include("views/gallery")
local ui = workpane.ui
local text = workpane.i18n.text

local function shapes()
    local canvas = ui.canvas({ height = 140, background = "panel" })

    canvas:command("draw", { commands = {
        { op = "rect", x = 16, y = 16, width = 120, height = 72, color = "accent", radius = 8 },
        { op = "rect", x = 150, y = 16, width = 120, height = 72, color = "danger", filled = false, thickness = 2 },
        { op = "circle", x = 330, y = 52, radius = 36, color = "success", opacity = 0.7 },
        { op = "line", x1 = 390, y1 = 16, x2 = 470, y2 = 88, color = "text", thickness = 3 },
        { op = "clip", x = 490, y = 16, width = 60, height = 72 },
        { op = "circle", x = 520, y = 52, radius = 50, color = "#e8a33d" },
        { op = "unclip" },
        { op = "text", x = 16, y = 104, text = text("components.canvas.caption"), color = "text", size = 14 },
    } })

    return canvas
end

-- A picture turns and flies from side to side, sampled by its nearest pixel, while the canvas ticks thirty times a second.
local function animation(report)
    local angle = 0
    local position = 0
    local direction = 1
    local ticks = 0
    local canvas

    canvas = ui.canvas({ height = 140, background = "panel", frameRate = 30, pixelated = true, pictures = { "sample.png" }, onFrame = function(frame)
        angle = angle + frame.delta * 2
        position = position + direction * frame.delta * 160

        if position > frame.width - 96 or position < 0 then
            direction = -direction
            position = math.max(0, math.min(position, frame.width - 96))
        end

        ticks = ticks + 1

        if ticks % 30 == 0 then
            report("frame", { delta = frame.delta, width = frame.width })
        end

        canvas:command("draw", { commands = {
            { op = "image", image = "sample.png", x = position, y = 22, width = 96, height = 96, rotation = angle },
            { op = "text", x = frame.width - 16, y = 16, text = tostring(ticks), color = "text-muted", align = "end", face = "monospace" },
        } })
    end })

    return canvas
end

-- A dot follows the pointer and the last key pressed is written under it, once a click gives the canvas the keyboard.
local function input(report)
    local pointer = { x = 40, y = 40 }
    local key = ""
    local canvas

    local function redraw()
        canvas:command("draw", { commands = {
            { op = "circle", x = pointer.x, y = pointer.y, radius = 6, color = "accent" },
            { op = "text", x = 16, y = 110, text = key ~= "" and key or text("components.canvas.press"), color = "text-muted", size = 13 },
        } })
    end

    canvas = ui.canvas({ height = 140, background = "panel", focusable = true, tracking = true, onPointerMove = function(event)
        pointer = event
        redraw()
    end, onPointerDown = function(event)
        report("pointer-down", event)
    end, onKeyDown = function(event)
        key = event.key
        report("key-down", event)
        redraw()
    end })

    redraw()

    return canvas
end

return function(report)
    return {
        gallery.section("components.canvas.shapes", { shapes() }),
        gallery.section("components.canvas.animation", { animation(report) }),
        gallery.section("components.canvas.input", { input(report) }),
    }
end
