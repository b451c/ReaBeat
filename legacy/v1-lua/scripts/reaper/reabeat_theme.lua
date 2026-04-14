-- ReaBeat Theme: REAPER-native dark theme with warm gold accent
-- Designed to feel like a built-in REAPER tool, not a foreign plugin.

local theme = {}

-- Color palette — REAPER-native dark with subtle warmth
theme.colors = {
    -- Backgrounds (REAPER-like greys, not navy)
    window_bg    = 0x1E1E1EFF,
    card_bg      = 0x282828FF,
    frame_bg     = 0x333333FF,
    frame_hover  = 0x3D3D3DFF,
    frame_active = 0x484848FF,
    border       = 0x3D3D3D80,
    separator    = 0x3D3D3D60,

    -- Status
    success = 0x5CB85CFF,
    error   = 0xD94848FF,
    warning = 0xE8A838FF,

    -- Accent (warm gold — premium, distinctive)
    accent        = 0xC8A040FF,
    accent_hover  = 0xDDB850FF,
    accent_active = 0xAA8830FF,

    -- Text hierarchy
    text_bright = 0xE8E8E8FF,
    text_normal = 0xBBBBBBFF,
    text_dim    = 0x808080FF,
    disabled    = 0x505050FF,

    -- Beats
    beat_color     = 0xC8A04033,  -- Gold, very subtle (just ticks)
    downbeat_color = 0xC8A04088,  -- Gold, visible but not overwhelming
    beat_range_bg  = 0xC8A04010,  -- Barely visible bar shading

    -- Waveform
    wave_fill = 0x90B0D0CC,  -- Bright blue-grey (dominant visual)
    wave_bg   = 0x141414FF,  -- Dark background for contrast

    -- Buttons
    btn_detect        = 0xC8A040FF,
    btn_detect_hover  = 0xDDB850FF,
    btn_detect_active = 0xAA8830FF,
    btn_apply         = 0x4A8C4AFF,
    btn_apply_hover   = 0x5CB85CFF,
    btn_apply_active  = 0x3A7A3AFF,
    btn_neutral        = 0x383838FF,
    btn_neutral_hover  = 0x454545FF,
    btn_neutral_active = 0x525252FF,

    -- Progress
    progress_bg   = 0x333333FF,
    progress_fill = 0xC8A040FF,
}

function theme.push(ctx, ImGui, C)
    local c = theme.colors
    local n = 0

    -- Window
    ImGui.PushStyleColor(ctx, C("Col_WindowBg"),     c.window_bg);     n = n + 1
    ImGui.PushStyleColor(ctx, C("Col_ChildBg"),      c.card_bg);       n = n + 1
    ImGui.PushStyleColor(ctx, C("Col_PopupBg"),      0x252525FF);      n = n + 1
    ImGui.PushStyleColor(ctx, C("Col_Border"),       c.border);        n = n + 1
    ImGui.PushStyleColor(ctx, C("Col_BorderShadow"), 0x00000000);      n = n + 1

    -- Frame
    ImGui.PushStyleColor(ctx, C("Col_FrameBg"),        c.frame_bg);    n = n + 1
    ImGui.PushStyleColor(ctx, C("Col_FrameBgHovered"), c.frame_hover); n = n + 1
    ImGui.PushStyleColor(ctx, C("Col_FrameBgActive"),  c.frame_active);n = n + 1

    -- Title
    ImGui.PushStyleColor(ctx, C("Col_TitleBg"),          0x1A1A1AFF);  n = n + 1
    ImGui.PushStyleColor(ctx, C("Col_TitleBgActive"),    0x252525FF);  n = n + 1
    ImGui.PushStyleColor(ctx, C("Col_TitleBgCollapsed"), 0x141414FF);  n = n + 1

    -- Text
    ImGui.PushStyleColor(ctx, C("Col_Text"),         c.text_bright);   n = n + 1
    ImGui.PushStyleColor(ctx, C("Col_TextDisabled"), c.text_dim);      n = n + 1

    -- Buttons (neutral default)
    ImGui.PushStyleColor(ctx, C("Col_Button"),        c.btn_neutral);       n = n + 1
    ImGui.PushStyleColor(ctx, C("Col_ButtonHovered"), c.btn_neutral_hover); n = n + 1
    ImGui.PushStyleColor(ctx, C("Col_ButtonActive"),  c.btn_neutral_active);n = n + 1

    -- Header
    ImGui.PushStyleColor(ctx, C("Col_Header"),        0x383838FF);     n = n + 1
    ImGui.PushStyleColor(ctx, C("Col_HeaderHovered"), 0x454545FF);     n = n + 1
    ImGui.PushStyleColor(ctx, C("Col_HeaderActive"),  0x525252FF);     n = n + 1

    -- Scrollbar
    ImGui.PushStyleColor(ctx, C("Col_ScrollbarBg"),          0x1A1A1AFF);  n = n + 1
    ImGui.PushStyleColor(ctx, C("Col_ScrollbarGrab"),        0x383838FF);  n = n + 1
    ImGui.PushStyleColor(ctx, C("Col_ScrollbarGrabHovered"), 0x454545FF);  n = n + 1
    ImGui.PushStyleColor(ctx, C("Col_ScrollbarGrabActive"),  0x525252FF);  n = n + 1

    -- Slider
    ImGui.PushStyleColor(ctx, C("Col_SliderGrab"),       0xC8A040CC);  n = n + 1
    ImGui.PushStyleColor(ctx, C("Col_SliderGrabActive"), 0xDDB850FF);  n = n + 1

    -- Checkmark & separator
    ImGui.PushStyleColor(ctx, C("Col_CheckMark"),         c.accent);      n = n + 1
    ImGui.PushStyleColor(ctx, C("Col_Separator"),         c.separator);   n = n + 1
    ImGui.PushStyleColor(ctx, C("Col_SeparatorHovered"),  0xC8A04080);    n = n + 1
    ImGui.PushStyleColor(ctx, C("Col_SeparatorActive"),   0xC8A040CC);    n = n + 1

    -- Resize grip
    ImGui.PushStyleColor(ctx, C("Col_ResizeGrip"),        0x33333380);  n = n + 1
    ImGui.PushStyleColor(ctx, C("Col_ResizeGripHovered"), 0xC8A04060);  n = n + 1
    ImGui.PushStyleColor(ctx, C("Col_ResizeGripActive"),  0xC8A040AA);  n = n + 1

    -- Tab
    ImGui.PushStyleColor(ctx, C("Col_Tab"),                0x282828FF);  n = n + 1
    ImGui.PushStyleColor(ctx, C("Col_TabHovered"),         0x454545FF);  n = n + 1

    -- Style vars
    local v = 0
    ImGui.PushStyleVar(ctx, C("StyleVar_WindowPadding"),     14, 12);  v = v + 1
    ImGui.PushStyleVar(ctx, C("StyleVar_FramePadding"),      8, 5);    v = v + 1
    ImGui.PushStyleVar(ctx, C("StyleVar_ItemSpacing"),       8, 6);    v = v + 1
    ImGui.PushStyleVar(ctx, C("StyleVar_FrameRounding"),     4);       v = v + 1
    ImGui.PushStyleVar(ctx, C("StyleVar_ChildRounding"),     5);       v = v + 1
    ImGui.PushStyleVar(ctx, C("StyleVar_WindowRounding"),    6);       v = v + 1
    ImGui.PushStyleVar(ctx, C("StyleVar_ScrollbarRounding"), 3);       v = v + 1
    ImGui.PushStyleVar(ctx, C("StyleVar_GrabRounding"),      3);       v = v + 1
    ImGui.PushStyleVar(ctx, C("StyleVar_DisabledAlpha"),     0.35);    v = v + 1

    return n, v
end

function theme.pop(ctx, ImGui, n_colors, n_vars)
    ImGui.PopStyleColor(ctx, n_colors)
    ImGui.PopStyleVar(ctx, n_vars)
end

-- Helper: push colored button style and return pop count
function theme.push_button(ctx, ImGui, C, color_name)
    local c = theme.colors
    if color_name == "detect" then
        ImGui.PushStyleColor(ctx, C("Col_Button"),        c.btn_detect)
        ImGui.PushStyleColor(ctx, C("Col_ButtonHovered"), c.btn_detect_hover)
        ImGui.PushStyleColor(ctx, C("Col_ButtonActive"),  c.btn_detect_active)
    elseif color_name == "apply" then
        ImGui.PushStyleColor(ctx, C("Col_Button"),        c.btn_apply)
        ImGui.PushStyleColor(ctx, C("Col_ButtonHovered"), c.btn_apply_hover)
        ImGui.PushStyleColor(ctx, C("Col_ButtonActive"),  c.btn_apply_active)
    else
        return 0
    end
    return 3
end

return theme
