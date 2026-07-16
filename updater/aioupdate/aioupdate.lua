--[[ AioUpdate -- a tiny companion addon that updates the AioHud plugin from its latest GitHub release.

     A compiled C++ plugin can't hot-swap or unload itself (it would crash mid-call), so this Lua addon does the
     dance the plugin can't: check the latest release, download the payload zip, //unload the plugin, extract the
     new files over plugins\ (the DLL is unlocked once unloaded), then //load it again. The user's plugins\AioHud\
     data\ (config, profiles) is never in the zip, so nothing is lost.

     Install : copy this folder to <Windower>\addons\aioupdate\ , then  //lua load aioupdate  (or add to init.txt).
     Use     : //aioupdate    (checks + updates if a newer release exists ; the HUD blinks off ~3s during the reload)
]]

_addon.name     = 'AioUpdate'
_addon.author   = 'Tetsouo'
_addon.version  = '1.1'
_addon.commands = { 'aioupdate', 'aioup' }

local REPO = 'Tetsouo/AioHud'
-- IMPORTANT: keep path args WITHOUT a trailing backslash. A quoted "...\" makes \" escape the quote on the command
-- line -> "Illegal characters in path". Windower's windower_path / addon_path DO end with a backslash.
local base        = windower.windower_path
local plugins_dir = base .. 'plugins'                        -- no trailing backslash (passed quoted to PowerShell)
local data_dir    = plugins_dir .. '\\AioHud\\data'
local zip         = data_dir .. '\\cache\\update.zip'
local vfile       = data_dir .. '\\version.txt'
local ps1         = windower.addon_path .. 'update.ps1'      -- addon_path ends with '\', filename has no trailing '\'

-- ASCII-only chat output (FFXI's chat log is ASCII ; no inline color codes -- add_to_chat takes a colour number)
local function log(s) windower.add_to_chat(207, 'AioUpdate: ' .. s) end

local function installed()
    local f = io.open(vfile, 'r')
    if not f then return '0' end
    local v = (f:read('*a') or ''):gsub('[^%w%.%-]', '')   -- keep only version-safe chars
    f:close()
    return v ~= '' and v or '0'
end

-- run update.ps1 in a given mode ; return its trimmed stdout (a single status line)
local function run(mode)
    local cmd = ('powershell -NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -File "%s" -Mode %s -Repo %s -Current "%s" -PluginsDir "%s" -Zip "%s" 2>&1')
        :format(ps1, mode, REPO, installed(), plugins_dir, zip)
    local h = io.popen(cmd)
    if not h then return 'ERROR could-not-run-powershell' end
    local out = h:read('*a') or ''
    h:close()
    return (out:gsub('^%s+', ''):gsub('%s+$', ''))
end

windower.register_event('addon command', function()
    log('checking for updates...')
    local r = run('prepare')
    if r:find('^UPTODATE') then
        log('already up to date (v' .. installed() .. ').')
    elseif r:find('^READY') then
        local tag = r:match('READY%s+(%S+)') or '?'
        log('downloading v' .. tag .. ', reloading AioHud...')
        windower.send_command('unload AioHud')
        -- wait for the unload so the DLL is unlocked, THEN extract + reload
        coroutine.schedule(function()
            local a = run('apply')
            windower.send_command('load AioHud')   -- reload either way (the new build on OK, the old one on failure)
            if a:find('^OK') then
                log('updated to v' .. tag .. '.')
            else
                log('update failed: ' .. a)
                log('(dual-box? //unload AioHud on the other client first, then //aioupdate again.)')
            end
        end, 3)
    else
        log('check failed: ' .. (r ~= '' and r or 'no response (network / GitHub?)'))
    end
end)

log('ready. Type //aioupdate to check for updates.')
