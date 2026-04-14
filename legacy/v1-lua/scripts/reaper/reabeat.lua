-- ReaBeat: Neural beat detection and tempo mapping for REAPER
-- Entry point — run from Actions menu
--
-- Dependencies:
--   - ReaImGui 0.9+ (install via ReaPack)
--   - mavriq-lua-sockets (install via ReaPack)
--   - ReaBeat Python backend (auto-launched)

local function get_script_dir()
    local src = (debug.getinfo(1, "S") or {}).source or ""
    local path = src:match("^@(.+)$") or src
    return path:match("^(.*[\\/])") or ""
end
local SCRIPT_DIR = get_script_dir()

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
        "ReaBeat requires ReaImGui 0.9+.\n\n" ..
        "Install via:\n" ..
        "   Extensions > ReaPack > Browse packages > ReaImGui",
        "ReaBeat — Missing Dependency", 0)
    return
end

-- Load modules
package.path = SCRIPT_DIR .. "?.lua;" .. package.path
local theme = require("reabeat_theme")
local ui = require("reabeat_ui")
local socket_client = require("reabeat_socket")
local server = require("reabeat_server")
local actions = require("reabeat_actions")

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

-- Detection cache: stores results per item GUID so switching items doesn't lose data
local detection_cache = {}  -- keyed by item GUID

-- Application state
local state = {
    -- Connection
    connected = false,
    launching = false,
    launch_start = 0,

    -- Item info
    item = nil,
    take = nil,
    item_guid = nil,
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
    action_mode = 3,         -- 3=Match Tempo, 1=Insert Tempo Map, 2=Insert Stretch Markers
    tempo_map_mode = 1,      -- 1=Constant, 2=Variable (per-bar)
    marker_mode = 1,         -- 1=Every beat, 2=Downbeats only
    target_bpm = nil,        -- Target BPM for Match Tempo (nil = use project BPM)
    align_to_bar = true,     -- Auto-align first downbeat to bar after Match Tempo
    stretch_mode = 1,        -- 1=Balanced, 2=Transient, 3=Tonal (index into STRETCH_MODES)
    quantize_markers = false, -- Quantize stretch markers to grid (mode 2 only)
    status_message = "",
    status_color = nil,
    last_apply_count = 0,
}

-- Save current detection to cache
local function save_to_cache()
    if not state.item_guid or not state.detected then return end
    detection_cache[state.item_guid] = {
        beats = state.beats,
        downbeats = state.downbeats,
        tempo = state.tempo,
        time_sig_num = state.time_sig_num,
        time_sig_denom = state.time_sig_denom,
        confidence = state.confidence,
        backend = state.backend,
        detection_time = state.detection_time,
        audio_duration = state.audio_duration,
    }
end

-- Restore detection from cache (returns true if found)
local function restore_from_cache(guid)
    local cached = detection_cache[guid]
    if not cached then return false end
    state.beats = cached.beats
    state.downbeats = cached.downbeats
    state.tempo = cached.tempo
    state.time_sig_num = cached.time_sig_num
    state.time_sig_denom = cached.time_sig_denom
    state.confidence = cached.confidence
    state.backend = cached.backend
    state.detection_time = cached.detection_time
    state.audio_duration = cached.audio_duration
    state.detected = true
    state.detect_progress = 1.0
    state.detect_message = ""
    state.status_message = string.format(
        "%d beats, %.1f BPM (cached)",
        #state.beats, state.tempo)
    state.status_color = "success"
    return true
end

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
        state.item = nil
        state.take = nil
        return
    end

    -- Check if it's the same item we already analyzed
    local guid = get_item_guid(item)
    if guid == state.item_guid then
        -- Same item — just refresh the pointer
        state.item = item
        state.take = reaper.GetActiveTake(item)
        return
    end

    -- Different item — save current detection to cache before switching
    save_to_cache()

    -- Switch to new item
    state.item = item
    state.item_guid = guid
    state.take = reaper.GetActiveTake(item)

    -- Try to restore from cache (previous detection for this item)
    if not restore_from_cache(guid) then
        -- No cache — reset detection state
        state.detected = false
        state.beats = nil
        state.detect_progress = 0
        state.detect_message = ""
    end

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
                    "ReaBeat — Backend Not Ready", 0)
            end
            return
        end
        state.beats = r.beats
        state.downbeats = r.downbeats
        state.tempo = r.tempo or 0
        state.detected_tempo_original = state.tempo
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
        -- Cache detection for this item
        save_to_cache()
    elseif msg.status == "error" then
        state.detecting = false
        state.status_message = msg.message or "Detection failed"
        state.status_color = "error"
    end
end

-- Stretch mode flag lookup (matches STRETCH_MODES in reabeat_ui.lua)
local STRETCH_FLAGS = { [1] = 1, [2] = 4, [3] = 2 }  -- balanced, transient, tonal

-- Apply action
local function apply_action()
    if not state.detected or not state.beats then return end

    local stretch_flag = STRETCH_FLAGS[state.stretch_mode]

    local count = 0
    if state.action_mode == 3 then
        -- Match Tempo
        local target = state.target_bpm or actions.get_project_bpm()
        if target and target > 0 and state.tempo > 0 then
            local first_db = nil
            if state.align_to_bar and state.downbeats and #state.downbeats > 0 then
                first_db = state.downbeats[1]
            end
            local ok = actions.match_tempo(state.take, state.item, state.tempo, target, first_db)
            if ok then
                local msg = string.format("Tempo matched: %.1f -> %.1f BPM", state.tempo, target)
                if first_db then msg = msg .. " (aligned)" end
                state.status_message = msg
                state.status_color = "success"
            end
        else
            state.status_message = "Set a target BPM first"
            state.status_color = "warning"
        end
    elseif state.action_mode == 1 then
        -- Insert Tempo Map (sync grid to audio, don't modify item)
        local mode, beat_list, beats_per_marker
        if state.tempo_map_mode == 1 then
            mode = "constant"
            beat_list = state.downbeats
            beats_per_marker = state.time_sig_num
        elseif state.tempo_map_mode == 2 then
            mode = "variable_bars"
            beat_list = state.downbeats
            beats_per_marker = state.time_sig_num
        else
            mode = "variable_beats"
            beat_list = state.beats
            beats_per_marker = 1
        end
        count = actions.insert_tempo_map(
            state.take, state.item, state.tempo, beat_list, beats_per_marker,
            state.time_sig_num, state.time_sig_denom, mode)
        if count > 0 then
            state.status_message = string.format("%d tempo marker(s) inserted", count)
            state.status_color = "success"
        end
    elseif state.action_mode == 2 then
        -- Insert Stretch Markers (optionally quantized to REAPER grid)
        local do_quantize = state.quantize_markers
        local use_downbeats = state.marker_mode == 2
        local beat_list = use_downbeats and state.downbeats or state.beats
        count = actions.insert_stretch_markers(state.take, beat_list, state.item, do_quantize, stretch_flag)
        if count > 0 then
            local label = do_quantize and "quantized" or "inserted"
            state.status_message = string.format("%d stretch markers %s", count, label)
            state.status_color = "success"
        end
    end
    state.last_apply_count = count
end

-- Keyboard shortcuts
local function handle_keys(ctx)
    if ImGui.IsKeyPressed(ctx, C("Key_Escape")) then
        -- Nothing to cancel in ReaBeat
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
    ImGui.SetNextWindowSizeConstraints(ctx, 420, 260, 560, 600)
    WINDOW_FLAGS = C("WindowFlags_NoCollapse")

    local theme_colors, theme_vars = theme.push(ctx, ImGui, C)

    local visible, open = ImGui.Begin(ctx, "ReaBeat", true, WINDOW_FLAGS)
    if visible then
        handle_keys(ctx)
        ui.draw(ctx, ImGui, C, state, {
            on_detect = start_detection,
            on_apply = apply_action,
            on_connect = try_connect,
            get_project_bpm = actions.get_project_bpm,
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
    ctx = ImGui.CreateContext("ReaBeat", config_flags)
    if not ctx then
        reaper.ShowMessageBox("Failed to create ImGui context", "ReaBeat", 0)
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
