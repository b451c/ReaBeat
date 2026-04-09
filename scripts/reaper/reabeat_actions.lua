-- REABeat Actions: REAPER API calls for tempo map and stretch markers
-- All actions wrapped in undo blocks. Warns before destructive operations.

local actions = {}

--- Insert tempo/time signature markers at detected bar positions.
-- @return Number of markers inserted, or 0 if cancelled
function actions.insert_tempo_map(beats, downbeats, tempo, ts_num, ts_denom, item, variable)
    if not downbeats or #downbeats == 0 then return 0 end

    -- Warn if existing tempo markers will be affected
    local existing = reaper.CountTempoTimeSigMarkers(0)
    if existing > 1 then
        local ok = reaper.ShowMessageBox(
            string.format(
                "Project has %d existing tempo markers.\n\n" ..
                "REABeat will ADD new markers (existing ones stay).\n" ..
                "Use Ctrl+Z to undo if needed.\n\nContinue?",
                existing),
            "REABeat — Tempo Map", 1)
        if ok ~= 1 then return 0 end
    end

    local item_pos = reaper.GetMediaItemInfo_Value(item, "D_POSITION")
    item_pos = math.max(0, item_pos)  -- Clamp negative positions

    reaper.Undo_BeginBlock()
    reaper.PreventUIRefresh(1)

    local count = 0

    if not variable then
        -- Constant tempo: single marker at item start
        reaper.SetTempoTimeSigMarker(0, -1,
            item_pos, -1, -1,
            tempo,
            math.floor(ts_num),
            math.floor(ts_denom),
            false)
        count = 1
    else
        -- Variable tempo: one marker per bar
        for i = 1, #downbeats do
            local bar_time = item_pos + downbeats[i]
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
        and string.format("REABeat: Insert variable tempo map (%d markers)", count)
        or string.format("REABeat: Insert constant tempo (%.1f BPM)", tempo)
    reaper.Undo_EndBlock(label, -1)

    return count
end

--- Insert stretch markers at detected beat positions.
-- @return Number of markers inserted, or 0 if cancelled
function actions.insert_stretch_markers(take, beat_times, item)
    if not take or not beat_times or #beat_times == 0 then return 0 end

    -- Warn if existing stretch markers will be replaced
    local existing = reaper.GetTakeNumStretchMarkers(take)
    if existing > 0 then
        local ok = reaper.ShowMessageBox(
            string.format(
                "Item has %d existing stretch markers.\n\n" ..
                "REABeat will REPLACE them with %d new markers.\n" ..
                "Use Ctrl+Z to undo if needed.\n\nContinue?",
                existing, #beat_times),
            "REABeat — Stretch Markers", 1)
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

    for _, bt in ipairs(beat_times) do
        local pos = bt + take_offset
        local idx = reaper.SetTakeStretchMarker(take, -1, pos)
        if idx >= 0 then
            count = count + 1
        end
    end

    reaper.UpdateItemInProject(item)
    reaper.PreventUIRefresh(-1)
    reaper.Undo_EndBlock(
        string.format("REABeat: Insert %d stretch markers", count), -1)

    return count
end

return actions
