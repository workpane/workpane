-- Plays the sounds of the assets of each plugin and stops them when the plugin stops.
local bridge = require("workpane.bridge")
local lifecycle = require("workpane.lifecycle")

local sounds = {}

-- Plays a sound file of the assets of a plugin and answers the identity of the sound.
function sounds.play(owner, path, options)
    lifecycle.check(owner)
    local chosen = options or {}

    if type(chosen) ~= "table" then
        bridge.raise("audio_options_invalid", "A sound is played with a table of options", tostring(path))
    end

    return bridge.call("workpane_audio_play", { plugin = owner, path = path, volume = chosen.volume, loop = chosen.loop == true }).sound
end

function sounds.stop(owner, sound)
    bridge.call("workpane_audio_stop", { plugin = owner, sound = sound })
end

function sounds.stopAll(owner)
    bridge.call("workpane_audio_forget", { plugin = owner })
end

-- A plugin that stops takes its sounds with it, while the host still knows it.
function sounds.forget(owner)
    pcall(bridge.call, "workpane_audio_forget", { plugin = owner })
end

return sounds
