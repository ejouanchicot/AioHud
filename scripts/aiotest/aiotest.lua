-- aiotest.lua -- the in-game test bridge for AioHUD. A DEV tool : it is never shipped to players.
--
-- WHY THIS EXISTS. The harness plan says the in-game half "is separate and always will be" : no offline test
-- can prove that *(g+0x40)+0x02 is the zone, so memory offsets, region differences and anything the client
-- decides were left to //aio doctor and to a human looking at the screen. That was true only while the only
-- witness WAS the plugin. Windower reads the same facts through a COMPLETELY DIFFERENT path -- its own memory
-- layer and packet handlers -- so an addon is an INDEPENDENT ORACLE. "Is our zone offset right?" stops being a
-- matter of opinion: our answer against Windower's, every time, automatically.
--
-- And 2026-09-12 showed why the in-game half matters more than it looks: an audit read a comment, concluded a
-- stencil mask was dead, removed it, and the HP bars went square-ended. No offline test could have seen that.
--
-- WHAT IT DOES, and deliberately no more :
--   * it reads a REQUEST file, runs the commands it names, waits, takes a snapshot of WINDOWER's own view of
--     the game, and writes it to a RESPONSE file.
--   * that is all. It asserts nothing. The assertions live on the other side (scripts/igtest.py), where they
--     can be edited and re-run without reloading an addon and without a game restart.
--
-- TRANSPORT : two text files in this addon's own folder, not a socket. No port, no firewall prompt, it works
-- while the client is minimised or the player is AFK, it survives a crash on either side, and both halves are
-- readable with a text editor when something goes wrong. LuaSocket IS available here (CurePlease and XivParty
-- use it) and could carry an interactive version later ; it is not needed for batch testing.
--
-- FORMAT : flat `key=value`, one per line. Windower's bundled json library only PARSES, it cannot encode, and
-- a hand-rolled encoder is exactly the kind of code that is wrong for a week without anyone noticing. Flat
-- lines are trivial to write here, trivial to read there, and greppable when a test fails at 2 am.
--
-- IT RUNS WHAT IT IS GIVEN. The request file names game commands and they are executed verbatim -- that is the
-- point (cast a spell, then assert the timer), and both files live on this machine, written by its owner. It
-- does NOT execute anything on load : the id present at load time is latched as already done, so reloading the
-- addon never replays the last test.
_addon.name    = 'aiotest'
_addon.author  = 'Tetsouo'
_addon.version = '1.0.0'
_addon.command = 'aiotest'

require('strings')

local IN   = windower.addon_path .. 'test_in.txt'
local OUT  = windower.addon_path .. 'test_out.txt'

local POLL_FRAMES = 20          -- ~3 Hz. A request is typed by a tool, not by a finger : latency is free.
local frame   = 0
local last_id = nil             -- the request id already handled (latched, never re-run)
local job     = nil             -- the running request, if any

-- ---- tiny helpers ------------------------------------------------------------------------------------------
local function read_file(path)
    local f = io.open(path, 'r')
    if not f then return nil end
    local s = f:read('*a')
    f:close()
    return s
end

local function write_file(path, s)
    local f = io.open(path, 'w')
    if not f then return false end
    f:write(s)
    f:close()
    return true
end

-- `key=value` per line -> table. Values keep their raw text (the other side knows the types).
local function parse_kv(s)
    local t = {}
    if not s then return t end
    for line in s:gmatch('[^\r\n]+') do
        local k, v = line:match('^%s*([%w_%.]+)%s*=%s*(.-)%s*$')
        if k then t[k] = v end
    end
    return t
end

local out_lines = {}
local function emit(k, v)
    if v == nil then return end
    if type(v) == 'boolean' then v = v and '1' or '0' end
    out_lines[#out_lines + 1] = tostring(k) .. '=' .. tostring(v)
end

-- ---- the snapshot : WINDOWER's own view, section by section -------------------------------------------------
-- Every section is independently guarded : not logged in, no target, a nil vitals table -- each writes an
-- explicit `<section>.ok=0` instead of vanishing, because a missing section and an empty one must not look the
-- same on the other side. That is rule 10's "empty is not unavailable", applied to the bridge itself.
local function snap_info()
    local i = windower.ffxi.get_info()
    if not i then emit('info.ok', 0) return end
    emit('info.ok', 1)
    emit('info.zone', i.zone)
    emit('info.logged_in', i.logged_in)
    emit('info.mog_house', i.mog_house)
    emit('info.target_index', i.target_index)
end

local function snap_player()
    local p = windower.ffxi.get_player()
    if not p then emit('player.ok', 0) return end
    emit('player.ok', 1)
    emit('player.id', string.format('%08X', p.id or 0))
    emit('player.name', p.name)
    emit('player.index', p.index)
    emit('player.main_job_id', p.main_job_id)
    emit('player.sub_job_id', p.sub_job_id)
    emit('player.main_job_level', p.main_job_level)
    if p.vitals then
        emit('player.hp', p.vitals.hp)   emit('player.hpp', p.vitals.hpp)
        emit('player.mp', p.vitals.mp)   emit('player.mpp', p.vitals.mpp)
        emit('player.tp', p.vitals.tp)
    else
        emit('player.vitals.ok', 0)
    end
    if p.buffs then
        emit('buffs.n', #p.buffs)
        emit('buffs.ids', table.concat(p.buffs, ','))
    else
        emit('buffs.ok', 0)
    end
end

local function snap_party()
    local pt = windower.ffxi.get_party()
    if not pt then emit('party.ok', 0) return end
    emit('party.ok', 1)
    emit('party.p1_count', pt.party1_count)
    emit('party.p2_count', pt.party2_count)
    emit('party.p3_count', pt.party3_count)
    -- The slot KEYS are what matter : p0..p5 is your party, a10..a15 and a20..a25 the two alliance parties.
    -- Emitting them by key (not by a renumbered index) keeps the mapping to AioHUD's 0..17 member array
    -- explicit on the other side instead of guessed here.
    local keys = { 'p0','p1','p2','p3','p4','p5',
                   'a10','a11','a12','a13','a14','a15',
                   'a20','a21','a22','a23','a24','a25' }
    local n = 0
    for _, k in ipairs(keys) do
        local m = pt[k]
        if m and m.name then
            n = n + 1
            emit('party.' .. k .. '.name', m.name)
            emit('party.' .. k .. '.hp',  m.hp)
            emit('party.' .. k .. '.hpp', m.hpp)
            emit('party.' .. k .. '.mp',  m.mp)
            emit('party.' .. k .. '.tp',  m.tp)
            emit('party.' .. k .. '.zone', m.zone)
            if m.mob then
                emit('party.' .. k .. '.id', string.format('%08X', m.mob.id or 0))
                emit('party.' .. k .. '.index', m.mob.index)
                emit('party.' .. k .. '.dist', string.format('%.2f', math.sqrt(m.mob.distance or 0)))
            else
                emit('party.' .. k .. '.mob', 'none')   -- out of zone / not spawned : a FACT, not a gap
            end
        end
    end
    emit('party.named', n)
end

local function snap_target()
    local t = windower.ffxi.get_mob_by_target('t')
    if not t then emit('target.ok', 0) return end
    emit('target.ok', 1)
    emit('target.id', string.format('%08X', t.id or 0))
    emit('target.index', t.index)
    emit('target.name', t.name)
    emit('target.hpp', t.hpp)
    emit('target.spawn_type', t.spawn_type)
    emit('target.dist', string.format('%.2f', math.sqrt(t.distance or 0)))
    emit('target.is_npc', t.is_npc)
end

local function snap_recasts()
    local sp = windower.ffxi.get_spell_recasts()
    local ab = windower.ffxi.get_ability_recasts()
    if sp then
        local n, parts = 0, {}
        for id, t in pairs(sp) do
            if t and t > 0 then n = n + 1; parts[#parts + 1] = id .. ':' .. math.floor(t) end
        end
        emit('recast.spells_pending', n)
        emit('recast.spells', table.concat(parts, ','))
    else
        emit('recast.spells.ok', 0)
    end
    if ab then
        local n, parts = 0, {}
        for id, t in pairs(ab) do
            if t and t > 0 then n = n + 1; parts[#parts + 1] = id .. ':' .. math.floor(t) end
        end
        emit('recast.abils_pending', n)
        emit('recast.abils', table.concat(parts, ','))
    else
        emit('recast.abils.ok', 0)
    end
end

local SECTIONS = {
    info    = snap_info,
    player  = snap_player,
    party   = snap_party,
    target  = snap_target,
    buffs   = snap_player,     -- buffs ride with the player snapshot ; asking for either is enough
    recasts = snap_recasts,
}

-- ---- the sequencer -----------------------------------------------------------------------------------------
local function finish(req)
    out_lines = {}
    emit('id', req.id)
    emit('t', os.time())
    emit('addon_version', _addon.version)
    local want = req.want or 'info,player,party,target'
    local done = {}
    for name in want:gmatch('[^,%s]+') do
        local fn = SECTIONS[name]
        if fn and not done[fn] then done[fn] = true; fn() end
        if not fn then emit('unknown_section.' .. name, 1) end
    end
    emit('ok', 1)
    write_file(OUT, table.concat(out_lines, '\n') .. '\n')
    windower.add_to_chat(200, '[aiotest] request ' .. tostring(req.id) .. ' -> test_out.txt')
end

windower.register_event('prerender', function()
    frame = frame + 1
    if frame % POLL_FRAMES ~= 0 then return end

    -- a request in flight : wait out its delay, then snapshot
    if job then
        if os.clock() * 1000 >= job.due then
            local req = job.req
            job = nil
            finish(req)
        end
        return
    end

    local req = parse_kv(read_file(IN))
    if not req.id or req.id == last_id then return end
    last_id = req.id

    -- run the commands first (cmd1, cmd2, ... in order), then wait before looking : a command dispatched this
    -- frame has not been processed yet, and the plugin's own answer may need a frame or a packet.
    local i = 1
    while req['cmd' .. i] do
        windower.send_command(req['cmd' .. i])
        i = i + 1
    end
    if req.cmd then windower.send_command(req.cmd) end

    local wait = tonumber(req.wait_ms or '600') or 600
    job = { req = req, due = os.clock() * 1000 + wait }
end)

-- LOAD must not replay : latch whatever id is sitting in the request file as already handled.
windower.register_event('load', function()
    local req = parse_kv(read_file(IN))
    last_id = req.id
    windower.add_to_chat(200, '[aiotest] ready -- files in ' .. windower.addon_path
                              .. ' (latched id=' .. tostring(last_id) .. ')')
end)

windower.register_event('addon command', function(cmd)
    cmd = (cmd or ''):lower()
    if cmd == 'ping' then
        out_lines = {}
        emit('id', 'ping'); emit('t', os.time()); emit('ok', 1)
        snap_info()
        write_file(OUT, table.concat(out_lines, '\n') .. '\n')
        windower.add_to_chat(200, '[aiotest] pong -> test_out.txt')
    elseif cmd == 'path' then
        windower.add_to_chat(200, '[aiotest] ' .. windower.addon_path)
    else
        windower.add_to_chat(200, '[aiotest] commands : ping | path   (normal use is the request file)')
    end
end)
