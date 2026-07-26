--[[ AioUpdate -- updates the AioHud plugin from its latest GitHub release, in game, WITH NO WINDOW.

     A compiled C++ plugin can't hot-swap or unload itself (it would crash mid-call), and a Lua-spawned process
     always flashes a cmd console. So the split is:
       - the PLUGIN launches the updater PowerShell with CREATE_NO_WINDOW (native -> truly no window) : it checks
         the latest release, downloads it, waits for the DLL to unlock, extracts, and writes data\update\done.txt.
       - THIS addon only does the //unload + //load the plugin can't -- by WATCHING done.txt in the background and
         reacting to its phases. All pure Lua (send_command + polling a text file), so it never opens a window.
     The user's plugins\AioHud\data\ (config, profiles) is never in the zip, so nothing is lost.

     v2.2 -- ROBUST : the download no longer waits on this addon. The plugin spawns the updater DIRECTLY (the
     config "Update now" button AND a typed //aioupdate both spawn it, de-duped plugin-side), and this addon just
     watches done.txt PERMANENTLY -> so a slow/late poll can never leave the update stuck at "no response". The
     only thing that still requires this addon to be loaded is the //unload + //load (the DLL can't self-swap).

     Triggers (both just ask the plugin to spawn -- the plugin de-dupes) :
       - //aioupdate                          (typed)
       - the config's Update tab "Update now"  (the plugin writes data\update\request.txt ; we relay it)

     Install : shipped in the release zip (extract into your Windower root). The plugin auto-registers
               "lua load aioupdate" in scripts\init.txt, so it loads with Windower -- no manual //lua load.
     Silent by default : it prints NOTHING to chat (a real update is visible as the HUD reloading).
                         Flip DEBUG=true below to trace each phase.
]]

_addon.name     = 'AioUpdate'
_addon.author   = 'ejouanchicot'
_addon.version  = '2.2'
_addon.commands = { 'aioupdate', 'aioup' }

local base     = windower.windower_path
if base:sub(-1) ~= '\\' and base:sub(-1) ~= '/' then base = base .. '\\' end   -- ensure a trailing separator
local data_dir = base .. 'plugins\\AioHud\\data'
local done     = data_dir .. '\\update\\done.txt'
local request  = data_dir .. '\\update\\request.txt'
local DEBUG    = false   -- flip to true to trace each phase in chat

-- silent by default : every message is gated by DEBUG, so an update produces no chat output unless tracing.
local function log(s) if DEBUG then windower.add_to_chat(207, 'AioUpdate: ' .. s) end end

local function read_status()
    local f = io.open(done, 'r')
    if not f then return nil end
    local s = (f:read('*a') or ''):gsub('%s+$', '')
    f:close()
    return s ~= '' and s or nil
end

-- ask the plugin to spawn the no-window updater. The plugin de-dupes (button + this may both fire within a
-- second), so calling it is always safe. Clearing done.txt first drops any stale phase from a previous run.
local function trigger()
    os.remove(done)
    log('triggering update...')
    windower.send_command('aio update')
end

-- PERMANENT background watcher : react to the updater's phases (written by the plugin's PowerShell) whenever
-- they appear, regardless of HOW the update was started. This is the only part that must stay loaded.
--   READY <v> -> the new build is downloaded ; //unload AioHud so the DLL can be replaced
--   OK <v>    -> extracted over the Windower root ; //load AioHud (only if we unloaded)
--   ERROR <m> -> reload the current build (if we unloaded) so the HUD comes back
--   UPTODATE  -> nothing to do
-- DUAL-BOX / MULTI-CLIENT : done.txt is a BROADCAST, not a queue. Every running client watches the same file,
-- so no client may consume it -- this addon used to os.remove() it on OK, and whichever client polled first
-- deleted the reload signal before the other saw it. The result: one character came back, the other stayed
-- unloaded and had to be //load'ed by hand. The updater script already clears done.txt at the start of every
-- run, so nobody here needs to delete anything.
--
-- Each client therefore tracks, in its OWN memory, the last phase it acted on, and reacts to a CHANGE.
local unloaded = false   -- did WE //unload this cycle ? (so we know whether to //load again)
local acted    = nil     -- the phase line this client has already handled

local function watch_done()
    local s = read_status()
    if s and s ~= acted then
        acted = s
        if s:find('^READY') then
            unloaded = true
            log('downloaded v' .. (s:match('READY%s+(%S+)') or '?') .. ', reloading AioHud...')
            windower.send_command('unload AioHud')       -- release the DLL so the (still-running) updater can extract
        elseif s:find('^OK') then
            if unloaded then windower.send_command('load AioHud') end
            log('updated to v' .. (s:match('OK%s+(%S+)') or '?') .. '.')
            unloaded = false
        elseif s:find('^ERROR') then
            if unloaded then windower.send_command('load AioHud') end
            log('update failed: ' .. s)
            unloaded = false
        elseif s:find('^UPTODATE') then
            log('already up to date.')
            unloaded = false
        end
    elseif not s then
        acted = nil          -- the updater cleared it : ready for the next cycle
        unloaded = false
    end
    coroutine.schedule(watch_done, 1)
end
coroutine.schedule(watch_done, 1)

-- Prime the handled phase at load : a done.txt left over from a previous session must not be replayed as a
-- fresh reload. A real update always rewrites the file, which reads as a change from this baseline.
acted = read_status()

-- SELF-UNINSTALL. The plugin appends "lua load aioupdate" to Windower's scripts\init.txt at every init, and
-- nothing ever removed it -- so a user who DELETES AioHud left Windower trying to load a missing addon at every
-- launch, for good, with no way to diagnose it (the thing that would explain it is the thing they removed).
-- The addon is the right place to undo it: it is the only piece that still runs once the plugin is gone.
--
-- Deliberately conservative. It fires only when the plugin DLL is actually absent, it rewrites init.txt by
-- copying every line except its own, and it does nothing at all if the file cannot be read or rewritten -- an
-- uninstall tidy-up must never be able to damage a working Windower install. Runs once, ~5 s after load, so it
-- never races the plugin's own init-time write on a normal startup.
local function self_uninstall_if_orphaned()
    local dll = base .. 'plugins\\AioHud.dll'
    local f = io.open(dll, 'rb')
    if f then f:close(); return end                 -- plugin present -> nothing to do, ever

    local ini = base .. 'scripts\\init.txt'
    local rf = io.open(ini, 'r'); if not rf then return end
    local lines, found = {}, false
    for line in rf:lines() do
        if line:lower():find('aioupdate', 1, true) then found = true else lines[#lines + 1] = line end
    end
    rf:close()
    if not found then return end

    -- temp + rename, like the plugin's own writer : a half-written init.txt breaks EVERY plugin, not just ours.
    local tmp = ini .. '.aiotmp'
    local wf = io.open(tmp, 'w'); if not wf then return end
    local ok = true
    for _, l in ipairs(lines) do ok = ok and (wf:write(l, '\n') ~= nil) end
    wf:close()
    if not ok then os.remove(tmp); return end
    os.remove(ini)
    if not os.rename(tmp, ini) then os.remove(tmp); return end
    windower.add_to_chat(207, 'AioUpdate: AioHud is gone -- removed its autoload line from init.txt. You can //lua unload aioupdate and delete this addon.')
end
coroutine.schedule(self_uninstall_if_orphaned, 5)

-- typed //aioupdate : just ask the plugin to spawn (the watcher above handles the unload/load).
windower.register_event('addon command', trigger)

-- config "Update now" button : the plugin writes request.txt. New plugins ALSO spawn directly (this relay is
-- then a de-duped no-op) ; old plugins rely on this relay to spawn. Either way the watcher does the unload/load.
local function watch_request()
    local f = io.open(request, 'r')
    if f then f:close(); os.remove(request); trigger() end
    coroutine.schedule(watch_request, 1)
end
coroutine.schedule(watch_request, 1)
