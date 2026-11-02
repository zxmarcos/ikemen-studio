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
--   @IKFILES {...}   every player's files (needs the debugger patch)
--   @IKTAKE {...}    a take of a player's input recorded (needs the debugger patch)
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
	version = '0.5',
	caps = {
		trace = debugTraceEnable ~= nil and debugTraceEnable(true),
		set = setVar ~= nil,
	},
	srcSent = 0,      -- sources already sent
	ctrlBreak = {},   -- source index -> true, or {fn}: stop only if fn() is true
	steps = 0,        -- ticks left to run before pausing again
	dirty = true,     -- send a snapshot even while paused
	idle = 0,         -- hook calls since the last snapshot
	lastTick = -1,
	lastState = {},   -- id -> state number, for state change events
	lastContact = {}, -- id -> moveContact, for hit events
	events = {},
	watch = {},       -- id -> true: send var/fvar for these entities
	breakpoints = {}, -- bid -> {pn, cond, fn, enabled, hits}
	exprs = {},       -- n -> {pn, fn} or {err}: the Watch window's expressions
	-- The Lab window: dummy {pn, stance, guard, life, power, lastHit}; waiting: a take to record or
	-- play once the round reset starts the fight; playing: the take played, to loop with a reset
	lab = {},
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
			-- A stage's attached character without an animation fails to be read: left out
			pcall(f, i)
		end
		playerId(back)
		i = i + 1
	end
	playerId(oldid)
end

-- Whether its animation element has a Clsn1 box (nil: this Ikemen GO cannot tell).
local function hasClsn1()
	if not clsnVar then return nil end
	local back = clsnVar('clsn1', 0, 'back')
	return back == back -- NaN without one
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
		ss = stateSource and stateSource() or nil, c1 = hasClsn1(),
		nex = numExplod and numExplod() or nil, npr = numProj and numProj() or nil,
	}
	-- A stage's attached character has fields the engine gives as a Go nil, which type() cannot read
	for k, v in pairs(e) do
		if not pcall(type, v) then e[k] = nil end
	end
	if ikdbg.watch[e.id] then
		local v, fv = {_array = true}, {_array = true}
		for n = 0, 59 do v[n + 1] = var(n) end
		for n = 0, 39 do fv[n + 1] = fvar(n) end
		e.vars, e.fvars = v, fv
		if debugObjects then
			local ex, pr = debugObjects()
			ex._array, pr._array = true, true
			e.explods, e.projs = ex, pr
		end
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
	-- primed after its first check: what is already true then does not stop the game
	ikdbg.breakpoints[bid] = {pn = pn, cond = cond, fn = fn, enabled = enabled ~= false, hits = old and old.hits or 0,
		primed = false}
end

function ikdbg.brkDel(bid)
	ikdbg.breakpoints[bid] = nil
end

-- A Watch expression, read with each snapshot on player pn (0: the entity whose variables are
-- watched, the one the Inspector shows). An expression, or statements that return values.
function ikdbg.exprSet(n, pn, code)
	local fn, err = loadstring('return ' .. code)
	if not fn then fn, err = loadstring(code) end
	ikdbg.exprs[n] = fn and {pn = pn, fn = fn} or {err = tostring(err)}
	ikdbg.dirty = true
end

function ikdbg.exprDel(n)
	ikdbg.exprs[n] = nil
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

-- Every player's files: @IKFILES {players = [{pn, files = {key = [paths]}}]}.
function ikdbg.files()
	local players = {_array = true}
	if debugCharFiles ~= nil then
		for pn = 1, 8 do
			local f = debugCharFiles(pn)
			if f.def then
				table.insert(players, {pn = pn, files = f, palno = debugPalNo ~= nil and debugPalNo(pn) or 0})
			end
		end
	end
	send('@IKFILES', {players = players})
end

-- The stage's 3D model: its animations, nodes and lights (debugModel, editor_model.go).
function ikdbg.model()
	local m = debugModel ~= nil and debugModel() or nil
	send('@IKMODEL', m or {none = true})
end

-- The keys of one of its animations (debugModelAnimKeys).
function ikdbg.modelKeys(i)
	local k = debugModelAnimKeys ~= nil and debugModelAnimKeys(i) or nil
	send('@IKMKEYS', k or {anim = i, none = true})
end

-- The frames' timings since the last call (it starts the measures).
function ikdbg.perf()
	send('@IKPERF', debugPerf ~= nil and debugPerf() or {none = true})
end

-- Compiles a player's animations from unsaved text (a file the debugger wrote).
function ikdbg.anims(pn, textFile)
	local ok, err = debugReloadAnims(pn, textFile)
	if not ok then send('@IKRES', {ok = false, r = 'animations of P' .. pn .. ': ' .. tostring(err)}) end
	ikdbg.dirty = true
end

-- Reads a player's SFF again (the debugger wrote it), for every player using it.
function ikdbg.sprites(pn)
	local ok, err = debugReloadSprites(pn)
	if not ok then send('@IKRES', {ok = false, r = 'sprites of P' .. pn .. ': ' .. tostring(err)}) end
	ikdbg.dirty = true
end

-- Gives palette group,number of a player's SFF new colours (256 RGBA colours in hexadecimal),
-- for every player using it.
function ikdbg.palette(pn, group, number, colours)
	if debugSetPalette == nil then return end
	local ok, err = debugSetPalette(pn, group, number, colours)
	if not ok then send('@IKRES', {ok = false, r = 'palette ' .. group .. ',' .. number .. ': ' .. tostring(err)}) end
	ikdbg.dirty = true
end

-- Reads a player's SND again (the debugger wrote it), for every player using it.
function ikdbg.sounds(pn)
	local ok, err = debugReloadSounds(pn)
	if not ok then send('@IKRES', {ok = false, r = 'sounds of P' .. pn .. ': ' .. tostring(err)}) end
end

-- Adds select.def's characters and stages again (needs the debugger patch: debugSelectReset,
-- main.f_loadSelectDef, start.f_makeGrid), from unsaved text (a file the debugger wrote,
-- deleted once read) or from the file. In menus and on a select screen: not between a select
-- screen and the end of its game mode (the characters chosen are in use).
function ikdbg.reloadRoster(textFile)
	local ok, err = false, 'the roster changes in menus or on a select screen, not in a game mode under way'
	if not ikdbg.inMode or (ikdbg.inSelect and not ikdbg.inChallenger) then
		ok, err = debugSelectReset()
	end
	if ok then
		main.t_stageDef = {['random'] = 0}
		main.t_charDef = {}
		main.t_selChars = {}
		main.t_selStages = {}
		main.t_unlockLua.chars, main.t_unlockLua.stages = {}, {}
		local file, nextRefresh = motif.files.select, main.nextRefresh
		motif.files.select = textFile or file
		main.nextRefresh = math.huge -- no loading screen frames in the middle of this one
		ok, err = pcall(function()
			main.f_loadSelectDef()
			start.f_makeGrid()
			if ikdbg.inSelect then -- what was chosen goes (the cells changed)
				start.f_selectReset(false, true)
				start.needUpdateDrawList = true
			end
		end)
		motif.files.select, main.nextRefresh = file, nextRefresh
	end
	if textFile then os.remove(textFile) end
	if not ok then error(err, 0) end
end

--;-----------------------------------------------------------
--; Lab: a dummy, takes of input (needs the debugger patch: debugInputHold, debugInputRecord,
--; debugInputPlay, debugSwapChar)
--;-----------------------------------------------------------
-- Runs f with the trigger context on player pn (its root).
local function onPlayer(pn, f)
	local hasOld, oldid = pcall(id)
	local ok, there = pcall(player, pn)
	local res = {false}
	if ok and there then res = {pcall(f)} end
	if hasOld then pcall(playerId, oldid) end
	return unpack(res)
end

-- Takes go between the debugger and the game as runs: key bits, ticks, key bits, ticks...
local function toRuns(take)
	local runs, last, n = {_array = true}, nil, 0
	for _, v in ipairs(take) do
		if v == last then
			n = n + 1
		else
			if last then table.insert(runs, last); table.insert(runs, n) end
			last, n = v, 1
		end
	end
	if last then table.insert(runs, last); table.insert(runs, n) end
	return runs
end

local function fromRuns(runs)
	local take = {}
	for i = 1, #runs - 1, 2 do
		for _ = 1, runs[i + 1] do take[#take + 1] = runs[i] end
	end
	return take
end

-- A player held by the editor: stance 'stand', 'crouch' or 'jump'; guard 'never', 'always',
-- 'first' (after a first hit, until 90 ticks go by without one) or 'random' (each attack, with a
-- chance in percent); life and power kept full once each combo is over. pn 0: none.
function ikdbg.dummy(pn, stance, guard, life, power, chance)
	local d = ikdbg.lab.dummy
	if d and d.pn ~= pn then debugInputHold(d.pn, nil) end
	ikdbg.lab.dummy = nil
	if pn > 0 then
		ikdbg.lab.dummy = {pn = pn, stance = stance, guard = guard, life = life, power = power, lastHit = -1000,
			chance = chance or 50}
		onPlayer(pn, function() setAILevel(0) end) -- its AI would play on
	end
	ikdbg.dirty = true
end

-- What the dummy does once it can act again (a reversal): a take, played so that its last tick is
-- the first one the dummy can act on; after 'guard', 'hit' (not knocked down), 'wakeup' or 'any';
-- each time with a chance in percent. runs nil: nothing.
function ikdbg.dummyReact(runs, after, chance)
	local d = ikdbg.lab.dummy
	if not d then return end
	d.react = runs and {take = fromRuns(runs), after = after, chance = chance or 100} or nil
	d.ep = nil
end

-- Ticks from now to the first one the dummy can act on (its [Statedef -1] sees ctrl), when the
-- engine tells: the hit or guard stun left, or the get up animation left. Measured on KFM: the
-- shake counts down first, the hit time once it is 0 (from that tick on), the hit state ends the
-- tick after it reaches -1 giving ctrl, and [Statedef -1] sees it the next tick; getting up (a
-- state after lying down, movetype no longer H, without ctrl yet: the common 5120) ends the tick
-- after AnimTime is 0, and [Statedef -1] sees ctrl the next.
local kWakeLead = 2
local function ticksToAct(ep)
	if ep.down and moveType() ~= 'H' and not ctrl() then return -animTime() + kWakeLead end
	if moveType() == 'H' and not getHitVar('fall') then
		local shake, hittime = getHitVar('hitshaketime'), getHitVar('hittime')
		if shake > 0 then return shake + hittime + 2 end
		return hittime + 3
	end
	return nil
end

-- Runs with the context on the dummy: follows a hit (an episode: guarded, knocked down) and
-- starts the reaction in time.
local function dummyReact(d, hit)
	local r = d.react
	if not r then return end
	local free = ctrl() and not hit
	if hit or (d.ep and not free) then
		d.ep = d.ep or {skip = math.random(100) > r.chance}
		if getHitVar('guarded') and hit then d.ep.guarded = true end
		if stateType() == 'L' then d.ep.down = true end
	end
	local ep = d.ep
	if not ep then return end
	local wanted = r.after == 'any' or (r.after == 'guard' and ep.guarded) or
		(r.after == 'wakeup' and ep.down) or (r.after == 'hit' and not ep.guarded and not ep.down)
	if wanted and not ep.started and not ep.skip then
		local left = ticksToAct(ep)
		if free or (left and left <= #r.take) then
			debugInputPlay(d.pn, r.take)
			ep.started = true
		end
	end
	if free then d.ep = nil end
end

local function runDummy()
	local d = ikdbg.lab.dummy
	if not d or ikdbg.lab.playing and ikdbg.lab.playing.pn == d.pn then return end
	local keys
	onPlayer(d.pn, function()
		local hit = moveType() == 'H'
		if hit then d.lastHit = gameTime() end
		dummyReact(d, hit)
		local near = inGuardDist()
		if near and not d.near then d.roll = math.random(100) <= d.chance end -- an attack coming: guard it?
		d.near = near
		local guard = d.guard == 'always' or (d.guard == 'first' and gameTime() - d.lastHit < 90) or
			(d.guard == 'random' and d.roll)
		keys = (d.stance == 'crouch' and 'D' or d.stance == 'jump' and 'U' or '') ..
			(guard and near and 'B' or '')
		if not hit and ctrl() then
			if d.life and life() < lifeMax() then setLife(lifeMax()) end
			if d.power and power() < powerMax() then setPower(powerMax()) end
		end
	end)
	if keys then debugInputHold(d.pn, keys) end
end

local function startTake(w)
	if w.kind == 'rec' then
		debugInputRecord(w.pn, true)
		ikdbg.lab.recording = w.pn
	else
		debugInputPlay(w.pn, fromRuns(w.runs), w.loop and not w.reset, w.seed)
		ikdbg.lab.playing = w
	end
end

-- Records player pn's input (from the start of the fight, after a round reset, with reset).
function ikdbg.record(pn, reset)
	ikdbg.stopTake()
	local w = {kind = 'rec', pn = pn, reset = reset}
	if reset then
		resetRound()
		ikdbg.lab.waiting = w
	else
		startTake(w)
	end
	ikdbg.dirty = true
end

-- Plays a take as player pn's input; seed: the recording's random seed, for the same random
-- numbers; reset: from the start of the fight after a round reset (each time, with loop).
function ikdbg.play(pn, runs, loop, seed, reset)
	ikdbg.stopTake()
	local w = {kind = 'play', pn = pn, runs = runs, loop = loop, seed = seed, reset = reset}
	if reset then
		resetRound()
		ikdbg.lab.waiting = w
	else
		startTake(w)
	end
	ikdbg.dirty = true
end

-- Stops recording (the take goes out as @IKTAKE {pn, seed, runs}) or playing.
function ikdbg.stopTake()
	local lab = ikdbg.lab
	if lab.recording then
		local take, seed = debugInputRecord(lab.recording, false)
		local files = debugCharFiles and debugCharFiles(lab.recording) or {}
		send('@IKTAKE', {pn = lab.recording, seed = seed, runs = toRuns(take), char = files.def and files.def[1]})
		lab.recording = nil
	end
	if lab.playing then debugInputPlay(lab.playing.pn) end
	lab.playing, lab.waiting = nil, nil
	ikdbg.dirty = true
end

-- Puts another character in player pn's place; the match starts again with it.
function ikdbg.swap(pn, def, pal)
	ikdbg.stopTake()
	local ok, err = debugSwapChar(pn, def, pal)
	if not ok then error(err, 0) end
	ikdbg.lastState, ikdbg.lastContact = {}, {}
end

-- Each tick: a take waiting for the fight to start, a take played to its end, the dummy.
local function runLab()
	local lab = ikdbg.lab
	local w = lab.waiting
	if w then
		if roundState() ~= 2 then
			w.left = true -- the reset began
		elseif w.left then
			lab.waiting = nil
			startTake(w)
		end
	end
	local p = lab.playing
	if p then
		local _, _, playing = debugInputState(p.pn)
		if not playing then
			if p.loop and p.reset then
				lab.playing = nil
				resetRound()
				p.left = nil
				lab.waiting = p
			elseif not p.loop then
				lab.playing = nil
			end
		end
	end
	runDummy()
end

-- What the Lab window shows: {dummy, rec, recTicks, play, at, len, waiting}.
local function labState()
	local lab = ikdbg.lab
	if not (lab.dummy or lab.recording or lab.playing or lab.waiting) then return nil end
	local s = {dummy = lab.dummy and lab.dummy.pn or 0, waiting = lab.waiting and lab.waiting.kind or nil}
	if lab.recording then
		local _, n = debugInputState(lab.recording)
		s.rec, s.recTicks = lab.recording, n
	end
	if lab.playing then
		local _, _, _, at, len = debugInputState(lab.playing.pn)
		s.play, s.at, s.len = lab.playing.pn, at, len
	end
	return s
end

-- A breakpoint on a state controller: the game stops after a tick in which it ran; with a
-- condition, only if that is true then for the one that ran it.
function ikdbg.ctrlBrkSet(src, on, cond)
	local b = on or nil
	if on and cond and cond:find('%S') then
		local fn, err = loadstring('return ' .. cond)
		if not fn then
			send('@IKRES', {ok = false, r = 'breakpoint condition: ' .. tostring(err)})
			b = nil
		else
			b = {fn = fn}
		end
	end
	ikdbg.ctrlBreak[src] = b
end

--;-----------------------------------------------------------
--; Frame hook
--;-----------------------------------------------------------
-- A breakpoint stops the game when its condition becomes true for an entity
-- (not on every tick it stays true, or Continue would stop again at once), and
-- not for what was already true when it was set (a state breakpoint set while
-- in that state waits for the state to be entered again).
local function checkBreakpoints(ents)
	local hit = nil
	for bid, b in pairs(ikdbg.breakpoints) do
		b.last = b.last or {}
		local primed = b.primed
		b.primed = true
		for _, e in ipairs(ents) do
			if b.enabled and (b.pn == 0 or (e.pn == b.pn and not e.h) or (b.pn < 0 and e.pn == -b.pn)) then
				local ok, r = ikdbg.with(e.id, b.fn)
				local now = ok and r ~= nil and r ~= false and r ~= 0
				if now and primed and not b.last[e.id] and hit == nil then
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
		local b, eid = ikdbg.ctrlBreak[src], tr[i]
		local stop = b == true
		if b and b.fn then
			local ok, r = ikdbg.with(eid, b.fn)
			stop = ok and r ~= nil and r ~= false and r ~= 0
		end
		if stop then
			local _, st, pn = ikdbg.with(eid, function() return stateNo(), playerNo() end)
			togglePause(true)
			ikdbg.steps = 0
			send('@IKBRK', {kind = 'ctrl', src = src, t = gameTime(), id = eid, pn = pn, st = st})
			return true
		end
	end
end

-- The Watch expressions' values: [{n, v}] or [{n, err}].
local function readExprs()
	if next(ikdbg.exprs) == nil then return nil end
	local sel = next(ikdbg.watch)
	local out = {_array = true}
	for n, x in pairs(ikdbg.exprs) do
		local r = {n = n, err = x.err}
		if x.fn then
			local hasOld, oldid = pcall(id)
			local ok, there = false, false
			if x.pn > 0 then ok, there = pcall(player, x.pn) elseif sel then ok, there = pcall(playerId, sel) end
			local res = ok and there and {pcall(x.fn)} or {false, 'no such player'}
			if hasOld then pcall(playerId, oldid) end
			if res[1] then
				local vals = {}
				for i = 2, table.maxn(res) do vals[#vals + 1] = tostring(res[i]) end
				r.v = table.concat(vals, ', ')
			else
				r.err = tostring(res[2])
			end
		end
		table.insert(out, r)
	end
	return out
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
			-- Hit (or guarding one) on this tick: what it does to it (the damage is taken next tick)
			if getHitVar('frame') then
				table.insert(ikdbg.events, {k = 'got', t = tick, id = e.id, pn = e.pn, h = e.h, st = e.st,
					by = getHitVar('playerid'), dmg = getHitVar('damage'), guarded = getHitVar('guarded'),
					hittime = getHitVar('hittime'), shake = getHitVar('hitshaketime'),
					slide = getHitVar('slidetime'), ctrltime = getHitVar('ctrltime'), fall = getHitVar('fall'),
					attr = getHitVar('attr'), combo = getHitVar('hitcount'), animtype = getHitVar('animtype'),
					-- the juggle points its attacker has left on it
					jug = select(2, ikdbg.with(getHitVar('playerid'), function() return jugglePoints(e.id) end))})
			end
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
	if advanced and debugInputHold then runLab() end
	send('@IKS', {
		t = tick, rs = roundState(), rn = roundNo(), paused = paused(), steps = ikdbg.steps,
		adv = advanced, ents = ents, ev = ev, bk = bk, tr = tr, w = readExprs(),
		lab = debugInputHold and labState() or nil,
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
	local ok, err = xpcall(frame, debug.traceback)
	if ok then ok, err = xpcall(ikdbg.goalTick, debug.traceback) end
	if not ok and not reported[tostring(err)] then
		reported[tostring(err)] = true
		send('@IKRES', {ok = false, r = 'ikdbg: ' .. tostring(err)})
	end
end)

-- Portraits are animations the scripts make from a character's sprites, with a motif
-- property's parameters: they take that property's source, so the editor can pick them.
if debugAnimSource and start and start.f_animGet then
	local animGet = start.f_animGet
	start.f_animGet = function(ref, side, member, params, ...)
		local a = animGet(ref, side, member, params, ...)
		if a and type(params) == 'table' and params.AnimData then
			pcall(debugAnimSource, a, params.AnimData)
		end
		return a
	end
end

-- The select screen's cells: each item of its draw list gets the grid index of its cell (the
-- nearest), and the motif's animations are drawn marked with it (debugAnimTag), so that the
-- editor knows which cell an outline on the game is.
if debugAnimTag and start and start.updateDrawList and batchDraw then
	local update, draw = start.updateDrawList, batchDraw
	start.updateDrawList = function(...)
		local list = update(...)
		local si = motif.select_info
		for _, item in ipairs(list) do
			local best, bestD = nil, math.huge
			for r, row in ipairs(start.t_grid) do
				for c, t in ipairs(row) do
					local dx, dy = item.x - (si.pos[1] + t.x), item.y - (si.pos[2] + t.y)
					if dx * dx + dy * dy < bestD then
						best, bestD = (r - 1) * si.columns + c, dx * dx + dy * dy
					end
				end
			end
			item.ikdbgCell = best
		end
		list.ikdbgCells = true
		return list
	end
	batchDraw = function(list, ...)
		if type(list) ~= 'table' or not list.ikdbgCells then
			return draw(list, ...)
		end
		for _, item in ipairs(list) do
			debugAnimTag(item.anim, item.ikdbgCell or 0)
			draw({item}, ...)
			debugAnimTag(item.anim, 0)
		end
	end
end

-- Moves P1's cursor to a cell of the select screen (0-based column and row), on one.
function ikdbg.selectCursor(col, row)
	if ikdbg.inSelect and start.c and start.c[1] then
		start.c[1].selX, start.c[1].selY = col, row
	end
end

-- Where the game is, for ikdbg.reloadRoster: in a game mode (from its select screen to its
-- end), on a select screen, choosing a challenger. Each of these functions runs its own loop.
if start then
	local function restore(flag, was, ...)
		ikdbg[flag] = was
		return ...
	end
	for name, flag in pairs({f_selectMode = 'inMode', f_selectScreen = 'inSelect', f_selectChallenger = 'inChallenger'}) do
		local f = start[name]
		if f then
			start[name] = function(...)
				local was = ikdbg[flag]
				ikdbg[flag] = true
				return restore(flag, was, f(...))
			end
		end
	end
end

--;-----------------------------------------------------------
--; Going to a screen and staying on it, to edit what only shows for a moment. The menus are Lua
--; loops: the title's ends in the demo (main.f_demo, held off; it runs only with the title idle, so
--; a goal starts there), the select screen's timer and the versus screen's time are held. What
--; runs in the engine (victory, results, continue, hiscore, storyboards) stops with the pause.
--; Matches are made with launchFight, whose arguments say which of those screens follow.
--; Screens after a match need the game started without -p1 -p2, which exits after its match.
--;-----------------------------------------------------------
local kForever = 2147483647
ikdbg.goal = nil    -- where to go: {screen, team, num, char, stage}
ikdbg.going = nil   -- in a match on the way there: what is left to do
ikdbg.held = nil    -- the screen the game stays on
ikdbg.leaving = false -- on the way back to the title
ikdbg.fighting = false -- the match's hook ran last, not a menu's refresh
local storyFrames, vsTime = 0, nil

local function report()
	send('@IKGO', {held = ikdbg.held or '', goal = ikdbg.goal and ikdbg.goal.screen or ''})
end

local function hold(name)
	ikdbg.held, ikdbg.goal, ikdbg.going = name, nil, nil
	report()
end

-- The debugger saw the moment it was watching for: held that many ticks on, past its fade.
function ikdbg.holdAfter(name, ticks)
	ikdbg.going = ikdbg.going or {screen = name}
	ikdbg.going.screen, ikdbg.going.ticks = name, ticks
end

-- The game stops here, the way a screen is held: the Storyboard window's pause.
function ikdbg.holdNow(name)
	togglePause(true)
	hold(name)
end

-- The game goes on from the screen held.
function ikdbg.letGo()
	if ikdbg.held == 'vs' and vsTime then
		motif.vs_screen.time = vsTime
		vsTime = nil
	end
	ikdbg.held, ikdbg.going = nil, nil
	togglePause(false)
	report()
end

-- "screen|team1|num1|team2|num2|char1|char2|stage"
local function parseGoal(text)
	local f = {}
	for v in (text .. '|'):gmatch('([^|]*)|') do f[#f + 1] = v end
	return {screen = f[1], team = {f[2] or 'single', f[4] or 'single'}, num = {tonumber(f[3]) or 1, tonumber(f[5]) or 1},
		char = {f[6] or '', f[7] or ''}, stage = f[8] or ''}
end

-- A moment of a match: these players' life gone or the time up, once the round is on. The debugger
-- watches for the element and pauses (kill: players; time: time up; menu: the pause menu; restart:
-- at a round's start; full: P1 not hit, so a fresh round too).
local kMoments = {
	round = {restart = true}, fight = {restart = true},
	ko = {kill = {2}}, dko = {kill = {1, 2}}, perfect = {kill = {2}, full = true}, win = {kill = {2}},
	draw = {time = true}, timeover = {time = true}, pause = {menu = true}, -- a draw: the time up, life even
}
-- What follows a match, and who has to lose for it to show
local kAfter = {
	victory = {state = 'victoryscreen', args = {victoryscreen = true}, kill = {2}},
	results = {state = 'winscreen', args = {winscreen = true}, kill = {2}},
	lose = {state = 'winscreen', lose = true, kill = {1}},
	continue = {state = 'continuescreen', args = {continue = true}, kill = {1}},
}

local function storyboardOf(screen)
	if screen:sub(1, 5) == 'file:' then return screen:sub(6) end -- the one the Storyboard window has
	if screen == 'logo' then return motif.files.logo.storyboard end
	if screen == 'intro' then return motif.files.intro.storyboard end
	if screen == 'gameover' then return motif.game_over_screen.storyboard end
	if screen == 'credits' then return motif.end_credits.storyboard end
	if screen == 'ending' then return motif.default_ending.storyboard end
end

-- The match's plan for the frame hook.
local function plan(g, kill)
	ikdbg.going = {screen = g.screen, kill = kill}
	return ikdbg.going
end

local function moment(g, m, inMatch)
	local p = plan(g, m.kill)
	p.time, p.menu, p.full, p.goal = m.time, m.menu, m.full, g
	if inMatch then togglePause(false) end
end

-- A character as the roster names it, whose portraits and animations are read already (a name it
-- does not know would be added apart, without them); not in the roster: its first one.
local function rosterChar(name)
	local wanted = name:lower()
	for _, c in ipairs(main.t_selChars) do
		local own = c.char and c.char:lower()
		if own and (own == wanted or own:match('([^/]+)%.def$') == wanted or own:match('^([^/]+)/') == wanted) then return c.char end
	end
	if #main.t_randomChars == 0 then return name end
	local first = start.f_getCharData(main.t_randomChars[1]).char
	send('@IKRES', {ok = true, r = name .. ' is not in the roster: ' .. first .. ' takes its place'})
	return first
end

-- A match of its own: both sides given, no select screen.
local function fight(g, args)
	return function()
		local ok, err = pcall(function()
			main.f_default()
			setGameMode('arcade')
			main.selectMenu = {false, false}
			main.motif.losescreen = args.lose == true
			setMotifElements(main.motif)
			start.f_selectReset(true)
			main.f_saveBaseRemapInput()
			main.t_availableChars = main.f_tableCopy(start.f_getOrderChars())
			-- The versus screen draws what the roster has read
			if args.vsscreen then
				for side = 1, 2 do
					g.char[side] = rosterChar(g.char[side])
					main.f_preloadBoostChar(start.f_getCharRef(g.char[side]))
				end
				main.f_waitForPreloads(true)
			end
			local p, data = {{}, {}}, {stage = g.stage, vsscreen = args.vsscreen == true, victoryscreen = args.victoryscreen == true,
				winscreen = args.winscreen == true, continue = args.continue == true, p1rounds = 1, p2rounds = 1}
			for side = 1, 2 do
				for n = 1, math.max(g.num[side], 1) do p[side][n] = g.char[side] end
				data['p' .. side .. 'char'] = p[side]
				data['p' .. side .. 'numchars'] = #p[side]
				data['p' .. side .. 'teammode'] = g.team[side]
			end
			launchFight(data)
			setMatchNo(-1)
		end)
		if not ok then send('@IKRES', {ok = false, r = 'going to ' .. g.screen .. ': ' .. tostring(err)}) end
	end
end

-- With the title idle: on the way to the goal.
local function fromTitle()
	local g = ikdbg.goal
	if not g then return end
	local s = g.screen
	if s == 'title' then
		hold(s)
	elseif s == 'select' or s == 'teamselect' then
		local name = s == 'select' and 'versus' or 'teamversus'
		main.f_default()
		main.menu.f = main.t_itemname[name]({{itemname = name}}, 1)
		hold(s)
	elseif s == 'options' then
		main.f_default()
		main.menu.f = main.t_itemname.options()
		hold(s)
	elseif s == 'hiscore' then
		hold(s)
		main.menu.f = function()
			while runHiscore('arcade', -1, kForever) and ikdbg.held == s do
				main.f_preloadTick(2)
				refresh()
			end
			debugHiscoreEnd()
		end
	elseif storyboardOf(s) ~= nil then
		local path = storyboardOf(s)
		if path == '' then
			ikdbg.goal = nil
			send('@IKRES', {ok = false, r = 'the motif names no storyboard for ' .. s})
			return
		end
		g.running = true
		storyFrames = 0
		main.menu.f = function()
			main.f_storyboard(path)
			if ikdbg.goal == g then -- over before it was held: a file that is not there
				ikdbg.goal = nil
				send('@IKRES', {ok = false, r = 'the storyboard ' .. path .. ' could not be shown'})
				report()
			end
		end
	elseif s == 'vs' then
		hold(s)
		main.menu.f = fight(g, {vsscreen = true})
	elseif kAfter[s] then
		local a = kAfter[s]
		plan(g, a.kill).state = a.state
		main.menu.f = fight(g, a.args or {lose = a.lose})
		ikdbg.goal = nil
	elseif kMoments[s] then
		moment(g, kMoments[s], false)
		main.menu.f = fight(g, {})
		ikdbg.goal = nil
	else
		ikdbg.goal = nil
		send('@IKRES', {ok = false, r = 'no screen called ' .. s})
	end
end

function ikdbg.goTo(text)
	local g = parseGoal(text)
	-- A moment of the round under way, unless it needs a fresh one, other teams are asked for or a
	-- moment held has decided it; else a match of its own
	local m = kMoments[g.screen]
	if ikdbg.fighting and m and not m.restart and not m.full and not ikdbg.held and not ikdbg.going and roundState() == 2 and
		g.team[1] == 'single' and g.team[2] == 'single' then
		moment(g, m, true)
		report()
		return
	end
	if ikdbg.held == 'vs' and vsTime then motif.vs_screen.time, vsTime = vsTime, nil end
	ikdbg.held, ikdbg.going, ikdbg.goal = nil, nil, g
	ikdbg.leaving = true
	togglePause(false)
	if ikdbg.fighting then endMatch() end
	report()
end

-- Each frame of a match (the frame hook): the plan under way.
function ikdbg.goalTick()
	ikdbg.fighting = true
	local p = ikdbg.going
	if not p then return end
	if p.ticks then
		p.ticks = p.ticks - 1
		if p.ticks <= 0 then
			togglePause(true)
			hold(p.screen)
		end
		return
	end
	if roundState() == 2 and not p.done then
		p.done = true
		if p.full then onPlayer(1, function() setLife(lifeMax()) end) end
		if p.kill then
			for _, side in ipairs(p.kill) do -- the whole team: a side's players go 1, 3, 5, 7 and 2, 4, 6, 8
				for pn = side, 8, 2 do onPlayer(pn, function() setLife(0) end) end
			end
		end
		if p.time then setTime(1) end
		if p.menu then
			esc(true)
			hold(p.screen)
			return
		end
	end
	if p.state and motifState(p.state) then
		p.shown = (p.shown or 0) + 1
		-- past its fade and what it types
		if p.shown >= 150 then
			togglePause(true)
			hold(p.screen)
		end
	end
end

if main and start then
	local demo, story, timer, draw = main.f_demo, main.f_storyboard, main.f_drawTimer, refresh
	main.f_demo = function()
		-- Before the title's own look at esc(), which would leave the game
		if ikdbg.leaving then
			ikdbg.leaving = false
			esc(false)
		end
		-- A match over before its moment showed: once more in a new one, then it is not coming
		local p = ikdbg.going
		if p then
			ikdbg.going = nil
			if p.goal and not p.goal.again then
				ikdbg.goal, p.goal.again = p.goal, true
			else
				hold('title')
				send('@IKRES', {ok = false, r = p.screen .. ' did not show'})
				return
			end
		end
		if not ikdbg.goal then
			if not ikdbg.held then return demo() end
			return
		end
		local ok, err = pcall(fromTitle)
		if not ok then
			ikdbg.goal = nil
			send('@IKRES', {ok = false, r = 'going to a screen: ' .. tostring(err)})
		end
		report()
	end
	-- Going somewhere: no logo and intro on the way
	main.f_storyboard = function(path, ...)
		if ikdbg.goal and not ikdbg.goal.running then return false end
		return story(path, ...)
	end
	main.f_drawTimer = function(value, params)
		local v, active = timer(value, params)
		if ikdbg.held then return value, active end
		return v, active
	end
	refresh = function(...)
		ikdbg.fighting = false
		if ikdbg.held == 'vs' and motif.vs_screen.time ~= kForever then
			vsTime = motif.vs_screen.time
			motif.vs_screen.time = kForever
		end
		-- A storyboard: past its first fade, then the pause holds it
		local g = ikdbg.goal
		if g and g.running then
			storyFrames = storyFrames + 1
			-- The Storyboard window's says where it wants to be: no fade to wait for
			if storyFrames >= (g.screen:sub(1, 5) == 'file:' and 2 or 60) then
				togglePause(true)
				hold(g.screen)
			end
		end
		draw(...)
		-- After it: a frame's input starts with esc off
		if ikdbg.leaving then esc(true) end
	end
	ikdbg.goal = os.getenv('IKDBG_GOTO') and parseGoal(os.getenv('IKDBG_GOTO')) or nil
end

send('@IKHELLO', {v = ikdbg.version, ik = version(), trace = ikdbg.caps.trace, set = ikdbg.caps.set})
