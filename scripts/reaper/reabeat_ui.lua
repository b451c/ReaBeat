-- ReaBeat UI: All drawing functions
-- Premium, REAPER-native feel. Compact, functional, musical.

local ui = {}
local theme = require("reabeat_theme")

local function fmt_time(s)
    if not s or s < 0 then return "0:00" end
    return string.format("%d:%02d", math.floor(s / 60), math.floor(s) % 60)
end

local function fmt_time_ms(s)
    if not s or s < 0 then return "0:00.0" end
    return string.format("%d:%04.1f", math.floor(s / 60), s % 60)
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

    -- Title
    ImGui.PushStyleColor(ctx, C("Col_Text"), c.accent)
    ImGui.Text(ctx, "ReaBeat")
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

    -- Item name and duration
    ImGui.PushStyleColor(ctx, C("Col_Text"), c.text_bright)
    ImGui.Text(ctx, state.item_name)
    ImGui.PopStyleColor(ctx, 1)
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
        ImGui.SetTooltip(ctx, "Analyze beats, tempo, and time signature using neural detection.\nBackends: beat-this > madmom > librosa (auto-fallback)")
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

    -- Compact result line: BPM · Time Sig · Beats · Confidence
    ImGui.PushStyleColor(ctx, C("Col_Text"), c.accent)
    ImGui.Text(ctx, string.format("%.1f BPM", state.tempo))
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

    -- Radio: Tempo Map
    if ImGui.RadioButton(ctx, "Insert Tempo Map", state.action_mode == 1) then
        state.action_mode = 1
    end
    if ImGui.IsItemHovered(ctx, C("HoveredFlags_ForTooltip")) then
        ImGui.SetTooltip(ctx, "Insert tempo/time signature markers at detected bar positions.\nAligns REAPER's grid to the audio.")
    end

    if state.action_mode == 1 then
        ImGui.Indent(ctx, 20)
        if ImGui.RadioButton(ctx, string.format("Constant (%.1f BPM)", state.tempo), state.tempo_mode == 1) then
            state.tempo_mode = 1
        end
        if ImGui.IsItemHovered(ctx, C("HoveredFlags_ForTooltip")) then
            ImGui.SetTooltip(ctx, "Single tempo marker at song start.\nBest for: tracks with steady tempo.")
        end
        if ImGui.RadioButton(ctx, "Variable (per bar)", state.tempo_mode == 2) then
            state.tempo_mode = 2
        end
        if ImGui.IsItemHovered(ctx, C("HoveredFlags_ForTooltip")) then
            ImGui.SetTooltip(ctx, "One tempo marker per bar with local BPM.\nBest for: live recordings, rubato, free-time.")
        end
        ImGui.Unindent(ctx, 20)
    end

    ImGui.Spacing(ctx)

    -- Radio: Stretch Markers
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
        ImGui.Unindent(ctx, 20)
    end

    ImGui.Spacing(ctx)

    -- Radio: Match & Quantize
    if ImGui.RadioButton(ctx, "Match & Quantize", state.action_mode == 4) then
        state.action_mode = 4
    end
    if ImGui.IsItemHovered(ctx, C("HoveredFlags_ForTooltip")) then
        ImGui.SetTooltip(ctx, "Two steps in one click:\n1. Insert variable tempo map (aligns REAPER grid to audio)\n2. Insert stretch markers quantized to that grid\n\nResult: tight timing with minimal stretching.")
    end

    if state.action_mode == 4 then
        ImGui.Indent(ctx, 20)
        if ImGui.RadioButton(ctx, string.format("Every beat (%d markers)", #state.beats), state.marker_mode == 1) then
            state.marker_mode = 1
        end
        if ImGui.RadioButton(ctx, string.format("Downbeats only (%d markers)", #state.downbeats), state.marker_mode == 2) then
            state.marker_mode = 2
        end
        ImGui.PushStyleColor(ctx, C("Col_Text"), c.text_dim)
        ImGui.Text(ctx, "Inserts tempo map first, then quantizes markers to it.")
        ImGui.PopStyleColor(ctx, 1)
        ImGui.Unindent(ctx, 20)
    end

    ImGui.Spacing(ctx)

    -- Radio: Match Tempo
    if ImGui.RadioButton(ctx, "Match Tempo", state.action_mode == 3) then
        state.action_mode = 3
    end
    if ImGui.IsItemHovered(ctx, C("HoveredFlags_ForTooltip")) then
        ImGui.SetTooltip(ctx, "Change item playback rate to match a target BPM.\nPitch is preserved (elastique). Original file unchanged.")
    end

    if state.action_mode == 3 then
        ImGui.Indent(ctx, 20)

        -- "Match to project" button
        local project_bpm = callbacks.get_project_bpm and callbacks.get_project_bpm() or 120
        local n_btn = theme.push_button(ctx, ImGui, C, "detect")
        if ImGui.Button(ctx, string.format("Match to project (%.1f BPM)", project_bpm), 0, 24) then
            state.target_bpm = project_bpm
        end
        ImGui.PopStyleColor(ctx, n_btn)
        if ImGui.IsItemHovered(ctx, C("HoveredFlags_ForTooltip")) then
            ImGui.SetTooltip(ctx, "Set target BPM to current REAPER project tempo.")
        end

        -- Custom BPM input
        ImGui.Text(ctx, "Target BPM:")
        ImGui.SameLine(ctx)
        ImGui.SetNextItemWidth(ctx, 80)
        local changed, new_val = ImGui.InputDouble(ctx, "##target_bpm", state.target_bpm or project_bpm, 0, 0, "%.1f")
        if changed then
            state.target_bpm = new_val
        end

        -- Show what will happen
        if state.target_bpm and state.tempo > 0 then
            local rate = state.target_bpm / state.tempo
            ImGui.PushStyleColor(ctx, C("Col_Text"), c.text_dim)
            ImGui.Text(ctx, string.format("%.1f BPM -> %.1f BPM (%.2fx speed, pitch preserved)",
                state.tempo, state.target_bpm, rate))
            ImGui.PopStyleColor(ctx, 1)
        end

        ImGui.Unindent(ctx, 20)
    end
end

function draw_apply(ctx, ImGui, C, state, w, callbacks)
    local n = theme.push_button(ctx, ImGui, C, "apply")
    if ImGui.Button(ctx, "Apply  (Enter)", w, 32) then
        callbacks.on_apply()
    end
    ImGui.PopStyleColor(ctx, n)
    if ImGui.IsItemHovered(ctx, C("HoveredFlags_ForTooltip")) then
        local tip = state.action_mode == 1
            and "Insert tempo markers into the project. Ctrl+Z to undo."
            or "Insert stretch markers into the media item. Ctrl+Z to undo."
        ImGui.SetTooltip(ctx, tip)
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
