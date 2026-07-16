--[[ AioUpdate -- updates the AioHud plugin from its latest GitHub release, in game, WITH NO WINDOW.

     A compiled C++ plugin can't hot-swap or unload itself (it would crash mid-call), and a Lua-spawned process
     always flashes a cmd console. So the split is:
       - the PLUGIN launches the updater PowerShell with CREATE_NO_WINDOW (native -> truly no window) : it checks
         the latest release, downloads it, waits for the DLL to unlock, extracts, and writes data\update\done.txt.
       - THIS addon only sends the trigger and does the //unload + //load the plugin can't -- all via pure Lua
         (send_command + polling a text file), so it never opens a window either.
     The user's plugins\AioHud\data\ (config, profiles) is never in the zip, so nothing is lost.

     Triggers (both run the exact same no-window flow):
       - //aioupdate                          (typed)
       - the config's Update tab "Update now"  (the plugin writes data\update\request.txt ; we watch for it)

     Install : shipped in the release zip (extract into your Windower root). The plugin auto-registers
               "lua load aioupdate" in scripts\init.txt, so it loads with Windower -- no manual //lua load.
     Silent by default : it prints NOTHING to chat (a real update is visible as the HUD reloading).
                         Flip DEBUG=true below to trace the flag path + each poll while troubleshooting.
]]

_addon.name     = 'AioUpdate'
_addon.author   = 'Tetsouo'
_addon.version  = '2.1'
_addon.commands = { 'aioupdate', 'aioup' }

local base     = windower.windower_path
if base:sub(-1) ~= '\\' and base:sub(-1) ~= '/' then base = base .. '\\' end   -- ensure a trailing separator
local data_dir = base .. 'plugins\\AioHud\\data'
local done     = data_dir .. '\\update\\done.txt'
local request  = data_dir .. '\\update\\request.txt'
local vfile    = data_dir .. '\\version.txt'
local DEBUG    = false   -- flip to true to trace the flag path + each poll in chat

-- silent by default : every message is gated by DEBUG, so an update produces no chat output unless tracing.
local function log(s) if DEBUG then windower.add_to_chat(207, 'AioUpdate: ' .. s) end end

local function installed()
    local f = io.open(vfile, 'r')
    if not f then return '?' end
    local v = (f:read('*a') or ''):gsub('[^%w%.%-]', '')
    f:close()
    return v ~= '' and v or '?'
end

local function read_status()
    local f = io.open(done, 'r')
    if not f then return nil end
    local s = (f:read('*a') or ''):gsub('%s+$', '')
    f:close()
    return s ~= '' and s or nil
end

local busy = false
local function run_update()
    if busy then return end                   -- an update is already in flight
    busy = true
    if DEBUG then log('flag = ' .. done); log('installed = v' .. installed()) end
    os.remove(done)                           -- clear any stale status
    log('checking for updates...')
    windower.send_command('aio update')       -- the plugin runs the no-window updater
    local unloaded, tries = false, 0
    local function poll()
        tries = tries + 1
        local s = read_status()
        if DEBUG and (s or tries % 3 == 0) then log('poll ' .. tries .. ': ' .. (s or '(no flag yet)')) end
        if s then
            if s:find('^UPTODATE') then
                log('already up to date (v' .. installed() .. ').')
                os.remove(done); busy = false; return
            elseif s:find('^READY') and not unloaded then
                unloaded = true
                log('downloading v' .. (s:match('READY%s+(%S+)') or '?') .. ', reloading AioHud...')
                windower.send_command('unload AioHud')   -- release the DLL so the (still-running) updater can extract
            elseif s:find('^OK') then
                if unloaded then windower.send_command('load AioHud') end
                log('updated to v' .. (s:match('OK%s+(%S+)') or '?') .. '.')
                os.remove(done); busy = false; return
            elseif s:find('^ERROR') then
                if unloaded then windower.send_command('load AioHud') end
                log('update failed: ' .. s)
                os.remove(done); busy = false; return
            end
        end
        if tries < 120 then coroutine.schedule(poll, 1)   -- poll ~2 min max
        else log('timed out (network / GitHub?).'); if unloaded then windower.send_command('load AioHud') end; busy = false end
    end
    coroutine.schedule(poll, 1)
end

windower.register_event('addon command', run_update)

-- watch for the config Update tab's button : the plugin writes request.txt -> run the same no-window flow.
local function watch()
    local f = io.open(request, 'r')
    if f then f:close(); os.remove(request); run_update() end
    coroutine.schedule(watch, 1)
end
coroutine.schedule(watch, 1)
