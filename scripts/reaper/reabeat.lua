-- REABeat: Neural beat detection and tempo mapping for REAPER
-- Entry point — run from Actions menu
--
-- Dependencies:
--   - ReaImGui 0.9+ (install via ReaPack)
--   - mavriq-lua-sockets (install via ReaPack)
--   - REABeat Python backend (auto-launched)

local SCRIPT_DIR = debug.getinfo(1, "S").source:match("@?(.*/)") or ""

-- Load ReaImGui
local ImGui
local function load_imgui()
    -- v0.9+ module API: need to add built-in path first
    if reaper.ImGui_GetBuiltinPath then
        package.path = package.path .. ';' .. reaper.ImGui_GetBuiltinPath() .. '/?.lua'
        local ok, lib = pcall(function() return require 'imgui' '0.9' end)
        if ok and lib then
            ImGui = lib
            return true
        end
    end
    -- Fallback: older API-style binding
    if reaper.ImGui_CreateContext then
        ImGui = setmetatable({}, {
            __index = function(_, key)
                return reaper["ImGui_" .. key]
            end
        })
        return true
    end
    return false
end

if not load_imgui() then
    reaper.ShowMessageBox(
        "REABeat requires ReaImGui 0.9+.\n\n" ..
        "Install via:\n" ..
        "   Extensions > ReaPack > Browse packages > ReaImGui",
        "REABeat — Missing Dependency", 0)
    return
end

-- Load modules
package.path = SCRIPT_DIR .. "?.lua;" .. package.path
local theme = require("reabeat_theme")
local ui = require("reabeat_ui")
local socket_client = require("reabeat_socket")
local server = require("reabeat_server")
local actions = require("reabeat_actions")
local waveform = require("reabeat_waveform")

-- Pass script directory to server launcher (reliable path for finding project root)
server.set_script_dir(SCRIPT_DIR)

-- ImGui constant resolver (cached)
local const_cache = {}
local function C(name)
    if not const_cache[name] then
        const_cache[name] = ImGui[name]
    end
    return const_cache[name]
end

-- Application state
local state = {
    -- Connection
    connected = false,
    connecting = false,
    launching = false,
    launch_start = 0,

    -- Item info
    item = nil,
    take = nil,
    item_name = "",
    item_duration = 0,
    audio_path = "",

    -- Detection
    detecting = false,
    detected = false,
    detect_progress = 0,
    detect_message = "",
    beats = nil,
    downbeats = nil,
    tempo = 0,
    time_sig_num = 4,
    time_sig_denom = 4,
    confidence = 0,
    backend = "",
    detection_time = 0,
    peaks = nil,
    audio_duration = 0,

    -- UI
    action_mode = 1,         -- 1=Tempo Map, 2=Stretch Markers
    tempo_mode = 1,          -- 1=Constant, 2=Variable (per bar)
    marker_mode = 1,         -- 1=Every beat, 2=Downbeats only
    status_message = "",
    status_color = nil,
    last_apply_count = 0,
}

-- Get stable item identifier (survives re-selection)
local function get_item_guid(item)
    if not item then return nil end
    -- Try SWS BR_GetMediaItemGUID first (most reliable)
    if reaper.BR_GetMediaItemGUID then
        return reaper.BR_GetMediaItemGUID(item)
    end
    -- Fallback: use the item's take source filename + position as stable ID
    local take = reaper.GetActiveTake(item)
    if take then
        local source = reaper.GetMediaItemTake_Source(take)
        local filename = reaper.GetMediaSourceFileName(source)
        local pos = reaper.GetMediaItemInfo_Value(item, "D_POSITION")
        return filename .. "|" .. tostring(pos)
    end
    return tostring(item)
end

-- Track selected item changes
local function update_selected_item()
    local item = reaper.GetSelectedMediaItem(0, 0)
    if not item then
        -- No selection — keep data, just update item reference to nil
        -- DON'T clear detection results (user just clicked empty space)
        state.item = nil
        state.take = nil
        return
    end

    -- Check if it's the same item we already analyzed
    local guid = get_item_guid(item)
    if guid == state.item_guid then
        -- Same item — just refresh the pointer (may have changed)
        state.item = item
        state.take = reaper.GetActiveTake(item)
        return
    end

    -- Different item — reset detection
    state.item = item
    state.item_guid = guid
    state.take = reaper.GetActiveTake(item)
    state.detected = false
    state.beats = nil
    state.peaks = nil
    state.detect_progress = 0
    state.detect_message = ""

    if state.take and not reaper.TakeIsMIDI(state.take) then
        local source = reaper.GetMediaItemTake_Source(state.take)
        if source then
            local filename = reaper.GetMediaSourceFileName(source)
            state.audio_path = filename or ""
            state.item_name = (filename or ""):match("([^/\\]+)$") or "(unknown)"
            state.item_duration = reaper.GetMediaItemInfo_Value(item, "D_LENGTH")
        else
            state.item_name = "(no audio source)"
            state.item_duration = 0
            state.audio_path = ""
        end
    elseif state.take and reaper.TakeIsMIDI(state.take) then
        state.item_name = "(MIDI item — select audio)"
        state.item_duration = 0
        state.audio_path = ""
        state.take = nil
    else
        state.item_name = "(empty item)"
        state.item_duration = 0
        state.audio_path = ""
    end
end

-- Connection and server management
local function try_connect()
    if state.connected then return end
    -- Try direct connection first
    local ok = socket_client.connect()
    if ok then
        state.connected = true
        state.launching = false
        state.status_message = "Connected"
        state.status_color = "success"
        return
    end
    -- Not connected — launch server
    if not state.launching then
        local result = server.launch()
        if result == "running" then
            -- Server already running, try connect again
            ok = socket_client.connect()
            if ok then
                state.connected = true
                state.status_message = "Connected"
                state.status_color = "success"
            end
        elseif result == "launching" then
            state.launching = true
            state.launch_start = os.clock()
            state.status_message = "Starting backend..."
            state.status_color = "warning"
        elseif result == "error" then
            state.status_message = server.get_error() or "Backend launch failed"
            state.status_color = "error"
        end
    end
end

local function poll_connection()
    if not state.launching then return end
    local result = server.poll()
    if result == "running" then
        local ok = socket_client.connect()
        if ok then
            state.connected = true
            state.launching = false
            state.status_message = "Connected"
            state.status_color = "success"
        end
    elseif result == "error" then
        state.launching = false
        state.status_message = server.get_error() or "Backend failed to start"
        state.status_color = "error"
    end
    -- "launching" — keep waiting
end

-- Detection request/response
local function start_detection()
    if not state.connected or state.detecting or state.audio_path == "" then return end
    state.detecting = true
    state.detected = false
    state.detect_progress = 0
    state.detect_message = "Starting..."
    state.status_message = "Detecting beats..."
    state.status_color = nil
    local ok = socket_client.send({
        cmd = "detect",
        params = { audio_path = state.audio_path }
    })
    if not ok then
        -- Connection lost — trigger reconnect
        state.connected = false
        state.detecting = false
        state.status_message = "Connection lost — reconnecting..."
        state.status_color = "error"
        try_connect()
    end
end

local function poll_responses()
    if not state.connected then return end
    local msg = socket_client.poll()
    if not msg then return end

    if msg.status == "progress" then
        state.detect_progress = msg.progress or 0
        state.detect_message = msg.message or ""
    elseif msg.status == "ok" and msg.result then
        local r = msg.result
        -- Handle ping response (backend check)
        if r.pong then
            if not r.backend_ok then
                state.status_message = "Backend error: beat-this not installed"
                state.status_color = "error"
                reaper.ShowMessageBox(
                    r.backend_msg or "beat-this is not installed.",
                    "REABeat — Backend Not Ready", 0)
            end
            return
        end
        state.beats = r.beats
        state.downbeats = r.downbeats
        state.tempo = r.tempo or 0
        state.time_sig_num = r.time_sig_num or 4
        state.time_sig_denom = r.time_sig_denom or 4
        state.confidence = r.confidence or 0
        state.backend = r.backend or "unknown"
        state.detection_time = r.detection_time or 0
        state.audio_duration = r.duration or state.item_duration
        state.peaks = r.peaks
        state.detecting = false
        state.detected = true
        state.detect_progress = 1.0
        state.detect_message = ""
        state.status_message = string.format(
            "%d beats, %.1f BPM (%s, %.1fs)",
            #state.beats, state.tempo, state.backend, state.detection_time)
        state.status_color = "success"
    elseif msg.status == "error" then
        state.detecting = false
        state.status_message = msg.message or "Detection failed"
        state.status_color = "error"
    end
end

-- Apply action
local function apply_action()
    if not state.detected or not state.beats then return end

    local count = 0
    if state.action_mode == 1 then
        -- Tempo Map
        count = actions.insert_tempo_map(
            state.beats, state.downbeats, state.tempo,
            state.time_sig_num, state.time_sig_denom,
            state.item, state.tempo_mode == 2)
        state.status_message = string.format("%d tempo markers inserted", count)
    else
        -- Stretch Markers
        local use_downbeats = state.marker_mode == 2
        local beat_list = use_downbeats and state.downbeats or state.beats
        count = actions.insert_stretch_markers(state.take, beat_list, state.item)
        state.status_message = string.format("%d stretch markers inserted", count)
    end
    state.status_color = "success"
    state.last_apply_count = count
end

-- Keyboard shortcuts
local function handle_keys(ctx)
    if ImGui.IsKeyPressed(ctx, C("Key_Escape")) then
        -- Nothing to cancel in REABeat
    end
    if ImGui.IsKeyPressed(ctx, C("Key_Enter")) and state.detected then
        apply_action()
    end
end

-- Main draw function
local ctx
local WINDOW_FLAGS

local function draw_frame()
    update_selected_item()
    poll_connection()
    poll_responses()

    ImGui.SetNextWindowSize(ctx, 420, 360, C("Cond_FirstUseEver"))
    ImGui.SetNextWindowSizeConstraints(ctx, 360, 260, 560, 600)
    WINDOW_FLAGS = C("WindowFlags_NoCollapse")

    local theme_colors, theme_vars = theme.push(ctx, ImGui, C)

    local visible, open = ImGui.Begin(ctx, "REABeat", true, WINDOW_FLAGS)
    if visible then
        handle_keys(ctx)
        ui.draw(ctx, ImGui, C, state, {
            on_detect = start_detection,
            on_apply = apply_action,
            on_connect = try_connect,
            draw_waveform = function(x, y, w, h)
                waveform.draw(ctx, ImGui, C, state, x, y, w, h)
            end,
        })
        ImGui.End(ctx)
    end

    theme.pop(ctx, ImGui, theme_colors, theme_vars)

    if not open then return false end
    return true
end

-- Init
local function init()
    local config_flags = C("ConfigFlags_DockingEnable")
    ctx = ImGui.CreateContext("REABeat", config_flags)
    if not ctx then
        reaper.ShowMessageBox("Failed to create ImGui context", "REABeat", 0)
        return false
    end
    try_connect()
    return true
end

-- Main loop
local function loop()
    if not draw_frame() then return end
    reaper.defer(loop)
end

if init() then
    reaper.defer(loop)
end
