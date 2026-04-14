-- ReaBeat Server Launcher: auto-launch Python backend
-- Cross-platform: macOS, Windows, Linux

local server = {}

local PORT = 9877
local IDLE_TIMEOUT_SEC = 300
local MAX_WAIT_SEC = 30

-- Detect OS once
local OS = reaper.GetOS()
local IS_WIN = OS:match("Win") ~= nil
local IS_MAC = OS:match("OSX") ~= nil or OS:match("macOS") ~= nil
local SEP = IS_WIN and "\\" or "/"
local NULL_DEV = IS_WIN and "NUL" or "/dev/null"

-- Temp directory
local function get_temp_dir()
    if IS_WIN then
        return os.getenv("TEMP") or os.getenv("TMP") or "C:\\Temp"
    end
    return "/tmp"
end

local TEMP_DIR = get_temp_dir()
local LOG_FILE = TEMP_DIR .. SEP .. "reabeat_server.log"
local DEBUG_FILE = TEMP_DIR .. SEP .. "reabeat_launch_debug.log"

-- Platform-specific search paths for Python/uv
local function get_search_paths()
    local home = os.getenv("HOME") or os.getenv("USERPROFILE") or ""
    if IS_WIN then
        return {
            home .. "\\.local\\bin",
            home .. "\\AppData\\Local\\uv\\bin",
            home .. "\\AppData\\Local\\Programs\\Python\\Python312",
            home .. "\\AppData\\Local\\Programs\\Python\\Python311",
            home .. "\\AppData\\Local\\Programs\\Python\\Python310",
            "C:\\Python312", "C:\\Python311", "C:\\Python310",
            home .. "\\scoop\\shims",
            home .. "\\miniconda3", home .. "\\miniforge3",
            home .. "\\anaconda3",
        }
    else
        return {
            home .. "/.local/bin",
            "/opt/homebrew/bin",
            "/usr/local/bin",
            "/usr/bin",
            home .. "/miniconda3/bin", home .. "/miniforge3/bin",
            home .. "/anaconda3/bin",
        }
    end
end

local SEARCH_PATHS = get_search_paths()

local state = {
    project_root = nil,
    script_dir = nil,
    launch_attempted = false,
    launch_error = nil,
    launching = false,
    launch_start_time = 0,
}

function server.set_script_dir(dir)
    state.script_dir = dir
end

-- Find project root by looking for pyproject.toml
local function find_project_root()
    local script_path = state.script_dir
        or debug.getinfo(2, "S").source:match("@?(.*[/\\])")
        or debug.getinfo(1, "S").source:match("@?(.*[/\\])")
        or ""

    -- Home directory for well-known install locations
    local home = os.getenv("HOME") or os.getenv("USERPROFILE") or ""

    local candidates = {
        -- Running from cloned repo: scripts/reaper/../../ = project root
        script_path .. ".." .. SEP .. ".." .. SEP,
        script_path .. ".." .. SEP .. ".." .. SEP .. ".." .. SEP,
        script_path,
        -- Well-known install locations (installer puts repo here)
        home .. SEP .. "ReaBeat" .. SEP,
        home .. SEP .. "Documents" .. SEP .. "ReaBeat" .. SEP,
    }
    for _, dir in ipairs(candidates) do
        local f = io.open(dir .. "pyproject.toml", "r")
        if f then
            f:close()
            return dir
        end
    end
    return nil
end

-- Find executable in known paths
local function find_command(name)
    local ext = IS_WIN and ".exe" or ""
    -- Search hardcoded paths first (GUI apps don't inherit terminal PATH)
    for _, dir in ipairs(SEARCH_PATHS) do
        local path = dir .. SEP .. name .. ext
        local f = io.open(path, "r")
        if f then
            f:close()
            return path
        end
    end
    -- Fallback: which/where
    local which = IS_WIN and "where" or "which"
    local handle = io.popen(which .. " " .. name .. " 2>" .. NULL_DEV)
    if handle then
        local result = handle:read("*l")
        handle:close()
        if result and result ~= "" and not result:match("not found") then
            return result:gsub("%s+$", "")
        end
    end
    return nil
end

-- Load socket lib with mavriq-lua-sockets paths (same as reabeat_socket.lua)
local _socket_lib = nil
local function get_socket_for_port_check()
    if _socket_lib then return _socket_lib end
    local ok, lib = pcall(require, "socket")
    if ok and lib then _socket_lib = lib; return lib end
    -- Add ReaPack socket paths and retry
    local resource_path = reaper.GetResourcePath()
    local extension = IS_WIN and "dll" or "so"
    local paths = {
        resource_path .. SEP .. "Scripts" .. SEP .. "Mavriq ReaScript Repository" .. SEP ..
            "Various" .. SEP .. "Mavriq-Lua-Sockets" .. SEP,
        resource_path .. SEP .. "Scripts" .. SEP .. "mavriq-lua-sockets" .. SEP,
        resource_path .. SEP .. "UserPlugins" .. SEP,
    }
    for _, path in ipairs(paths) do
        package.cpath = package.cpath .. ";" .. path .. "?." .. extension
        package.cpath = package.cpath .. ";" .. path .. "socket" .. SEP .. "?." .. extension
        package.path = package.path .. ";" .. path .. "?.lua"
    end
    ok, lib = pcall(require, "socket")
    if ok and lib then _socket_lib = lib; return lib end
    ok, lib = pcall(require, "socket.core")
    if ok and lib then _socket_lib = lib; return lib end
    return nil
end

-- Check if port is open via TCP connect (no os.execute — avoids visible console windows)
local function is_port_open()
    local lib = get_socket_for_port_check()
    if not lib then return false end
    local tcp = lib.tcp()
    tcp:settimeout(0.3)
    local connected = tcp:connect("127.0.0.1", PORT)
    tcp:close()
    return connected == 1
end

function server.is_running()
    local ok, result = pcall(is_port_open)
    return ok and result
end

function server.launch()
    if server.is_running() then
        state.launching = false
        return "running"
    end
    if state.launching then return "launching" end
    if state.launch_attempted and state.launch_error then
        return "error"
    end

    if not state.project_root then
        state.project_root = find_project_root()
    end
    if not state.project_root then
        state.launch_error = "Could not find project root (pyproject.toml)"
        return "error"
    end

    -- Build launch command (platform-specific)
    local cmd
    local uv_path = find_command("uv")

    if IS_WIN then
        -- Windows: write a temp .bat file to avoid nested-quote issues
        -- (cmd /C "cd /D "path" && ..." breaks when paths contain spaces)
        local bat_file = TEMP_DIR .. SEP .. "reabeat_launch.bat"
        local runner = nil
        if uv_path then
            runner = string.format('"%s" run python', uv_path)
        else
            local python_path = find_command("python") or find_command("python3")
            if python_path then
                runner = string.format('"%s"', python_path)
            end
        end
        if runner then
            local bat = io.open(bat_file, "w")
            if bat then
                -- Prevent Intel Fortran Runtime (PyTorch MKL) from crashing
                -- on CTRL_CLOSE_EVENT when the launching console is destroyed
                bat:write('@SET FOR_DISABLE_CONSOLE_CTRL_HANDLER=1\r\n')
                bat:write(string.format('@cd /D "%s"\r\n', state.project_root))
                bat:write(string.format('%s -m reabeat serve --port %d --idle-timeout %d > "%s" 2>&1\r\n',
                    runner, PORT, IDLE_TIMEOUT_SEC, LOG_FILE))
                bat:close()
                -- Launch via wscript to get a persistent hidden console.
                -- start /B shares the parent's transient console which is
                -- destroyed when os.execute() returns, killing uv.exe.
                local vbs_file = TEMP_DIR .. SEP .. "reabeat_launch.vbs"
                local vbs = io.open(vbs_file, "w")
                if vbs then
                    vbs:write('Set WshShell = CreateObject("WScript.Shell")\n')
                    vbs:write('WshShell.Run "cmd /C ""' .. bat_file .. '""", 0, False\n')
                    vbs:close()
                    cmd = string.format('start "" wscript "%s"', vbs_file)
                end
            end
        end
    else
        -- macOS/Linux: export PATH, background with &
        local path_dirs = table.concat(SEARCH_PATHS, ":")
        local path_export = "export PATH=" .. path_dirs .. ":$PATH && "

        if uv_path then
            cmd = string.format(
                '%scd "%s" && "%s" run python -m reabeat serve --port %d --idle-timeout %d > "%s" 2>&1 &',
                path_export, state.project_root, uv_path, PORT, IDLE_TIMEOUT_SEC, LOG_FILE)
        else
            local python_path = find_command("python3") or find_command("python")
            if python_path then
                cmd = string.format(
                    '%scd "%s" && "%s" -m reabeat serve --port %d --idle-timeout %d > "%s" 2>&1 &',
                    path_export, state.project_root, python_path, PORT, IDLE_TIMEOUT_SEC, LOG_FILE)
            end
        end
    end

    if not cmd then
        state.launch_error = "Python not found. Install uv: https://docs.astral.sh/uv/"
        state.launch_attempted = true
        reaper.ShowMessageBox(
            "ReaBeat requires Python 3.10+ with the reabeat package.\n\n" ..
            "Recommended install:\n\n" ..
            (IS_WIN
                and '  powershell -c "irm https://astral.sh/uv/install.ps1 | iex"\n'
                or  '  curl -LsSf https://astral.sh/uv/install.sh | sh\n') ..
            "\nThen:\n" ..
            "  cd " .. (state.project_root or "ReaBeat") .. "\n" ..
            "  uv sync",
            "ReaBeat — Python Not Found", 0)
        return "error"
    end

    -- Debug log
    local log = io.open(DEBUG_FILE, "w")
    if log then
        log:write("os: " .. OS .. "\n")
        log:write("project_root: " .. tostring(state.project_root) .. "\n")
        log:write("cmd: " .. cmd .. "\n")
        log:close()
    end

    os.execute(cmd)
    state.launch_attempted = true
    state.launching = true
    state.launch_start_time = os.clock()
    state.launch_error = nil
    return "launching"
end

function server.poll()
    if not state.launching then
        if server.is_running() then return "running" end
        return "error"
    end
    if server.is_running() then
        state.launching = false
        state.launch_error = nil
        return "running"
    end
    local elapsed = os.clock() - state.launch_start_time
    if elapsed >= MAX_WAIT_SEC then
        state.launching = false
        state.launch_error = string.format(
            "Server did not start within %ds.\nCheck log: %s", MAX_WAIT_SEC, LOG_FILE)
        return "error"
    end
    return "launching"
end

function server.is_launching()
    return state.launching
end

function server.get_error()
    return state.launch_error
end

function server.kill()
    if IS_WIN then
        os.execute(string.format(
            'for /f "tokens=5" %%a in (\'netstat -ano ^| findstr ":%d " ^| findstr LISTENING\') do taskkill /PID %%a /F > %s 2>&1',
            PORT, NULL_DEV))
    else
        os.execute(string.format("kill $(lsof -ti:%d) 2>/dev/null", PORT))
    end
end

return server
