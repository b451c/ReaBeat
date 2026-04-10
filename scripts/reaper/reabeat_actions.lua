-- ReaBeat Actions: REAPER API calls for tempo map, stretch markers, match tempo.
-- All actions wrapped in undo blocks. Warns before destructive operations.

local actions = {}

--- Insert tempo/time signature markers at detected bar positions.
-- Snaps first beat to nearest bar boundary for grid alignment.
-- @return Number of markers inserted, or 0 if cancelled
function actions.insert_tempo_map(beats, downbeats, tempo, ts_num, ts_denom, item, variable)
    if not downbeats or #downbeats == 0 then return 0 end

    -- Warn if existing tempo markers will be affected
    local existing = reaper.CountTempoTimeSigMarkers(0)
    if existing > 1 then
        local ok = reaper.ShowMessageBox(
            string.format(
                "Project has %d existing tempo markers.\n\n" ..
                "ReaBeat will ADD new markers (existing ones stay).\n" ..
                "Use Ctrl+Z to undo if needed.\n\nContinue?",
                existing),
            "ReaBeat - Tempo Map", 1)
        if ok ~= 1 then return 0 end
    end

    local item_pos = reaper.GetMediaItemInfo_Value(item, "D_POSITION")
    item_pos = math.max(0, item_pos)

    -- Snap first downbeat to nearest bar line using TimeMap2 (grid-independent).
    -- Unlike BR_GetClosestGridDivision, this always snaps to a bar boundary
    -- regardless of the user's grid resolution setting.
    local first_beat_time = item_pos + downbeats[1]
    local _, measures = reaper.TimeMap2_timeToBeats(0, first_beat_time)
    local bar_time = reaper.TimeMap2_beatsToTime(0, 0, measures)
    local next_bar_time = reaper.TimeMap2_beatsToTime(0, 0, measures + 1)
    local snapped_time
    if math.abs(first_beat_time - bar_time) <= math.abs(first_beat_time - next_bar_time) then
        snapped_time = bar_time
    else
        snapped_time = next_bar_time
    end
    local snap_offset = snapped_time - first_beat_time

    reaper.Undo_BeginBlock()
    reaper.PreventUIRefresh(1)

    local count = 0

    if not variable then
        -- Constant tempo: single marker at snapped bar position
        reaper.SetTempoTimeSigMarker(0, -1,
            snapped_time, -1, -1,
            tempo,
            math.floor(ts_num),
            math.floor(ts_denom),
            false)
        count = 1
    else
        -- Variable tempo: one marker per bar, all shifted by snap_offset
        for i = 1, #downbeats do
            local bar_time = item_pos + downbeats[i] + snap_offset
            local bar_bpm = tempo

            if i < #downbeats then
                local bar_duration = downbeats[i + 1] - downbeats[i]
                if bar_duration > 0 then
                    bar_bpm = (ts_num / bar_duration) * 60.0
                    bar_bpm = math.max(30, math.min(300, bar_bpm))
                end
            end

            local set_ts = (i == 1)
            reaper.SetTempoTimeSigMarker(0, -1,
                bar_time, -1, -1,
                bar_bpm,
                set_ts and math.floor(ts_num) or 0,
                set_ts and math.floor(ts_denom) or 0,
                true)
            count = count + 1
        end
    end

    reaper.UpdateTimeline()
    reaper.PreventUIRefresh(-1)

    local label = variable
        and string.format("ReaBeat: Insert variable tempo map (%d markers)", count)
        or string.format("ReaBeat: Insert constant tempo (%.1f BPM)", tempo)
    reaper.Undo_EndBlock(label, -1)

    return count
end

--- Insert stretch markers at detected beat positions.
-- @param quantize_to_grid If true, snaps markers to REAPER grid after insertion
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

    local item_pos = reaper.GetMediaItemInfo_Value(item, "D_POSITION")

    reaper.Undo_BeginBlock()
    reaper.PreventUIRefresh(1)

    -- Clear existing stretch markers
    if existing > 0 then
        reaper.DeleteTakeStretchMarkers(take, 0, existing)
    end

    local count = 0
    local take_offset = reaper.GetMediaItemTakeInfo_Value(take, "D_STARTOFFS")

    for _, bt in ipairs(beat_times) do
        local pos = bt + take_offset
        local idx = reaper.SetTakeStretchMarker(take, -1, pos)
        if idx >= 0 then
            count = count + 1
        end
    end

    -- Quantize: snap each stretch marker to nearest beat (quarter note).
    -- Uses TimeMap2 to resolve beat positions from the tempo map,
    -- independent of REAPER's grid resolution setting.
    if quantize_to_grid and count > 0 then
        local n_markers = reaper.GetTakeNumStretchMarkers(take)
        for i = 0, n_markers - 1 do
            local _, pos = reaper.GetTakeStretchMarker(take, i)
            -- Convert marker position (take-relative) to project time
            local project_time = item_pos + (pos - take_offset)
            -- Snap to nearest beat using tempo map (not grid setting)
            local _, _, _, fullbeats = reaper.TimeMap2_timeToBeats(0, project_time)
            local nearest_beat = math.floor(fullbeats + 0.5)
            local grid_time = reaper.TimeMap2_beatsToTime(0, nearest_beat)
            -- Convert back to take-relative position
            local snapped_pos = (grid_time - item_pos) + take_offset
            if math.abs(snapped_pos - pos) > 0.001 then
                reaper.SetTakeStretchMarker(take, i, snapped_pos)
            end
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
