--[[ AioUpdate -- updates the AioHud plugin from its latest GitHub release, in game, WITH NO WINDOW.

     A compiled C++ plugin can't hot-swap or unload itself (it would crash mid-call), and a Lua-spawned process
     always flashes a cmd console. So the split is:
       - the PLUGIN launches the updater PowerShell with CREATE_NO_WINDOW (native -> truly no window) : it checks
         the latest release, downloads it, waits for the DLL to unlock, extracts, and writes data\update\done.txt.
       - THIS addon only sends the trigger and does the //unload + //load the plugin can't -- all via pure Lua
         (send_command + polling a text file), so it never opens a window either.
     The user's plugins\AioHud\data\ (config, profiles) is never in the zip, so nothing is lost.

     Install : copy this folder to <Windower>\addons\aioupdate\ , then  //lua load aioupdate  (or add to init.txt).
     Use     : //aioupdate    (checks + updates if a newer release exists ; the HUD blinks off ~3s during the reload)
]]

_addon.name     = 'AioUpdate'
_addon.author   = 'Tetsouo'
_addon.version  = '2.0'
_addon.commands = { 'aioupdate', 'aioup' }

local base     = windower.windower_path
local data_dir = base .. 'plugins\\AioHud\\data'
local done     = data_dir .. '\\update\\done.txt'
local vfile    = data_dir .. '\\version.txt'

local function log(s) windower.add_to_chat(207, 'AioUpdate: ' .. s) end

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

windower.register_event('addon command', function()
    os.remove(done)                       -- clear any stale status
    log('checking for updates...')
    windower.send_command('aio update')   -- the plugin runs the no-window updater
    local unloaded, tries = false, 0
    local function poll()
        tries = tries + 1
        local s = read_status()
        if s then
            if s:find('^UPTODATE') then
                log('already up to date (v' .. installed() .. ').')
                os.remove(done); return
            elseif s:find('^READY') and not unloaded then
                unloaded = true
                log('downloading v' .. (s:match('READY%s+(%S+)') or '?') .. ', reloading AioHud...')
                windower.send_command('unload AioHud')   -- release the DLL so the (still-running) updater can extract
            elseif s:find('^OK') then
                if unloaded then windower.send_command('load AioHud') end
                log('updated to v' .. (s:match('OK%s+(%S+)') or '?') .. '.')
                os.remove(done); return
            elseif s:find('^ERROR') then
                if unloaded then windower.send_command('load AioHud') end
                log('update failed: ' .. s)
                os.remove(done); return
            end
        end
        if tries < 120 then coroutine.schedule(poll, 1)   -- poll ~2 min max
        else log('timed out (network / GitHub?).'); if unloaded then windower.send_command('load AioHud') end end
    end
    coroutine.schedule(poll, 1)
end)

log('ready. Type //aioupdate to check for updates.')
