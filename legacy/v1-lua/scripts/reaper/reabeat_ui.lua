-- ReaBeat UI: All drawing functions
-- Premium, REAPER-native feel. Compact, functional, musical.

local ui = {}
local theme = require("reabeat_theme")

local function fmt_time(s)
    if not s or s < 0 then return "0:00" end
    return string.format("%d:%02d", math.floor(s / 60), math.floor(s) % 60)
end

function ui.draw(ctx, ImGui, C, state, callbacks)
    local c = theme.colors
    local avail_w = ImGui.GetContentRegionAvail(ctx)

    -- Header
    draw_header(ctx, ImGui, C, state, avail_w, callbacks)

    ImGui.Spacing(ctx)
    ImGui.Separator(ctx)
    ImGui.Spacing(ctx)

    -- Source info + Detect button
    draw_source(ctx, ImGui, C, state, avail_w, callbacks)

    ImGui.Spacing(ctx)

    -- Detection results
    if state.detected then
        draw_results(ctx, ImGui, C, state, avail_w)
        ImGui.Spacing(ctx)
        ImGui.Separator(ctx)
        ImGui.Spacing(ctx)

        -- Action selection
        draw_actions(ctx, ImGui, C, state, avail_w, callbacks)
        ImGui.Spacing(ctx)

        -- Apply button
        draw_apply(ctx, ImGui, C, state, avail_w, callbacks)
    elseif state.detecting then
        draw_progress(ctx, ImGui, C, state, avail_w)
    end

    ImGui.Spacing(ctx)

    -- Status bar (bottom)
    draw_status(ctx, ImGui, C, state, avail_w)
end

function draw_header(ctx, ImGui, C, state, w, callbacks)
    local c = theme.colors

    -- Title + version
    ImGui.PushStyleColor(ctx, C("Col_Text"), c.accent)
    ImGui.Text(ctx, "ReaBeat")
    ImGui.PopStyleColor(ctx, 1)
    ImGui.SameLine(ctx)
    ImGui.PushStyleColor(ctx, C("Col_Text"), c.text_dim)
    ImGui.Text(ctx, "v1.3.1")
    ImGui.PopStyleColor(ctx, 1)

    -- Support button (next to title)
    ImGui.SameLine(ctx)
    ImGui.PushStyleColor(ctx, C("Col_Text"), c.text_dim)
    if ImGui.SmallButton(ctx, "Support") then
        ImGui.OpenPopup(ctx, "support_popup")
    end
    ImGui.PopStyleColor(ctx, 1)

    if ImGui.BeginPopup(ctx, "support_popup") then
        ImGui.PushStyleColor(ctx, C("Col_Text"), c.text_bright)
        ImGui.Text(ctx, "ReaBeat is free & open source.")
        ImGui.Text(ctx, "If it saves you time, consider supporting:")
        ImGui.PopStyleColor(ctx, 1)
        ImGui.Spacing(ctx)
        ImGui.Separator(ctx)
        ImGui.Spacing(ctx)

        if ImGui.Selectable(ctx, "Ko-fi") then
            reaper.CF_ShellExecute("https://ko-fi.com/quickmd")
        end
        if ImGui.Selectable(ctx, "Buy Me a Coffee") then
            reaper.CF_ShellExecute("https://buymeacoffee.com/bsroczynskh")
        end
        if ImGui.Selectable(ctx, "PayPal") then
            reaper.CF_ShellExecute("https://www.paypal.com/paypalme/b451c")
        end

        ImGui.Spacing(ctx)
        ImGui.Separator(ctx)
        ImGui.Spacing(ctx)
        ImGui.PushStyleColor(ctx, C("Col_Text"), c.text_dim)
        ImGui.Text(ctx, "Thank you!")
        ImGui.PopStyleColor(ctx, 1)

        ImGui.EndPopup(ctx)
    end

    -- Connection status (right-aligned)
    ImGui.SameLine(ctx, w - 110)
    if state.connected then
        ImGui.PushStyleColor(ctx, C("Col_Text"), c.success)
        ImGui.Text(ctx, "Connected")
        ImGui.PopStyleColor(ctx, 1)
    elseif state.launching then
        local elapsed = os.clock() - (state.launch_start or os.clock())
        ImGui.PushStyleColor(ctx, C("Col_Text"), c.warning)
        ImGui.Text(ctx, string.format("Starting... %ds", math.floor(elapsed)))
        ImGui.PopStyleColor(ctx, 1)
    else
        ImGui.PushStyleColor(ctx, C("Col_Text"), c.error)
        ImGui.Text(ctx, "Offline")
        ImGui.PopStyleColor(ctx, 1)
        ImGui.SameLine(ctx)
        if ImGui.SmallButton(ctx, "Retry") then
            callbacks.on_connect()
        end
    end
end

function draw_source(ctx, ImGui, C, state, w, callbacks)
    local c = theme.colors

    if not state.item then
        ImGui.PushStyleColor(ctx, C("Col_Text"), c.text_dim)
        ImGui.Text(ctx, "Select a media item in REAPER to begin.")
        ImGui.PopStyleColor(ctx, 1)
        return
    end

    -- Item name and duration (truncate long names to prevent overlap with Detect button)
    local display_name = state.item_name
    local max_name = math.max(10, math.floor((w - 160) / 7))
    if #display_name > max_name then
        display_name = display_name:sub(1, max_name - 3) .. "..."
    end
    ImGui.PushStyleColor(ctx, C("Col_Text"), c.text_bright)
    ImGui.Text(ctx, display_name)
    ImGui.PopStyleColor(ctx, 1)
    if display_name ~= state.item_name and ImGui.IsItemHovered(ctx, C("HoveredFlags_ForTooltip")) then
        ImGui.SetTooltip(ctx, state.item_name)
    end
    ImGui.SameLine(ctx)
    ImGui.PushStyleColor(ctx, C("Col_Text"), c.text_dim)
    ImGui.Text(ctx, string.format("(%s)", fmt_time(state.item_duration)))
    ImGui.PopStyleColor(ctx, 1)

    -- Detect button (right side)
    ImGui.SameLine(ctx, w - 120)
    local can_detect = state.connected and not state.detecting and state.audio_path ~= ""
    if not can_detect then ImGui.BeginDisabled(ctx) end
    local n = theme.push_button(ctx, ImGui, C, "detect")
    local label = state.detected and "Re-detect" or "Detect Beats"
    if ImGui.Button(ctx, label, 112, 28) then
        callbacks.on_detect()
    end
    if ImGui.IsItemHovered(ctx, C("HoveredFlags_ForTooltip")) then
        ImGui.SetTooltip(ctx, "Analyze beats, tempo, and time signature.\nNeural beat detection powered by beat-this (CPJKU, ISMIR 2024).")
    end
    ImGui.PopStyleColor(ctx, n)
    if not can_detect then ImGui.EndDisabled(ctx) end
end

function draw_progress(ctx, ImGui, C, state, w)
    local c = theme.colors

    -- Progress bar with smooth fill
    ImGui.PushStyleColor(ctx, C("Col_PlotHistogram"), c.progress_fill)
    ImGui.ProgressBar(ctx, state.detect_progress, w, 22,
        state.detect_message ~= "" and state.detect_message or "Detecting...")
    ImGui.PopStyleColor(ctx, 1)
end

function draw_results(ctx, ImGui, C, state, w)
    local c = theme.colors

    -- Editable BPM field
    ImGui.PushStyleColor(ctx, C("Col_FrameBg"), 0x33333300)  -- transparent bg
    ImGui.PushStyleColor(ctx, C("Col_FrameBgHovered"), c.frame_hover)
    ImGui.PushStyleColor(ctx, C("Col_FrameBgActive"), c.frame_active)
    ImGui.PushStyleColor(ctx, C("Col_Text"), c.accent)
    ImGui.SetNextItemWidth(ctx, 52)
    local changed, new_bpm = ImGui.InputDouble(ctx, "##bpm", state.tempo, 0, 0, "%.1f")
    if changed then
        state.tempo = math.max(20, math.min(400, new_bpm))
    end
    ImGui.PopStyleColor(ctx, 4)
    if ImGui.IsItemHovered(ctx, C("HoveredFlags_ForTooltip")) then
        ImGui.SetTooltip(ctx, "Detected tempo. Click to edit if detection is wrong.\nAffects Match Tempo rate calculation.")
    end

    ImGui.SameLine(ctx)
    ImGui.PushStyleColor(ctx, C("Col_Text"), c.text_dim)
    -- Show original detection if user edited
    if state.detected_tempo_original and math.abs(state.tempo - state.detected_tempo_original) > 0.1 then
        ImGui.Text(ctx, string.format("BPM (was %.1f)", state.detected_tempo_original))
    else
        ImGui.Text(ctx, "BPM")
    end
    ImGui.PopStyleColor(ctx, 1)

    ImGui.SameLine(ctx)
    ImGui.PushStyleColor(ctx, C("Col_Text"), c.text_dim)
    ImGui.Text(ctx, "|")
    ImGui.PopStyleColor(ctx, 1)

    ImGui.SameLine(ctx)
    ImGui.Text(ctx, string.format("%d/%d", state.time_sig_num, state.time_sig_denom))

    ImGui.SameLine(ctx)
    ImGui.PushStyleColor(ctx, C("Col_Text"), c.text_dim)
    ImGui.Text(ctx, "|")
    ImGui.PopStyleColor(ctx, 1)

    ImGui.SameLine(ctx)
    ImGui.Text(ctx, string.format("%d beats", #state.beats))

    ImGui.SameLine(ctx)
    ImGui.PushStyleColor(ctx, C("Col_Text"), c.text_dim)
    ImGui.Text(ctx, "|")
    ImGui.PopStyleColor(ctx, 1)

    ImGui.SameLine(ctx)
    ImGui.Text(ctx, string.format("%d bars", #state.downbeats))

    -- Confidence indicator
    ImGui.SameLine(ctx, w - 100)
    local conf_color = c.success
    if state.confidence < 0.7 then conf_color = c.warning end
    if state.confidence < 0.5 then conf_color = c.error end
    ImGui.PushStyleColor(ctx, C("Col_Text"), conf_color)
    ImGui.Text(ctx, string.format("%.0f%% confidence", state.confidence * 100))
    ImGui.PopStyleColor(ctx, 1)
end

function draw_actions(ctx, ImGui, C, state, w, callbacks)
    local c = theme.colors

    ImGui.PushStyleColor(ctx, C("Col_Text"), c.text_bright)
    ImGui.Text(ctx, "Action")
    ImGui.PopStyleColor(ctx, 1)

    ImGui.Spacing(ctx)

    -- 1. Match Tempo (simplest - one click, changes speed)
    if ImGui.RadioButton(ctx, "Match Tempo", state.action_mode == 3) then
        state.action_mode = 3
    end
    if ImGui.IsItemHovered(ctx, C("HoveredFlags_ForTooltip")) then
        ImGui.SetTooltip(ctx, "Change item playback rate to match a target BPM.\nPitch is preserved (elastique). Original file unchanged.")
    end

    if state.action_mode == 3 then
        ImGui.Indent(ctx, 20)
        local project_bpm = callbacks.get_project_bpm and callbacks.get_project_bpm() or 120
        local n_btn = theme.push_button(ctx, ImGui, C, "detect")
        if ImGui.Button(ctx, string.format("Match to project (%.1f BPM)", project_bpm), 0, 24) then
            state.target_bpm = project_bpm
        end
        ImGui.PopStyleColor(ctx, n_btn)
        if ImGui.IsItemHovered(ctx, C("HoveredFlags_ForTooltip")) then
            ImGui.SetTooltip(ctx, "Set target BPM to current REAPER project tempo.")
        end
        ImGui.Text(ctx, "Target BPM:")
        ImGui.SameLine(ctx)
        ImGui.SetNextItemWidth(ctx, 80)
        local changed, new_val = ImGui.InputDouble(ctx, "##target_bpm", state.target_bpm or project_bpm, 0, 0, "%.1f")
        if changed then
            state.target_bpm = new_val
        end
        if state.target_bpm and state.tempo > 0 then
            local rate = state.target_bpm / state.tempo
            ImGui.PushStyleColor(ctx, C("Col_Text"), c.text_dim)
            ImGui.Text(ctx, string.format("%.1f BPM -> %.1f BPM (%.2fx speed, pitch preserved)",
                state.tempo, state.target_bpm, rate))
            ImGui.PopStyleColor(ctx, 1)
        end
        local align_changed, align_val = ImGui.Checkbox(ctx, "Align first downbeat to bar", state.align_to_bar)
        if align_changed then state.align_to_bar = align_val end
        if ImGui.IsItemHovered(ctx, C("HoveredFlags_ForTooltip")) then
            ImGui.SetTooltip(ctx, "After matching tempo, shift the item so the first\ndetected downbeat lands on the nearest bar line.\nSaves manual alignment.")
        end
        ImGui.Unindent(ctx, 20)
    end

    ImGui.Spacing(ctx)

    -- 2. Insert Tempo Map (sync grid to audio)
    if ImGui.RadioButton(ctx, "Insert Tempo Map", state.action_mode == 1) then
        state.action_mode = 1
    end
    if ImGui.IsItemHovered(ctx, C("HoveredFlags_ForTooltip")) then
        ImGui.SetTooltip(ctx, "Sync REAPER's grid to the audio tempo.\nDoes NOT modify the audio — only changes the tempo map.\nUse for live recordings or songs with tempo changes.")
    end

    if state.action_mode == 1 then
        ImGui.Indent(ctx, 20)
        if ImGui.RadioButton(ctx, string.format("Constant (%.1f BPM)", state.tempo), state.tempo_map_mode == 1) then
            state.tempo_map_mode = 1
        end
        if ImGui.IsItemHovered(ctx, C("HoveredFlags_ForTooltip")) then
            ImGui.SetTooltip(ctx, "Single tempo marker. Best for songs with steady tempo.")
        end
        if ImGui.RadioButton(ctx, string.format("Variable - bars (%d markers)", #state.downbeats), state.tempo_map_mode == 2) then
            state.tempo_map_mode = 2
        end
        if ImGui.IsItemHovered(ctx, C("HoveredFlags_ForTooltip")) then
            ImGui.SetTooltip(ctx, "Per-bar tempo markers at each downbeat.\nBest for live recordings with tempo changes.")
        end
        if ImGui.RadioButton(ctx, string.format("Variable - beats (%d markers)", #state.beats), state.tempo_map_mode == 3) then
            state.tempo_map_mode = 3
        end
        if ImGui.IsItemHovered(ctx, C("HoveredFlags_ForTooltip")) then
            ImGui.SetTooltip(ctx, "Per-beat tempo markers for maximum precision.\nBest for rubato, swing, or complex rhythms.")
        end
        ImGui.Unindent(ctx, 20)
    end

    ImGui.Spacing(ctx)

    -- 3. Insert Stretch Markers
    if ImGui.RadioButton(ctx, "Insert Stretch Markers", state.action_mode == 2) then
        state.action_mode = 2
    end
    if ImGui.IsItemHovered(ctx, C("HoveredFlags_ForTooltip")) then
        ImGui.SetTooltip(ctx, "Insert stretch markers at detected beat positions.\nUse REAPER's stretch marker editing to quantize/straighten timing.")
    end

    if state.action_mode == 2 then
        ImGui.Indent(ctx, 20)
        if ImGui.RadioButton(ctx, string.format("Every beat (%d markers)", #state.beats), state.marker_mode == 1) then
            state.marker_mode = 1
        end
        if ImGui.RadioButton(ctx, string.format("Downbeats only (%d markers)", #state.downbeats), state.marker_mode == 2) then
            state.marker_mode = 2
        end
        draw_stretch_mode(ctx, ImGui, C, state)
        local q_changed, q_val = ImGui.Checkbox(ctx, "Quantize to grid", state.quantize_markers)
        if q_changed then state.quantize_markers = q_val end
        if ImGui.IsItemHovered(ctx, C("HoveredFlags_ForTooltip")) then
            ImGui.SetTooltip(ctx, "Snap each stretch marker to the nearest beat in\nREAPER's project grid. Does not modify the tempo map.")
        end
        ImGui.Unindent(ctx, 20)
    end

end

-- Stretch marker algorithm selector
-- Maps to REAPER's I_STRETCHFLAGS: 0=default, 1=balanced, 4=transient, 2=tonal
local STRETCH_MODES = {
    { label = "Balanced",  flag = 1, tip = "General purpose. Good for most material." },
    { label = "Transient", flag = 4, tip = "Preserves attacks. Best for drums, percussion, rhythmic material." },
    { label = "Tonal",     flag = 2, tip = "Preserves pitch quality. Best for vocals, melodic instruments, pads." },
}

function draw_stretch_mode(ctx, ImGui, C, state)
    local c = theme.colors
    local mode_idx = state.stretch_mode or 1

    ImGui.Text(ctx, "Stretch quality:")
    ImGui.SameLine(ctx)
    ImGui.SetNextItemWidth(ctx, 110)
    if ImGui.BeginCombo(ctx, "##stretch_mode", STRETCH_MODES[mode_idx].label) then
        for i, mode in ipairs(STRETCH_MODES) do
            local selected = (i == mode_idx)
            if ImGui.Selectable(ctx, mode.label, selected) then
                state.stretch_mode = i
            end
            if ImGui.IsItemHovered(ctx, C("HoveredFlags_ForTooltip")) then
                ImGui.SetTooltip(ctx, mode.tip)
            end
        end
        ImGui.EndCombo(ctx)
    end
    if ImGui.IsItemHovered(ctx, C("HoveredFlags_ForTooltip")) then
        ImGui.SetTooltip(ctx, STRETCH_MODES[mode_idx].tip)
    end
end

function draw_apply(ctx, ImGui, C, state, w, callbacks)
    local n = theme.push_button(ctx, ImGui, C, "apply")
    if ImGui.Button(ctx, "Apply  (Enter)", w, 32) then
        callbacks.on_apply()
    end
    ImGui.PopStyleColor(ctx, n)
    if ImGui.IsItemHovered(ctx, C("HoveredFlags_ForTooltip")) then
        ImGui.SetTooltip(ctx, "Apply the selected action. Ctrl+Z to undo.")
    end
end

function draw_status(ctx, ImGui, C, state, w)
    local c = theme.colors
    if state.status_message == "" then return end
    local color = c.text_dim
    if state.status_color == "success" then color = c.success end
    if state.status_color == "error" then color = c.error end
    if state.status_color == "warning" then color = c.warning end
    ImGui.PushStyleColor(ctx, C("Col_Text"), color)
    ImGui.Text(ctx, state.status_message)
    ImGui.PopStyleColor(ctx, 1)
end

return ui
