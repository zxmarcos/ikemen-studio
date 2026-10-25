--;===========================================================
--; IKEMEN DEBUGGER BRIDGE
--;===========================================================
-- Talks to an external debugger over stdin/stdout. Only active when Ikemen
-- is started with "-ikdbg 1" (the debugger launches it that way).
--
-- Out (stdout), one JSON object per line after a tag:
--   @IKHELLO {...}   module loaded
--   @IKS {...}       snapshot: tick, pause state, every player and helper, events
--   @IKBRK {...}     a breakpoint stopped the game
--   @IKRES {...}     result of an ikdbg.exec call
--   @IKSRC {...}     where state controllers come from (needs the debugger patch)
--   @IKRELOAD {...}  result of ikdbg.reload (needs the debugger patch)
-- In (stdin): Ikemen runs each line as Lua, once per frame. The debugger
-- only sends "if ikdbg then ikdbg.exec('...') end", so a bad command is
-- reported instead of raising a Lua error (which would stop the engine).
-- The patched Ikemen also takes the same lines through shared memory
-- (editor_frame.go): all of them every frame, in menus too.
--
-- With an Ikemen GO that has the debugger patch (script_debugger.go), the
-- snapshots also list the state controllers that ran on the tick, and
-- breakpoints can be put on a controller.

if getCommandLineValue('-ikdbg') == nil then
	return
end

ikdbg = {
	version = '0.3',
	caps = {
		trace = debugTraceEnable ~= nil and debugTraceEnable(true),
		set = setVar ~= nil,
	},
	srcSent = 0,      -- sources already sent
	ctrlBreak = {},   -- source index -> true
	steps = 0,        -- ticks left to run before pausing again
	dirty = true,     -- send a snapshot even while paused
	idle = 0,         -- hook calls since the last snapshot
	lastTick = -1,
	lastState = {},   -- id -> state number, for state change events
	lastContact = {}, -- id -> moveContact, for hit events
	events = {},
	watch = {},       -- id -> true: send var/fvar for these entities
	breakpoints = {}, -- bid -> {pn, cond, fn, enabled, hits}
}

--;-----------------------------------------------------------
--; JSON
--;-----------------------------------------------------------
local function jsonString(s)
	return '"' .. s:gsub('[%c"\\]', function(c)
		if c == '"' then return '\\"' end
		if c == '\\' then return '\\\\' end
		if c == '\n' then return '\\n' end
		if c == '\r' then return '\\r' end
		if c == '\t' then return '\\t' end
		return string.format('\\u%04x', c:byte())
	end) .. '"'
end

-- table.concat of thousands of strings overflows gopher-lua's registry
-- ("registry overflow"), so long lists are joined in groups.
local function join(parts)
	local n = #parts
	if n <= 500 then return table.concat(parts, ',') end
	local groups = {}
	for i = 1, n, 500 do
		groups[#groups + 1] = table.concat(parts, ',', i, math.min(i + 499, n))
	end
	return table.concat(groups, ',')
end

local function json(v)
	local t = type(v)
	if t == 'nil' then
		return 'null'
	elseif t == 'boolean' then
		return v and 'true' or 'false'
	elseif t == 'number' then
		if v ~= v or v == math.huge or v == -math.huge then return 'null' end
		if v == math.floor(v) and math.abs(v) < 1e15 then return string.format('%d', v) end
		return string.format('%.6g', v)
	elseif t == 'string' then
		return jsonString(v)
	elseif t == 'table' then
		local parts = {}
		if v._array or #v > 0 then
			for i = 1, #v do parts[i] = json(v[i]) end
			return '[' .. join(parts) .. ']'
		end
		for k, val in pairs(v) do
			if k ~= '_array' then
				table.insert(parts, jsonString(tostring(k)) .. ':' .. json(val))
			end
		end
		return '{' .. join(parts) .. '}'
	end
	return jsonString(tostring(v))
end
ikdbg.json = json

local function send(tag, t)
	print(tag .. ' ' .. json(t))
end

--;-----------------------------------------------------------
--; Redirection helpers
--;-----------------------------------------------------------
-- Runs f with the trigger context on the entity with this id; restores the
-- previous context afterwards.
function ikdbg.with(eid, f)
	local hasOld, oldid = pcall(id)
	local ok, a, b = false, nil, nil
	if playerId(eid) then
		ok, a, b = pcall(f)
	end
	if hasOld then playerId(oldid) end
	return ok, a, b
end

-- Calls f(index) for every player and helper, with the context on it.
local function forEachEntity(f)
	local oldid = id()
	if not player(1) then
		for pn = 2, 8 do
			if player(pn) then break end
		end
	end
	local i = 0
	while playerIndexExist(i) do
		local back = id()
		if playerIndex(i) then
			f(i)
		end
		playerId(back)
		i = i + 1
	end
	playerId(oldid)
end

local function readEntity()
	local e = {
		id = id(), pn = playerNo(), h = isHelper(), name = name(),
		st = stateNo(), pst = prevStateNo(), anim = anim(),
		elem = animElemNo(0), elemn = getAnimElemCount(), etime = animElemVar('time'),
		atime = animTime(), time = time(),
		x = posX(), y = posY(), vx = velX(), vy = velY(), f = facing(),
		life = life(), lifemax = lifeMax(), pow = power(), powmax = powerMax(),
		ctrl = ctrl(), stt = stateType(), mvt = moveType(), phy = physics(),
		hp = hitPauseTime(), alive = alive(),
		mh = moveHit(), mc = moveContact(), mg = moveGuarded(),
		ss = stateSource and stateSource() or nil,
	}
	if ikdbg.watch[e.id] then
		local v, fv = {_array = true}, {_array = true}
		for n = 0, 59 do v[n + 1] = var(n) end
		for n = 0, 39 do fv[n + 1] = fvar(n) end
		e.vars, e.fvars = v, fv
	end
	return e
end

--;-----------------------------------------------------------
--; Commands (called through stdin)
--;-----------------------------------------------------------
function ikdbg.exec(code)
	local f, err = loadstring(code)
	if not f then
		send('@IKRES', {ok = false, r = tostring(err), code = code})
		return
	end
	-- Outside a match there is no character to go back to
	local hasOld, oldid = pcall(id)
	local res = {pcall(f)}
	if hasOld then pcall(playerId, oldid) end
	local out = {}
	for i = 2, #res do out[#out + 1] = tostring(res[i]) end
	send('@IKRES', {ok = res[1], r = table.concat(out, ', '), code = code})
	ikdbg.dirty = true
end

function ikdbg.pause(on)
	ikdbg.steps = 0
	togglePause(on)
	ikdbg.dirty = true
end

-- frameStep() set from stdin is cleared by the match loop before it is used,
-- so stepping unpauses and pauses again from the frame hook.
function ikdbg.step(n)
	ikdbg.steps = math.max(1, n or 1)
	togglePause(false)
end

function ikdbg.setWatch(eid, on)
	ikdbg.watch[eid] = on or nil
	ikdbg.dirty = true
end

function ikdbg.brkSet(bid, pn, cond, enabled)
	local fn, err = loadstring('return ' .. cond)
	if not fn then
		send('@IKRES', {ok = false, r = 'breakpoint ' .. bid .. ': ' .. tostring(err)})
		return
	end
	local old = ikdbg.breakpoints[bid]
	ikdbg.breakpoints[bid] = {pn = pn, cond = cond, fn = fn, enabled = enabled ~= false, hits = old and old.hits or 0}
end

function ikdbg.brkDel(bid)
	ikdbg.breakpoints[bid] = nil
end

-- Loads and compiles every player's files again (the debugger saved them); the
-- answer is @IKRELOAD {items = [{pn, ok, err}]}. Needs the patched Ikemen.
function ikdbg.reload()
	local items = {_array = true}
	if debugReloadChar == nil then
		table.insert(items, {pn = 0, ok = false, err = 'this Ikemen GO cannot reload characters (no debugger patch)'})
	else
		for pn = 1, 8 do
			local ok, err = debugReloadChar(pn)
			if ok or not tostring(err):find('^no player') then
				table.insert(items, {pn = pn, ok = ok, err = err})
			end
		end
	end
	send('@IKRELOAD', {items = items})
	ikdbg.dirty = true
end

-- A breakpoint on a state controller: the game stops after a tick in which it ran.
function ikdbg.ctrlBrkSet(src, on)
	ikdbg.ctrlBreak[src] = on or nil
end

--;-----------------------------------------------------------
--; Frame hook
--;-----------------------------------------------------------
-- A breakpoint stops the game when its condition becomes true for an entity
-- (not on every tick it stays true, or Continue would stop again at once).
local function checkBreakpoints(ents)
	local hit = nil
	for bid, b in pairs(ikdbg.breakpoints) do
		b.last = b.last or {}
		for _, e in ipairs(ents) do
			if b.enabled and (b.pn == 0 or (e.pn == b.pn and not e.h) or (b.pn < 0 and e.pn == -b.pn)) then
				local ok, r = ikdbg.with(e.id, b.fn)
				local now = ok and r ~= nil and r ~= false and r ~= 0
				if now and not b.last[e.id] and hit == nil then
					b.hits = b.hits + 1
					hit = {bid = bid, t = gameTime(), id = e.id, pn = e.pn, st = e.st, cond = b.cond}
				end
				b.last[e.id] = now
			end
		end
	end
	if hit then
		togglePause(true)
		ikdbg.steps = 0
		send('@IKBRK', hit)
	end
end

-- New sources go out a few hundred per frame (a character can have thousands).
local function sendSources()
	if debugSourceCount() <= ikdbg.srcSent then return end
	local items = debugSources(ikdbg.srcSent + 1, 300)
	items._array = true
	send('@IKSRC', {from = ikdbg.srcSent + 1, items = items})
	ikdbg.srcSent = ikdbg.srcSent + #items
end

-- The first controller breakpoint in the trace, if any.
local function checkCtrlBreak(tr)
	if next(ikdbg.ctrlBreak) == nil then return end
	for i = 1, #tr - 1, 2 do
		local src = tr[i + 1]
		if ikdbg.ctrlBreak[src] then
			local eid = tr[i]
			local _, st, pn = ikdbg.with(eid, function() return stateNo(), playerNo() end)
			togglePause(true)
			ikdbg.steps = 0
			send('@IKBRK', {kind = 'ctrl', src = src, t = gameTime(), id = eid, pn = pn, st = st})
			return true
		end
	end
end

local function frame()
	if ikdbg.caps.trace then sendSources() end
	local tick = gameTime()
	local advanced = tick ~= ikdbg.lastTick
	if not advanced and not ikdbg.dirty then
		ikdbg.idle = ikdbg.idle + 1
		if ikdbg.idle < 30 then return end
	end
	ikdbg.idle, ikdbg.dirty, ikdbg.lastTick = 0, false, tick

	local ents = {_array = true}
	forEachEntity(function()
		local e = readEntity()
		table.insert(ents, e)
		if advanced then
			local prev = ikdbg.lastState[e.id]
			if prev ~= nil and prev ~= e.st then
				table.insert(ikdbg.events, {k = 'st', t = tick, id = e.id, pn = e.pn, h = e.h, from = prev, to = e.st, anim = e.anim})
			end
			-- moveContact stays at 1 through the hit pause: report the first tick only
			if e.mc == 1 and ikdbg.lastContact[e.id] ~= 1 then
				table.insert(ikdbg.events, {k = e.mh == 1 and 'hit' or 'guarded', t = tick, id = e.id, pn = e.pn, h = e.h, st = e.st})
			end
			ikdbg.lastContact[e.id] = e.mc
		end
		ikdbg.lastState[e.id] = e.st
	end)

	local bk = {_array = true}
	for bid, b in pairs(ikdbg.breakpoints) do
		table.insert(bk, {bid = bid, hits = b.hits})
	end
	local ev = ikdbg.events
	ev._array = true
	local tr = nil
	if advanced and ikdbg.caps.trace then
		tr = debugTraceRead()
		tr._array = true
	end
	send('@IKS', {
		t = tick, rs = roundState(), rn = roundNo(), paused = paused(), steps = ikdbg.steps,
		adv = advanced, ents = ents, ev = ev, bk = bk, tr = tr,
	})
	ikdbg.events = {}

	if advanced then
		if not (tr and checkCtrlBreak(tr)) then
			checkBreakpoints(ents)
		end
		if ikdbg.steps > 0 then
			ikdbg.steps = ikdbg.steps - 1
			if ikdbg.steps == 0 then
				togglePause(true)
				ikdbg.dirty = true
			end
		end
	end
end

-- An error in the frame hook would stop the engine: report it (once per message) instead.
local reported = {}
hook.add('loop', 'ikdbg', function()
	local ok, err = pcall(frame)
	if not ok and not reported[tostring(err)] then
		reported[tostring(err)] = true
		send('@IKRES', {ok = false, r = 'ikdbg: ' .. tostring(err)})
	end
end)

send('@IKHELLO', {v = ikdbg.version, ik = version(), trace = ikdbg.caps.trace, set = ikdbg.caps.set})
