-- REABeat Waveform: source waveform with beat/downbeat overlay
-- Clean, readable visualization even with 500+ beats.

local waveform = {}
local theme = require("reabeat_theme")

function waveform.draw(ctx, ImGui, C, state, x_offset, y_offset, avail_w, target_h)
    if not state.peaks or #state.peaks == 0 then return end

    local c = theme.colors
    local h = 140  -- Fixed height for readability
    local w = avail_w

    -- Reserve space
    local cx, cy = ImGui.GetCursorScreenPos(ctx)
    ImGui.Dummy(ctx, w, h)
    local draw_list = ImGui.GetWindowDrawList(ctx)

    local x = cx
    local y = cy
    local dur = state.audio_duration or 1

    -- Background
    ImGui.DrawList_AddRectFilled(draw_list, x, y, x + w, y + h, c.wave_bg)

    -- Alternating bar shading (every other bar gets subtle highlight)
    if state.downbeats and dur > 0 then
        for i = 1, #state.downbeats - 1, 2 do
            local bx1 = x + (state.downbeats[i] / dur) * w
            local bx2 = x + (state.downbeats[i + 1] / dur) * w
            ImGui.DrawList_AddRectFilled(draw_list, bx1, y, bx2, y + h, c.beat_range_bg)
        end
    end

    -- Draw waveform peaks (BEHIND markers — this is the main visual)
    local n_peaks = #state.peaks
    local mid_y = y + h / 2
    local amp_scale = h * 0.45

    for i = 1, math.min(n_peaks, math.floor(w)) do
        local px = x + (i - 1) * w / n_peaks
        local peak = state.peaks[i] or 0
        local bar_h = peak * amp_scale
        if bar_h > 0.5 then
            ImGui.DrawList_AddLine(draw_list,
                px, mid_y - bar_h, px, mid_y + bar_h,
                c.wave_fill, 1.0)
        end
    end

    -- Beat markers: very subtle short ticks at bottom only
    if state.beats and dur > 0 then
        for _, bt in ipairs(state.beats) do
            local bx = x + (bt / dur) * w
            if bx >= x and bx <= x + w then
                ImGui.DrawList_AddLine(draw_list,
                    bx, y + h - 8, bx, y + h,
                    c.beat_color, 1.0)
            end
        end
    end

    -- Downbeat markers: taller, brighter, from bottom
    if state.downbeats and dur > 0 then
        local n_bars = #state.downbeats
        -- Calculate label interval: show labels at reasonable density
        local label_every = 1
        if n_bars > 120 then label_every = 16
        elseif n_bars > 60 then label_every = 8
        elseif n_bars > 30 then label_every = 4
        elseif n_bars > 16 then label_every = 2
        end

        for i, db in ipairs(state.downbeats) do
            local dx = x + (db / dur) * w
            if dx >= x and dx <= x + w then
                -- Downbeat line (extends higher than beat markers)
                ImGui.DrawList_AddLine(draw_list,
                    dx, y + h * 0.55, dx, y + h,
                    c.downbeat_color, 1.0)

                -- Bar number label (only at intervals)
                if (i - 1) % label_every == 0 then
                    ImGui.DrawList_AddText(draw_list,
                        dx + 2, y + h - 12,
                        c.downbeat_color,
                        tostring(i))
                end
            end
        end
    end

    -- Border
    ImGui.DrawList_AddRect(draw_list, x, y, x + w, y + h, c.border)

    -- Hover: show time + bar info tooltip
    if ImGui.IsItemHovered(ctx) then
        local mx = ImGui.GetMousePos(ctx)
        if dur > 0 then
            local frac = math.max(0, math.min(1, (mx - x) / w))
            local t = frac * dur

            -- Find which bar we're in
            local bar_num = 0
            if state.downbeats then
                for i, db in ipairs(state.downbeats) do
                    if db <= t then bar_num = i end
                end
            end

            local tip = string.format("%d:%04.1f", math.floor(t / 60), t % 60)
            if bar_num > 0 then
                tip = tip .. string.format("  |  Bar %d", bar_num)
            end
            ImGui.SetTooltip(ctx, tip)

            -- Hover line
            ImGui.DrawList_AddLine(draw_list, mx, y, mx, y + h, 0xFFFFFF33, 1.0)
        end
    end
end

return waveform
