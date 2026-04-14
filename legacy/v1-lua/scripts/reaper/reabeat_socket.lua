-- ReaBeat Socket Client: TCP communication with Python backend
-- Line-delimited JSON on localhost:9877

local socket_client = {}

local PORT = 9877
local HOST = "127.0.0.1"

local socket = nil
local connected = false
local recv_buffer = ""

-- Load socket library (proven paths from REAmix)
local function get_socket_lib()
    if socket then return socket end

    -- Try direct require first (may already be on path)
    local ok, lib = pcall(require, "socket")
    if ok and lib then
        socket = lib
        return socket
    end

    -- Setup paths for mavriq-lua-sockets (ReaPack installation)
    local resource_path = reaper.GetResourcePath()
    local opsys = reaper.GetOS()
    local extension = opsys:match("Win") and "dll" or "so"

    local socket_paths = {
        resource_path .. "/Scripts/Mavriq ReaScript Repository/Various/Mavriq-Lua-Sockets/",
        resource_path .. "/Scripts/mavriq-lua-sockets/",
        resource_path .. "/UserPlugins/",
        resource_path .. "/Scripts/Daniel Lumertz Scripts/LUA Sockets/socket module/",
    }

    for _, path in ipairs(socket_paths) do
        package.cpath = package.cpath .. ";" .. path .. "?." .. extension
        package.cpath = package.cpath .. ";" .. path .. "socket/?." .. extension
        package.path = package.path .. ";" .. path .. "?.lua"
    end

    ok, lib = pcall(require, "socket")
    if ok and lib then
        socket = lib
        return socket
    end

    -- Fallback: try socket.core directly
    ok, lib = pcall(require, "socket.core")
    if ok and lib then
        socket = lib
        return socket
    end

    return nil
end

function socket_client.connect()
    local lib = get_socket_lib()
    if not lib then return false end
    local tcp = lib.tcp()
    tcp:settimeout(0.5)
    local ok, err = tcp:connect(HOST, PORT)
    if not ok then
        tcp:close()
        return false
    end
    tcp:settimeout(0)  -- Non-blocking for poll
    socket_client._tcp = tcp
    connected = true
    recv_buffer = ""

    -- Verify with ping
    socket_client.send({ cmd = "ping" })
    lib.sleep(0.1)
    local resp = socket_client.poll()
    if resp and resp.status == "ok" then
        return true
    end
    -- Still consider connected even without pong (server may be slow)
    return true
end

function socket_client.send(msg)
    if not connected or not socket_client._tcp then return false end
    local json_str = json_encode(msg) .. "\n"
    local ok, err = socket_client._tcp:send(json_str)
    if not ok then
        connected = false
        return false
    end
    return true
end

function socket_client.poll()
    if not connected or not socket_client._tcp then return nil end
    local data, err, partial = socket_client._tcp:receive("*l")
    if not data and partial and #partial > 0 then
        recv_buffer = recv_buffer .. partial
        return nil
    end
    if not data then
        if err == "timeout" then
            -- Check if buffer has a complete line
            if recv_buffer ~= "" then
                local line_end = recv_buffer:find("\n")
                if line_end then
                    local line = recv_buffer:sub(1, line_end - 1)
                    recv_buffer = recv_buffer:sub(line_end + 1)
                    return json_decode(line)
                end
            end
            return nil
        end
        if err == "closed" then
            connected = false
        end
        return nil
    end
    -- Full line received
    local line = recv_buffer .. data
    recv_buffer = ""
    return json_decode(line)
end

function socket_client.disconnect()
    if socket_client._tcp then
        pcall(function() socket_client._tcp:close() end)
        socket_client._tcp = nil
    end
    connected = false
end

function socket_client.is_connected()
    return connected
end

-- Minimal JSON encoder/decoder
function json_encode(val)
    if type(val) == "nil" then return "null" end
    if type(val) == "boolean" then return val and "true" or "false" end
    if type(val) == "number" then
        if val ~= val then return "null" end  -- NaN
        if val == math.huge or val == -math.huge then return "null" end
        if val == math.floor(val) and math.abs(val) < 2^53 then
            return string.format("%d", val)
        end
        return string.format("%.6g", val)
    end
    if type(val) == "string" then
        return '"' .. val:gsub('\\', '\\\\'):gsub('"', '\\"'):gsub('\n', '\\n'):gsub('\r', '\\r'):gsub('\t', '\\t') .. '"'
    end
    if type(val) == "table" then
        -- Array check
        local is_array = true
        local max_idx = 0
        for k, _ in pairs(val) do
            if type(k) ~= "number" or k ~= math.floor(k) or k < 1 then
                is_array = false
                break
            end
            max_idx = math.max(max_idx, k)
        end
        if is_array and max_idx == #val then
            local parts = {}
            for i = 1, #val do
                parts[i] = json_encode(val[i])
            end
            return "[" .. table.concat(parts, ",") .. "]"
        else
            local parts = {}
            for k, v in pairs(val) do
                local key = type(k) == "number" and tostring(k) or tostring(k)
                parts[#parts + 1] = '"' .. key .. '":' .. json_encode(v)
            end
            return "{" .. table.concat(parts, ",") .. "}"
        end
    end
    return "null"
end

function json_decode(str)
    if not str or str == "" then return nil end
    -- Use REAPER's built-in JSON if available, otherwise simple parse
    local ok, result = pcall(function()
        -- Lua pattern-based JSON parser (handles our simple protocol)
        return json_parse(str, 1)
    end)
    if ok then return result end
    return nil
end

-- Simple recursive JSON parser
function json_parse(str, pos)
    pos = pos or 1
    -- Skip whitespace
    pos = str:match("^%s*()", pos)
    local c = str:sub(pos, pos)

    if c == '"' then
        -- String
        local s, e = str:find('"', pos + 1)
        while s and str:sub(s - 1, s - 1) == '\\' do
            s, e = str:find('"', s + 1)
        end
        local val = str:sub(pos + 1, s - 1)
        val = val:gsub('\\n', '\n'):gsub('\\r', '\r'):gsub('\\t', '\t'):gsub('\\"', '"'):gsub('\\\\', '\\')
        return val, s + 1
    elseif c == '{' then
        -- Object
        local obj = {}
        pos = pos + 1
        pos = str:match("^%s*()", pos)
        if str:sub(pos, pos) == '}' then return obj, pos + 1 end
        while true do
            pos = str:match("^%s*()", pos)
            local key, next_pos = json_parse(str, pos)
            pos = str:match("^%s*:%s*()", next_pos)
            local val
            val, pos = json_parse(str, pos)
            obj[key] = val
            pos = str:match("^%s*()", pos)
            if str:sub(pos, pos) == '}' then return obj, pos + 1 end
            pos = str:match("^%s*,%s*()", pos)
        end
    elseif c == '[' then
        -- Array
        local arr = {}
        pos = pos + 1
        pos = str:match("^%s*()", pos)
        if str:sub(pos, pos) == ']' then return arr, pos + 1 end
        while true do
            local val
            val, pos = json_parse(str, pos)
            arr[#arr + 1] = val
            pos = str:match("^%s*()", pos)
            if str:sub(pos, pos) == ']' then return arr, pos + 1 end
            pos = str:match("^%s*,%s*()", pos)
        end
    elseif str:sub(pos, pos + 3) == "true" then
        return true, pos + 4
    elseif str:sub(pos, pos + 4) == "false" then
        return false, pos + 5
    elseif str:sub(pos, pos + 3) == "null" then
        return nil, pos + 4
    else
        -- Number
        local num_str = str:match("^%-?%d+%.?%d*[eE]?[+-]?%d*", pos)
        if num_str then
            return tonumber(num_str), pos + #num_str
        end
        return nil, pos + 1
    end
end

return socket_client
