-- ReaBeat Actions: REAPER API calls for tempo map, stretch markers, match tempo.
-- All actions wrapped in undo blocks. Warns before destructive operations.

local actions = {}

--- Insert stretch markers at detected beat positions.
-- @param quantize_to_grid If true, snaps markers to nearest beat in REAPER's project grid
-- @param stretch_flag Stretch algorithm: 1=balanced, 2=tonal, 4=transient (nil=no change)
-- @return Number of markers inserted, or 0 if cancelled
function actions.insert_stretch_markers(take, beat_times, item, quantize_to_grid, stretch_flag)
    if not take or not beat_times or #beat_times == 0 then return 0 end

    -- Warn if existing stretch markers will be replaced
    local existing = reaper.GetTakeNumStretchMarkers(take)
    if existing > 0 then
        local ok = reaper.ShowMessageBox(
            string.format(
                "Item has %d existing stretch markers.\n\n" ..
                "ReaBeat will REPLACE them with %d new markers.\n" ..
                "Use Ctrl+Z to undo if needed.\n\nContinue?",
                existing, #beat_times),
            "ReaBeat - Stretch Markers", 1)
        if ok ~= 1 then return 0 end
    end

    reaper.Undo_BeginBlock()
    reaper.PreventUIRefresh(1)

    -- Clear existing stretch markers
    if existing > 0 then
        reaper.DeleteTakeStretchMarkers(take, 0, existing)
    end

    local count = 0
    local take_offset = reaper.GetMediaItemTakeInfo_Value(take, "D_STARTOFFS")
    local playrate = reaper.GetMediaItemTakeInfo_Value(take, "D_PLAYRATE")

    -- Build marker pairs: source position (where audio IS) and target
    -- position (where we WANT it to play). Quantize mode snaps each beat
    -- to the nearest beat in REAPER's project grid (TimeMap2 per-bar).
    local item_pos = reaper.GetMediaItemInfo_Value(item, "D_POSITION")
    local markers = {}  -- {src=source_pos, dst=target_pos}
    if quantize_to_grid then
        for _, bt in ipairs(beat_times) do
            -- Where this beat plays on the timeline
            local timeline = item_pos + (bt) / playrate

            -- Snap to nearest beat in REAPER's grid (per-bar, no drift)
            local _, measures = reaper.TimeMap2_timeToBeats(0, timeline)
            local bar_start = reaper.TimeMap2_beatsToTime(0, 0, measures)
            local bar_end = reaper.TimeMap2_beatsToTime(0, 0, measures + 1)
            local bar_dur = bar_end - bar_start
            local _, grid_ts_num = reaper.TimeMap_GetTimeSigAtTime(0, timeline)
            if grid_ts_num == 0 then grid_ts_num = 4 end

            local pos_in_bar = timeline - bar_start
            local beat_in_bar = pos_in_bar / bar_dur * grid_ts_num
            local nearest = math.max(0, math.min(grid_ts_num, math.floor(beat_in_bar + 0.5)))
            local grid_time = bar_start + (nearest / grid_ts_num) * bar_dur

            local delta = grid_time - timeline
            -- Skip if beat is closer to a DIFFERENT grid beat than the nearest
            -- (more than half a beat interval away = wrong beat entirely)
            local half_beat = bar_dur / grid_ts_num / 2
            if math.abs(delta) >= half_beat then
                grid_time = timeline
            end

            -- Target source position for this grid time
            local dst = (grid_time - item_pos) * playrate + take_offset
            markers[#markers + 1] = { src = bt + take_offset, dst = dst }
        end
    else
        for _, bt in ipairs(beat_times) do
            markers[#markers + 1] = { src = bt + take_offset, dst = bt + take_offset }
        end
    end

    -- Insert stretch markers with both source and target positions.
    -- srcpos = where the audio transient IS in source media
    -- pos = source position that plays at grid_time: (grid_time - item_pos) * playrate
    -- Because: timeline = item_pos + pos / playrate → pos = (timeline - item_pos) * playrate
    for _, m in ipairs(markers) do
        local idx = reaper.SetTakeStretchMarker(take, -1, m.dst, m.src)
        if idx >= 0 then
            count = count + 1
        end
    end

    -- Apply stretch algorithm override if specified
    if stretch_flag and count > 0 then
        reaper.SetMediaItemTakeInfo_Value(take, "I_STRETCHFLAGS", stretch_flag)
    end

    reaper.UpdateItemInProject(item)
    reaper.PreventUIRefresh(-1)

    local label = quantize_to_grid
        and string.format("ReaBeat: Insert %d stretch markers (quantized)", count)
        or string.format("ReaBeat: Insert %d stretch markers", count)
    reaper.Undo_EndBlock(label, -1)

    return count
end

--- Get current project BPM.
-- @return number BPM
function actions.get_project_bpm()
    local bpm, bpi = reaper.GetProjectTimeSignature2(0)
    return bpm
end

--- Match item tempo to target BPM by adjusting playrate.
-- Preserves pitch using REAPER's elastique engine.
-- Optionally aligns first downbeat to nearest bar boundary.
-- @param first_downbeat_src First downbeat position in source (seconds), or nil to skip alignment
-- @return boolean success
function actions.match_tempo(take, item, detected_bpm, target_bpm, first_downbeat_src)
    if not take or not item then return false end
    if detected_bpm <= 0 or target_bpm <= 0 then return false end

    local rate = target_bpm / detected_bpm

    if rate < 0.25 or rate > 4.0 then
        reaper.ShowMessageBox(
            string.format(
                "Tempo ratio too extreme: %.1f BPM -> %.1f BPM (%.1fx)\n\n" ..
                "Supported range: 0.25x to 4.0x",
                detected_bpm, target_bpm, rate),
            "ReaBeat - Match Tempo", 0)
        return false
    end

    reaper.Undo_BeginBlock()
    reaper.PreventUIRefresh(1)

    reaper.SetMediaItemTakeInfo_Value(take, "D_PLAYRATE", rate)
    reaper.SetMediaItemTakeInfo_Value(take, "B_PPITCH", 1)

    local source = reaper.GetMediaItemTake_Source(take)
    if source then
        local source_len = reaper.GetMediaSourceLength(source)
        local offset = reaper.GetMediaItemTakeInfo_Value(take, "D_STARTOFFS")
        local new_len = (source_len - offset) / rate
        reaper.SetMediaItemInfo_Value(item, "D_LENGTH", new_len)
    end

    -- Auto-align: shift item so first downbeat lands on nearest bar
    if first_downbeat_src then
        local item_pos = reaper.GetMediaItemInfo_Value(item, "D_POSITION")
        local take_offset = reaper.GetMediaItemTakeInfo_Value(take, "D_STARTOFFS")
        -- Where the first downbeat plays on the timeline after rate change
        local downbeat_timeline = item_pos + (first_downbeat_src - take_offset) / rate
        -- Find nearest bar boundary
        local _, measures = reaper.TimeMap2_timeToBeats(0, downbeat_timeline)
        local nearest_bar_time = reaper.TimeMap2_beatsToTime(0, 0, measures)
        -- Also check next bar — pick whichever is closer
        local next_bar_time = reaper.TimeMap2_beatsToTime(0, 0, measures + 1)
        local snap_to
        if math.abs(downbeat_timeline - nearest_bar_time) <= math.abs(downbeat_timeline - next_bar_time) then
            snap_to = nearest_bar_time
        else
            snap_to = next_bar_time
        end
        -- Shift item so downbeat lands on the bar
        local shift = snap_to - downbeat_timeline
        reaper.SetMediaItemInfo_Value(item, "D_POSITION", item_pos + shift)
    end

    reaper.UpdateItemInProject(item)
    reaper.PreventUIRefresh(-1)

    local label = first_downbeat_src
        and string.format("ReaBeat: Match tempo %.1f -> %.1f BPM (aligned)", detected_bpm, target_bpm)
        or string.format("ReaBeat: Match tempo %.1f -> %.1f BPM", detected_bpm, target_bpm)
    reaper.Undo_EndBlock(label, -1)

    return true
end

return actions
