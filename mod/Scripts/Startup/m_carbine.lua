M4Carbine = M4Carbine or {}
M4Carbine.itemClass = "106b3519-0607-4b29-b7a2-9d8fb554a23e"
M4Carbine.inHand = nil
M4Carbine.projectile = nil

function M4Carbine.CaptureProjectile()
	if M4Carbine.projectile ~= nil then
		return M4Carbine.projectile
	end
	local ok, value = pcall(function() return tonumber(System.GetCVar('wh_w_AimByProjectile')) end)
	M4Carbine.projectile = (ok and value ~= nil) and value or 1
	System.LogAlways("M4CARBINE projectile default " .. tostring(M4Carbine.projectile))
	return M4Carbine.projectile
end

M4Carbine.units = {{x = 1, y = 0, z = 0}, {x = 0, y = 1, z = 0}, {x = 0, y = 0, z = 1}}

function M4Carbine.BoneAxes(player, bone)
	local u = M4Carbine.units
	return player:GetBoneLocal(bone, u[1]), player:GetBoneLocal(bone, u[2]), player:GetBoneLocal(bone, u[3])
end

function M4Carbine.Dude() return M4Carbine.tickPlayer or System.GetEntityByName('Dude') end

local function heldItemId() return M4Carbine.Dude().human:GetItemInHand(0) end

function M4Carbine.ItemInHand()
	local ok, id = pcall(heldItemId)
	if not ok or not id then
		return nil
	end
	local found, item = pcall(ItemManager.GetItem, id)
	return found and item or nil
end

function M4Carbine.HoldsCarbine()
	local item = M4Carbine.ItemInHand()
	return item ~= nil and item.class == M4Carbine.itemClass
end

-- The M4's aim-in and spread buff (prm, lcs, was) is only on while it is in hand, so the
-- handgonne keeps vanilla values. The class row stays self-loading: vanilla cocking (A385)
-- queued a powder-flask load per M4 shot that flicked the hands, and the loading chain's
-- shared ADB speed 60 is out of reach of buffs and SetAnimationSpeed (A384).
M4Carbine.fastBuff = "f0a7c3d2-1234-4567-89ab-cdef01234567"
M4Carbine.steadyBuff = "f0a7c3d2-1234-4567-89ab-cdef01234569"

function M4Carbine.SyncBuff(held)
	pcall(function()
		local soul = M4Carbine.Dude().soul
		soul:RemoveAllBuffsByGuid(M4Carbine.fastBuff)
		soul:RemoveAllBuffsByGuid(M4Carbine.steadyBuff)
		if held then
			soul:AddBuff(M4Carbine.fastBuff)
			soul:AddBuff(M4Carbine.steadyBuff)
		end
	end)
end

function M4Carbine.MarkShotSound(held)
	M4Carbine.MarkTrigger(held and "c_f_m4a1_in_hand" or "c_f_m4a1_out_of_hand", "shot sound " .. (held and "M4" or "vanilla"))
end

function M4Carbine.MarkTrigger(name, label)
	pcall(function()
		local trigger = Sound.GetAudioTriggerID(name)
		System.LogAlways("M4CARBINE " .. label .. " mark=" .. tostring(trigger))
		if trigger then
			local player = M4Carbine.Dude()
			player:ExecuteAudioTrigger(trigger, player:GetDefaultAuxAudioProxyID())
		end
	end)
end

M4Carbine.horseBraveBuff = "61bf5b0d-aa94-45cc-9cdd-dd76d3903189"

function M4Carbine.CalmHorse(player)
	local horse = XGenAIModule.GetEntityByWUID(player.human:GetHorse())
	if not (horse and horse.soul) or horse == M4Carbine.horse then return end
	local last = M4Carbine.horse
	if last then
		pcall(function() last.soul:RemoveAllBuffsByGuid(M4Carbine.horseBraveBuff) end)
	end
	M4Carbine.horse = horse
	horse.soul:RemoveAllBuffsByGuid(M4Carbine.horseBraveBuff)
	horse.soul:AddBuff(M4Carbine.horseBraveBuff)
	System.LogAlways("M4CARBINE horse morale pinned on " .. tostring(horse:GetName()))
end

-- The silent frame mark tells the plugin each frame boundary, so full-auto
-- presses are counted in frames and every shot lands the same frames apart.
-- In the saddle the mounted mark lets the plugin reload on the move there only.
M4Carbine.props = {names = {"powder_flask", "rifle_ignition", "rifle_ramrod"}, radius = 2, before = {}, every = 0.5, at = nil}

function M4Carbine.MarkProps(player)
	M4Carbine.props.before = {}
	for _, entity in pairs(System.GetEntitiesInSphere(player:GetWorldPos(), M4Carbine.props.radius) or {}) do
		M4Carbine.props.before[entity.id] = true
	end
end

function M4Carbine.HideProps(player)
	local props, now = M4Carbine.props, M4Carbine.Now()
	if props.at and now - props.at >= 0 and now - props.at < props.every then return end
	props.at = now
	for _, entity in pairs(System.GetEntitiesInSphere(player:GetWorldPos(), props.radius) or {}) do
		if not props.before[entity.id] and not entity:IsHidden() then
			local name = entity:GetName()
			for _, prop in ipairs(props.names) do
				if string.find(name, prop, 1, true) == 1 then
					entity:Hide(1)
					System.LogAlways("M4CARBINE prop hidden " .. tostring(name))
					break
				end
			end
		end
	end
end

-- A save load sets the game clock back to the save's time (25 s at 13:18:04),
-- and every time the mod had stored then sat in the future: waits stalled and
-- the raise skipped the grid, so a turned aim popped 27 cm (M4QUIRK
-- 2026-10-06). The mod's clock only moves forward.
M4Carbine.clock = {last = nil, offset = 0}

function M4Carbine.Now()
	local clock, t = M4Carbine.clock, System.GetCurrTime()
	if clock.last and t < clock.last then clock.offset = clock.offset + clock.last - t end
	clock.last = t
	return t + clock.offset
end

M4Carbine.waits = {}

function M4Carbine.When(ready, run)
	table.insert(M4Carbine.waits, {ready = ready, run = run})
end

function M4Carbine.At(seconds, run)
	local due = M4Carbine.Now() + seconds
	M4Carbine.When(function() return M4Carbine.Now() >= due end, run)
end

function M4Carbine.RunWaits()
	local waits = M4Carbine.waits
	if #waits == 0 then return end
	M4Carbine.waits = {}
	for _, wait in ipairs(waits) do
		local ok, ready = pcall(wait.ready)
		if ok and ready then
			M4Carbine.Try("wait", wait.run)
		elseif ok then
			table.insert(M4Carbine.waits, wait)
		else
			M4Carbine.Error("wait check", ready)
		end
	end
end

-- Quirks are logged as M4QUIRK lines, always on: a condition that should never
-- hold, held for its grace seconds, logs once until it clears; each name logs at
-- most quirks.cap times a session.
M4Carbine.quirks = {since = {}, seen = {}, count = {}, cap = 20}

function M4Carbine.Note(name, text)
	local q = M4Carbine.quirks
	q.count[name] = (q.count[name] or 0) + 1
	if q.count[name] <= q.cap then
		System.LogAlways(string.format("M4QUIRK %s t=%.3f %s%s", name, M4Carbine.Now(), text, q.count[name] == q.cap and " (last one logged)" or ""))
	end
end

function M4Carbine.Error(name, err)
	M4Carbine.Note("error", name .. ": " .. tostring(err))
end

function M4Carbine.Try(name, fn, ...)
	local ok, err = pcall(fn, ...)
	if not ok then M4Carbine.Error(name, err) end
end

function M4Carbine.Check(name, bad, grace, detail, arg)
	local q, now = M4Carbine.quirks, M4Carbine.Now()
	if not bad then
		q.since[name], q.seen[name] = nil, nil
		return
	end
	q.since[name] = q.since[name] or now
	if not q.seen[name] and now - q.since[name] >= grace then
		q.seen[name] = true
		M4Carbine.Note(name, detail(arg))
	end
end

-- The view's forward, up and right from the aim input. A hit row's
-- AnimateCamera hands the camera to the head bone, which the hold turns, so
-- the camera is no reference for the twist: it chased its own turn to the
-- clamp (M4VIEW 2026-10-06).
function M4Carbine.AimAxes(player)
	local a = player.actor:GetAngles()
	return {x = -math.sin(a.z) * math.cos(a.x), y = math.cos(a.z) * math.cos(a.x), z = math.sin(a.x)},
		{x = math.sin(a.z) * math.sin(a.x), y = -math.cos(a.z) * math.sin(a.x), z = math.cos(a.x)},
		{x = math.cos(a.z), y = math.sin(a.z), z = 0}
end

function M4Carbine.MissYaw(player)
	local sight = M4CarbineSightProfile
	local function dot(a, b) return a.x * b.x + a.y * b.y + a.z * b.z end
	local x, y, z = M4Carbine.BoneAxes(player, 'RightHandSlot')
	local d = {x = sight.front.x - sight.rear.x, y = sight.front.y - sight.rear.y, z = sight.front.z - sight.rear.z}
	local f = {x = dot(x, d), y = dot(y, d), z = dot(z, d)}
	local v, u = System.GetViewCameraDir(), System.GetViewCameraUpDir()
	local r = {x = v.y * u.z - v.z * u.y, y = v.z * u.x - v.x * u.z, z = v.x * u.y - v.y * u.x}
	return math.deg(math.atan2(dot(f, r), dot(f, v)))
end

function M4Carbine.WatchState(player)
	local o = M4Carbine.overlay
	local L = ''
	for l = 0, 15 do L = L .. (player:IsAnimationRunning(0, l) and '1' or '0') end
	return string.format("st=%s ap=%s idle=%s aim=%s drawn=%s hand=%s holster=%s mnt=%s cr=%s L=%s",
		tostring(o.state), tostring(o.aimPlaying), tostring(o.idleClip), tostring(o.aimHeld), tostring(player.human:IsWeaponDrawn()),
		tostring(M4Carbine.inHand), tostring(o.holstering), tostring(M4Carbine.Mounted()), tostring(M4Carbine.Crouched()), L)
end

-- A snap is one frame that moves the gun against the body, or the left hand along
-- the gun, popLimit metres and spike times more than the frames either side of it:
-- a fast but smooth raise or holster is no snap. A hit logs M4HIT frames for
-- hitSeconds with what the overlay and layers did.
M4Carbine.watch = {popLimit = 0.04, spike = 3, hitSeconds = 3, hitAt = -1, gun = nil, grip = nil, moves = {{}, {}, {}}, filled = 0, last = 3, gripBuf = {},
	names = {gun = "gun snapped", grip = "left hand slid on the gun"}}

function M4Carbine.WaitsText(waits) return "waits=" .. #waits end

function M4Carbine.Watch(player)
	local o, watch, check = M4Carbine.overlay, M4Carbine.watch, M4Carbine.Check
	local now, held = M4Carbine.Now(), M4Carbine.inHand
	local drawn = player.human:IsWeaponDrawn()
	local state = M4Carbine.WatchState
	check("overlay stopped", (o.state == "on" or o.state == "aim") and not player:IsAnimationRunning(0, o.layer), 0.1, state, player)
	check("hold missing", held and drawn and not o.holstering and not o.aimHeld and o.state ~= "on", 1.5, state, player)
	check("overlay on another weapon", o.state ~= nil and not held, o.holsterBlend + 0.3, state, player)
	check("aim never raised", held and drawn and o.aimHeld and o.state ~= "aim" and not M4Carbine.Drawing(), 1, state, player)
	check("aim stuck raised", o.state == "aim" and not o.aimHeld, 0.5, state, player)
	check("holster stuck", held and o.holstering, 2, state, player)
	check("eye layer off the saddle", o.eyeOn and not M4Carbine.Mounted(), 0.3, state, player)
	check("camera owned off the saddle", o.camOwned and not M4Carbine.Mounted(), 0.1, state, player)
	local aimOff = false
	if held and not M4Carbine.Mounted() then
		local cv, av = System.GetViewCameraDir(), M4Carbine.AimAxes(player)
		aimOff = cv.x * av.x + cv.y * av.y + cv.z * av.z < 0.9986
	end
	check("camera off the aim", aimOff, 0.2, state, player)
	check("waits piling up", #M4Carbine.waits > 30, 0, M4Carbine.WaitsText, M4Carbine.waits)
	local dt = System.GetFrameTime()
	if not held or dt <= 0 or dt > 0.1 then
		watch.gun, watch.grip, watch.filled = nil, nil, 0
		return
	end
	local rs, lh = player:GetBonePos('RightHandSlot'), player:GetBonePos('LeftHand')
	local w = player:GetWorldPos()
	local gun = player:VectorToLocal(0, {x = rs.x - w.x, y = rs.y - w.y, z = rs.z - w.z})
	local x, y, z = M4Carbine.BoneAxes(player, 'RightHandSlot')
	local ex, ey, ez = lh.x - rs.x, lh.y - rs.y, lh.z - rs.z
	local gx, gy, gz = x.x * ex + y.x * ey + z.x * ez, x.y * ex + y.y * ey + z.y * ez, x.z * ex + y.z * ey + z.z * ez
	local pg, pr = watch.gun, watch.grip
	local m = watch.moves
	local k = watch.last % 3 + 1
	local frame = m[k]
	frame.gun = pg and math.sqrt((gun.x - pg.x) ^ 2 + (gun.y - pg.y) ^ 2 + (gun.z - pg.z) ^ 2) or 0
	frame.grip = pr and math.sqrt((gx - pr.x) ^ 2 + (gy - pr.y) ^ 2 + (gz - pr.z) ^ 2) or 0
	frame.shot, frame.dt = now - M4Carbine.wall.shotAt < 0.2, dt
	frame.at, frame.p, frame.turn = player:GetAnimationTime(0, o.layer) or -1, o.raiseP or -1, o.twist * (o.twistW or 0)
	frame.state = nil
	-- Only a frame that moved past popLimit can be reported, so only it pays
	-- for the state text: built every frame it was half the script's garbage.
	if frame.gun > watch.popLimit or frame.grip > watch.popLimit then frame.state = state(player) end
	watch.last = k
	if watch.filled < 3 then watch.filled = watch.filled + 1 end
	if watch.filled == 3 then
		local m1, m2, m3 = m[k % 3 + 1], m[(k + 1) % 3 + 1], frame
		for what, name in pairs(watch.names) do
			local a, b, c = m1[what], m2[what], m3[what]
			local bad = b > watch.popLimit and b > watch.spike * math.max(a, c)
			check(name, bad, 0, bad and function()
				-- The overlay clip time, raise progress and turn on each of the three
				-- frames tell a grid step from a twist jump or a stalled tick.
				local f, mm = {}, {m1, m2, m3}
				for i = 1, 3 do f[i] = string.format("%.0fms at=%.4f p=%.2f turn=%.1f", mm[i].dt * 1000, mm[i].at, mm[i].p, mm[i].turn) end
				return string.format("%.1f cm in %.0f ms (frames around %.1f, %.1f) shot=%s %s frames %s", b * 100, m2.dt * 1000, a * 100, c * 100, tostring(m2.shot), m2.state, table.concat(f, " | "))
			end)
		end
	end
	if M4Carbine.trace and now >= watch.hitAt and now - watch.hitAt < watch.hitSeconds then
		local a = player:GetWorldAngles()
		System.LogAlways(string.format("M4HIT t=%.3f gun=%.1f grip=%.1f cm twist=%.1f miss=%.1f hips=%.1f yaw=%.1f at=%.3f l0=%.3f %s", now, frame.gun * 100, frame.grip * 100,
			o.twist, M4Carbine.MissYaw(player), M4Carbine.HipsYaw(player), math.deg(a.z), player:GetAnimationTime(0, o.layer) or -1, player:GetAnimationTime(0, 0) or -1, frame.state or state(player)))
	end
	local g = watch.gripBuf
	g.x, g.y, g.z = gx, gy, gz
	watch.gun, watch.grip = gun, g
end

M4Carbine.frameNo = M4Carbine.frameNo or 0

function M4Carbine.StartFrames()
	M4Carbine.frameToken = (M4Carbine.frameToken or 0) + 1
	M4Carbine.mountedMemo.frame = -1
	local token = M4Carbine.frameToken
	M4Carbine.frameNext = function() M4Carbine.FrameTick(token) end
	M4Carbine.counterNext = function() M4Carbine.CounterFrame(token) end
	M4Carbine.Try("ListenToMenu", M4Carbine.ListenToMenu)
	M4Carbine.FrameTick(M4Carbine.frameToken)
	M4Carbine.CounterFrame(token)
end

function M4Carbine.CounterFrame(token)
	if token ~= M4Carbine.frameToken then return end
	M4Carbine.Try("CounterTick", M4Carbine.CounterTick)
	Script.SetTimer(0, M4Carbine.counterNext)
end

function M4Carbine.FrameTick(token)
	if token ~= M4Carbine.frameToken then return end
	M4Carbine.frameNo = M4Carbine.frameNo + 1
	M4Carbine.overlay.tickAt = M4Carbine.Now()
	M4Carbine.crouchedMemo.open = true
	local player = System.GetEntityByName('Dude')
	M4Carbine.tickPlayer = player
	M4Carbine.Try("Sync", M4Carbine.Sync)
	M4Carbine.Try("ZoomTick", M4Carbine.ZoomTick)
	M4Carbine.Try("SyncLookIK", M4Carbine.SyncLookIK, M4Carbine.inHand == true)
	M4Carbine.Try("MagTick", M4Carbine.MagTick)
	M4Carbine.Try("MagDropsTick", M4Carbine.MagDropsTick)
	M4Carbine.Try("SyncBack", M4Carbine.SyncBack)
	M4Carbine.Try("DollTick", M4Carbine.DollTick)
	M4Carbine.Try("PoseTick", M4Carbine.PoseTick)
	M4Carbine.Try("RunWaits", M4Carbine.RunWaits)
	M4Carbine.Try("FrameMarks", M4Carbine.FrameMarks, player)
	M4Carbine.Try("FingerTick", M4Carbine.FingerTick, player)
	M4Carbine.Try("TriggerTick", M4Carbine.TriggerTick)
	M4Carbine.Try("LightTick", M4Carbine.LightTick)
	M4Carbine.Try("Watch", M4Carbine.Watch, player)
	M4Carbine.crouchedMemo.open = false
	M4Carbine.tickPlayer = nil
	Script.SetTimer(1, M4Carbine.frameNext)
end

function M4Carbine.FrameMarks(player)
	if M4Carbine.inHand then
		local mounted = M4Carbine.Mounted()
		local name = mounted and "c_f_m4a1_frame_mounted" or "c_f_m4a1_frame"
		M4Carbine.frameMarks = M4Carbine.frameMarks or {}
		M4Carbine.frameMarks[name] = M4Carbine.frameMarks[name] or Sound.GetAudioTriggerID(name)
		local mark = M4Carbine.frameMarks[name]
		if player and mark then
			player:ExecuteAudioTrigger(mark, player:GetDefaultAuxAudioProxyID())
		end
		if player then
			M4Carbine.Try("StanceSwap", M4Carbine.StanceSwap, player, true)
			M4Carbine.Try("PushGuard", M4Carbine.PushGuard)
			M4Carbine.Try("GridScrub", M4Carbine.GridScrub, player)
			M4Carbine.Try("StanceScrub", M4Carbine.StanceScrub, player)
			M4Carbine.Try("MountScrub", M4Carbine.MountScrub, player)
			M4Carbine.Try("RollReference", M4Carbine.RollReference, player)
			M4Carbine.Try("HideProps", M4Carbine.HideProps, player)
			M4Carbine.Try("HitTick", M4Carbine.HitTick, player)
			M4Carbine.Try("MuzzleFlash", M4Carbine.MuzzleFlash, player)
		end
	else
		M4Carbine.flash.count = nil
	end
	M4Carbine.Try("CalmHorse", M4Carbine.CalmHorse, player)
	M4Carbine.Try("BackFrame", M4Carbine.BackFrame, player)
	M4Carbine.Try("HeadHitTick", M4Carbine.HeadHitTick)
	M4Carbine.Try("HeadshotTick", M4Carbine.HeadshotTick)
	M4Carbine.Try("MountCamera", M4Carbine.MountCamera, player)
	M4Carbine.traceFrame = (M4Carbine.traceFrame or 0) + 1
	if M4Carbine.trace and M4Carbine.inHand and (M4Carbine.overlay.state ~= "on" or M4Carbine.traceFrame % 4 == 0) then M4Carbine.Try("TraceAim", M4Carbine.TraceAim, player) end
end

-- Every round the M4 spends spawns its own muzzle flash (Libs/Particles/m4a1.xml) at
-- the muzzle: the front sight carried ahead along the sight line and down to the bore.
-- The M4's event file never plays (A718), and the ammo count drops on every real shot.
M4Carbine.flash = {effect = "m4a1.muzzle_flash", ammo = "ef51da7c-92e7-4e56-8c7f-3d22a27fe9a2", ahead = 0.23, drop = 0.07, scale = 1}

local function flashPoint(origin, x, y, z, v)
	return {x = origin.x + x.x * v.x + x.y * v.y + x.z * v.z, y = origin.y + y.x * v.x + y.y * v.y + y.z * v.z, z = origin.z + z.x * v.x + z.y * v.y + z.z * v.z}
end

function M4Carbine.FlashAt(player)
	local f, sight = M4Carbine.flash, M4CarbineSightProfile
	local origin = player:GetBonePos('RightHandSlot')
	local x, y, z = M4Carbine.BoneAxes(player, 'RightHandSlot')
	local rear, front = flashPoint(origin, x, y, z, sight.rear), flashPoint(origin, x, y, z, sight.front)
	local dx, dy, dz = front.x - rear.x, front.y - rear.y, front.z - rear.z
	local n = math.sqrt(dx * dx + dy * dy + dz * dz)
	dx, dy, dz = dx / n, dy / n, dz / n
	local u = System.GetViewCameraUpDir()
	local along = u.x * dx + u.y * dy + u.z * dz
	local ux, uy, uz = u.x - dx * along, u.y - dy * along, u.z - dz * along
	n = math.sqrt(ux * ux + uy * uy + uz * uz)
	ux, uy, uz = ux / n, uy / n, uz / n
	return {x = front.x + dx * f.ahead - ux * f.drop, y = front.y + dy * f.ahead - uy * f.drop, z = front.z + dz * f.ahead - uz * f.drop}, {x = dx, y = dy, z = dz}
end

function M4Carbine.MuzzleFlash(player)
	local f = M4Carbine.flash
	local count = player.inventory:GetCountOfClass(f.ammo) or 0
	local spent = f.count and f.count - count or 0
	f.count = count
	if spent < 1 or spent > 3 then return end
	M4Carbine.MagSpent(spent)
	M4Carbine.finger.pressAt = M4Carbine.Now()
	local at, dir = M4Carbine.FlashAt(player)
	Particle.SpawnEffect(f.effect, at, dir, f.scale)
	M4Carbine.Try("RoundHit", M4Carbine.RoundHit, player)
	System.LogAlways(string.format("M4FLASH spent=%d at=%.3f,%.3f,%.3f dir=%.3f,%.3f,%.3f", spent, at.x, at.y, at.z, dir.x, dir.y, dir.z))
end

M4Carbine.headshot = {bone = "Head", reach = 0.18, boidDamage = 100, seconds = 1, range = 300, types = 1 + 2 + 4 + 8 + 16 + 0x100, marked = {},
	cvar = "m4_head_hit", near = 1.5, search = 3.5}

function M4Carbine.RoundHit(player)
	local shot = M4Carbine.headshot
	local from, dir = System.GetViewCameraPos(), System.GetViewCameraDir()
	local hits = {}
	local n = Physics.RayWorldIntersection(from, {x = dir.x * shot.range, y = dir.y * shot.range, z = dir.z * shot.range}, 1, shot.types, player.id, nil, hits)
	if type(n) == "table" then hits, n = n, #n end
	local hit = (n or 0) > 0 and hits[1] or nil
	local target = hit and hit.entity
	if target and target.class == "Boid" and target.Server and target.Server.OnHit then
		target.Server.OnHit(target, {pos = hit.pos, dir = dir, damage = shot.boidDamage, shooter = player})
		return
	end
	if M4Carbine.HeadHit() then return end
	if not (target and target.actor) then return end
	local head = target:GetBonePos(shot.bone)
	if not head then return end
	local hx, hy, hz = head.x - from.x, head.y - from.y, head.z - from.z
	local cx, cy, cz = hy * dir.z - hz * dir.y, hz * dir.x - hx * dir.z, hx * dir.y - hy * dir.x
	if cx * cx + cy * cy + cz * cz > shot.reach * shot.reach then return end
	M4Carbine.HeadshotMark(target)
end

function M4Carbine.HeadshotMark(target)
	if target.actor:IsDead() then
		target.actor:RagDollize()
		System.LogAlways("M4CARBINE headshot ragdoll " .. target:GetName() .. " on a dead body")
		return
	end
	M4Carbine.headshot.marked[target.id] = {entity = target, at = M4Carbine.Now()}
end

function M4Carbine.HeadHit()
	local ok, value = pcall(System.GetCVar, M4Carbine.headshot.cvar)
	if not ok or value == nil then return nil end
	value = tostring(value)
	local x, y, z = string.match(value, "^%s*%d+%s+(%S+)%s+(%S+)%s+(%S+)")
	x, y, z = tonumber(x), tonumber(y), tonumber(z)
	return tonumber(string.match(value, "^%s*(%d+)")) or 0, x and y and z and {x = x, y = y, z = z} or nil
end

function M4Carbine.HeadHitActor(at)
	local shot, dude = M4Carbine.headshot, M4Carbine.Dude()
	local best, bestD = nil, shot.near * shot.near
	for _, entity in pairs(System.GetEntitiesInSphere(at, shot.search) or {}) do
		if entity.actor and entity ~= dude then
			local p = entity:GetBonePos(shot.bone) or entity:GetWorldPos()
			local dx, dy, dz = p.x - at.x, p.y - at.y, p.z - at.z
			local d = dx * dx + dy * dy + dz * dz
			if d <= bestD then best, bestD = entity, d end
		end
	end
	return best
end

function M4Carbine.HeadHitTick()
	local shot = M4Carbine.headshot
	local seq, at = M4Carbine.HeadHit()
	if seq == nil or shot.seq and seq <= shot.seq then return end
	local fresh = shot.seq ~= nil
	shot.seq = seq
	if not (fresh and at) then return end
	local target = M4Carbine.HeadHitActor(at)
	System.LogAlways(string.format("M4CARBINE head hit %d at %.3f,%.3f,%.3f on %s", seq, at.x, at.y, at.z, target and target:GetName() or "no actor"))
	if target then M4Carbine.HeadshotMark(target) end
end

function M4Carbine.HeadshotTick()
	local shot, now = M4Carbine.headshot, M4Carbine.Now()
	for id, mark in pairs(shot.marked) do
		local actor = System.GetEntity(id) == mark.entity and mark.entity.actor
		if not actor or now - mark.at > shot.seconds then
			shot.marked[id] = nil
		elseif actor:IsDead() then
			shot.marked[id] = nil
			actor:RagDollize()
			System.LogAlways(string.format("M4CARBINE headshot ragdoll %s %.3f s after the hit", mark.entity:GetName(), now - mark.at))
		end
	end
end

M4Carbine.finger = {clip = "rifle_sprint_fingers_open_add", layer = 12, blendIn = 0.04, blendOut = 0.08, after = 0.15, held = false, pressAt = -1, on = false, playing = false}
M4Carbine.tap = {lift = "settle_medium_01_additive_hand", press = "settle_medium_01_additive_upper", liftFor = 0.08, pressFor = 0.07, holdFor = 0.06, blendOut = 0.12, at = -1}

function M4Carbine.Reloading()
	local model = M4Carbine.magModel
	return model.entity ~= nil or model.endedAt ~= nil
end

function M4Carbine.FingerAimed()
	return M4Carbine.inHand and M4Carbine.overlay.state == "aim" and not M4Carbine.Reloading() or false
end

function M4Carbine.FingerTick(player)
	local finger, tap = M4Carbine.finger, M4Carbine.tap
	if not M4Carbine.inHand then finger.held = false end
	local aimed, now = M4Carbine.FingerAimed(), M4Carbine.Now()
	finger.on = (aimed and (finger.held or now - finger.pressAt < finger.after)) or false
	local clip, blend = false, finger.playing == finger.clip and finger.blendOut or tap.blendOut
	if finger.on then
		clip, blend = finger.clip, finger.blendIn
	elseif aimed and now - tap.at < tap.liftFor then
		clip, blend = tap.lift, tap.liftFor
	elseif aimed and now - tap.at < tap.liftFor + tap.pressFor + tap.holdFor then
		clip, blend = tap.press, tap.pressFor
	end
	if clip == finger.playing then return end
	finger.playing = clip
	if M4Carbine.Reloading() then return end
	player:StartAnimation(0, clip or M4Carbine.overlay.blank, finger.layer, blend, 1, clip ~= false)
end

M4Carbine.trigger = {cgf = "objects/manmade/weapons/firearms/m4_trigger.cgf", slot = 5, pin = {x = 0.0541092, y = -0.0287817, z = 0.2016320}, angles = {x = -0.074543, y = -0.143317, z = 0.037785}, w = 0, entity = nil, drawn = {}}

function M4Carbine.TriggerModel(gun)
	local trigger = M4Carbine.trigger
	if not gun or trigger.drawn[tostring(gun.id)] then return end
	trigger.drawn[tostring(gun.id)] = true
	gun:LoadObject(trigger.slot, trigger.cgf)
	gun:SetSlotPos(trigger.slot, trigger.pin)
	System.LogAlways("M4TRIGGER drawn on " .. tostring(gun.id))
end

function M4Carbine.TriggerTick()
	local trigger, finger = M4Carbine.trigger, M4Carbine.finger
	local gun = M4Carbine.inHand and M4Carbine.MagGun() or nil
	M4Carbine.TriggerModel(gun)
	M4Carbine.TriggerModel(M4Carbine.back.entity and System.GetEntity(M4Carbine.back.entity))
	if not finger.on and trigger.entity == nil then return end
	if trigger.entity and (gun == nil or gun.id ~= trigger.entity) then
		local was = System.GetEntity(trigger.entity)
		if was then
			was:SetSlotPos(trigger.slot, trigger.pin)
			was:SetSlotAngles(trigger.slot, {x = 0, y = 0, z = 0})
		end
		trigger.entity, trigger.w = nil, 0
	end
	if not gun then return end
	local dt = System.GetFrameTime()
	local w = finger.on and math.min(1, trigger.w + dt / finger.blendIn) or math.max(0, trigger.w - dt / finger.blendOut)
	if w == trigger.w and trigger.entity == gun.id then return end
	trigger.w, trigger.entity = w, w > 0 and gun.id or nil
	local a = trigger.angles
	gun:SetSlotPos(trigger.slot, trigger.pin)
	gun:SetSlotAngles(trigger.slot, {x = a.x * w, y = a.y * w, z = a.z * w})
end

M4Carbine.light = {glow = "m4_light_glow", characters = {0, 2}, lampSlot = 8, pos = {x = 0.0920367, y = 0.0835886, z = 0.5769692}, angles = {x = 0.0, y = -1.3341623, z = 1.6999993}, keys = {attack_special = true, grab_body = true, stealth_kill = true, knock_out = true, horse_pulldown = true, mercy_kill = true, butcher = true}, debounce = 0.15, pressAt = -1, switchAt = -1, on = false, lit = nil, archetype = "Lights.Standard.Light_Lantern", texture = "textures/projectors/round_02_diff.dds", radius = 25, fov = 50, colour = {x = 1, y = 0.95, z = 0.86}, power = 1.5, fog = 1}

function M4Carbine.LightGlow(gun, on)
	local light = M4Carbine.light
	for _, slot in ipairs(light.characters) do gun:HideAttachment(slot, light.glow, not on, not on) end
end

function M4Carbine.LightPress(action, activation)
	local light = M4Carbine.light
	if activation ~= "press" or not light.keys[action] or not (M4Carbine.inHand and M4Carbine.overlay.aimHeld) then return end
	local now = M4Carbine.Now()
	if now - light.pressAt < light.debounce then return end
	light.pressAt, light.on, light.switchAt = now, not light.on, now
	if M4Carbine.FingerAimed() then
		local tap = M4Carbine.tap
		tap.at, light.switchAt = now, now + tap.liftFor + tap.pressFor
	end
	light.clickAt = light.switchAt
	System.LogAlways(string.format("M4LIGHT %s by %s", light.on and "on" or "off", action))
end

function M4Carbine.LightDesc()
	local light = M4Carbine.light
	if type(Light) ~= "table" or type(Light.GetLightDesc) ~= "function" then Script.ReloadScript("Scripts/Entities/Lights/Light.lua") end
	local lt = Light.GetLightDesc(System.GetArchetypeProperties(light.archetype) or Light.Properties, light.lampSlot)
	local c, power = light.colour, light.power
	lt.radius = light.radius
	lt.diffuse_color = {x = c.x * power, y = c.y * power, z = c.z * power}
	lt.specular_multiplier = 1
	lt.volumetric_multiplier_wh = light.fog
	lt.projector_texture = light.texture
	lt.proj_fov = light.fov
	lt.cast_shadow = 0
	lt.flare_enable = false
	lt.ambient = false
	lt.fake = false
	lt.area_light = false
	lt.style = 0
	return lt
end

function M4Carbine.LightTick()
	local light = M4Carbine.light
	local gun = M4Carbine.inHand and M4Carbine.MagGun() or nil
	if not gun then light.on, light.clickAt = false, nil end
	if light.clickAt and M4Carbine.Now() >= light.clickAt then
		light.clickAt = nil
		M4Carbine.MarkTrigger("c_f_m4a1_light", "light click")
	end
	local want = light.on and gun.id or nil
	if want == light.lit or (gun and M4Carbine.Now() < light.switchAt) then return end
	local was = light.lit and System.GetEntity(light.lit)
	if was then
		was:FreeSlot(light.lampSlot)
		M4Carbine.LightGlow(was, false)
	end
	light.lit = want
	if not want then return end
	M4Carbine.LightGlow(gun, true)
	gun:LoadLight(light.lampSlot, M4Carbine.LightDesc())
	gun:SetSlotPos(light.lampSlot, light.pos)
	gun:SetSlotAngles(light.lampSlot, light.angles)
	System.LogAlways("M4LIGHT lit on " .. tostring(gun.id))
end

-- The M4 feeds from 30-round magazines drawn from the 5.56 in the inventory. The
-- script counts the rounds a magazine holds down with each spent round and tells
-- the native plugin, which counts its own shots down from that and refuses the
-- fire press of an empty magazine. An empty magazine reloads by itself; the torch
-- key reloads a partial one. A reload holds fire for reloadSeconds and loads what
-- the inventory still has, up to size. A holster drops a reload in progress.
-- On-screen text for the count spammed the screen (A742). The mod's own UI
-- element M4Counter (Libs/UI/UIElements/M4Counter.xml, M4Counter.gfx) shows the
-- rounds in the magazine / rounds left at the bottom of the screen while the M4
-- is in hand; the vanilla hud.gfx is not touched.
-- The loaded count lives in the game global variable m4_mag_rounds, which the game
-- saves: Lua state survives a load in the same session, and the player's AI table
-- and an item's condition cannot carry it (probes 2026-10-07). A save from before
-- the magazines reads 0, so its first draw reloads.
M4Carbine.mag = M4Carbine.mag or {size = 30, rounds = nil, reloading = false, token = 0, key = "toggle_torch", saved = "m4_mag_rounds"}

function M4Carbine.MagStore(rounds)
	local mag = M4Carbine.mag
	mag.rounds = rounds
	pcall(Variables.SetGlobal, mag.saved, rounds)
end

function M4Carbine.MagStock()
	local player = M4Carbine.Dude()
	return player and player.inventory:GetCountOfClass(M4Carbine.flash.ammo) or 0
end

M4Carbine.counter = M4Carbine.counter or {
	element = "M4Counter", instance = 0, shown = false, broken = false, slotHidden = false,
	barInside = "_root.bc.mc_StatsContainer.qamWeapon.inside", barScale = 0.5, barHome = 300, barPitch = 99, barSlotUnits = 100,
	barSlot = 0, barOffset = 0, barAlpha = 100, barSlotAlpha = 100, choiceSeconds = 0.35, ammoCvar = "m4_qam_ammo",
}
M4Carbine.vanillaWeaponSlot = {"_root", "_root.bc", "_root.bc.mc_StatsContainer", "_root.bc.mc_StatsContainer.qamWeapon"}

function M4Carbine.CounterBarAlpha(path)
	local ok, alpha = pcall(UIAction.GetAlpha, "hud", 0, path)
	return ok and tonumber(alpha) or nil
end

function M4Carbine.CounterBarRest(x)
	local counter = M4Carbine.counter
	for s = 0, 3 do
		if math.abs(x - counter.barHome + counter.barPitch * s) < 0.5 or math.abs(x - counter.barHome + counter.barSlotUnits * s) < 0.5 then
			local below = s == 0 and 0 or M4Carbine.CounterBarAlpha(counter.barInside .. ".slot" .. (s - 1))
			if below == nil or (below > 0.5 and below < 99.5) then return end
			counter.barRestX = x
			if below > 0.5 then return end
			counter.restSlot, counter.restX = s, x
			if M4Carbine.inHand == true and counter.choice ~= false then counter.barSlot, counter.barKnown = s, true end
			return
		end
	end
end

function M4Carbine.CounterSlide()
	local counter = M4Carbine.counter
	local now = M4Carbine.Now()
	if counter.barRetryAt and now < counter.barRetryAt then return end
	local ok, pos = pcall(UIAction.GetPos, "hud", 0, counter.barInside)
	local x = ok and type(pos) == "table" and tonumber(pos.x) or nil
	if x == nil then
		counter.barRetryAt = now + 1
		if not counter.barWarned then
			counter.barWarned = true
			System.LogAlways("M4CARBINE counter bar unreadable, the counter stays over the weapon slot")
		end
		return
	end
	local moved = x ~= counter.barX
	counter.barX, counter.barMoving, counter.barWasMoving = x, moved, counter.barMoving
	if x ~= counter.barRestX then M4Carbine.CounterBarRest(x) end
	local offset = counter.barScale * (x - counter.barHome + counter.barPitch * counter.barSlot)
	if counter.barOffset == nil or math.abs(offset - counter.barOffset) >= 0.05 then
		counter.barOffset = offset
		pcall(UIAction.CallFunction, counter.element, counter.instance, "SetSlide", offset)
	end
	if moved or counter.barWasMoving or counter.barAlpha == nil then
		counter.barSlotAlpha = M4Carbine.CounterBarAlpha(counter.barInside .. ".slot" .. counter.barSlot) or 100
	end
	local alpha = M4Carbine.CounterBarAlpha(counter.barInside)
	alpha = alpha and alpha * counter.barSlotAlpha / 100
	if alpha ~= nil and (counter.barAlpha == nil or math.abs(alpha - counter.barAlpha) >= 0.5) then
		counter.barAlpha = alpha
		pcall(UIAction.CallFunction, counter.element, counter.instance, "SetFade", alpha)
	end
end

function M4Carbine.CounterShow(text)
	local counter = M4Carbine.counter
	counter.text = text
	if counter.shown then
		pcall(UIAction.CallFunction, counter.element, counter.instance, "SetText", text)
	end
end

function M4Carbine.VanillaWeaponSlotVisible()
	for _, clip in ipairs(M4Carbine.vanillaWeaponSlot) do
		local ok, visible = pcall(UIAction.IsVisible, "hud", 0, clip)
		if ok and visible == false then return false end
	end
	return true
end

function M4Carbine.CounterHide()
	local counter = M4Carbine.counter
	if not counter.shown then return end
	counter.shown = false
	pcall(UIAction.HideElement, counter.element, counter.instance)
end

function M4Carbine.CounterChoose(choice)
	local counter = M4Carbine.counter
	counter.choice, counter.choiceAt = choice, M4Carbine.Now()
	M4Carbine.Try("CounterTick", M4Carbine.CounterTick)
end

function M4Carbine.CounterPress(action)
	local counter = M4Carbine.counter
	if not counter.barKnown then return false end
	local choice = action == "action_qam_" .. (counter.barSlot + 1)
	if not choice and M4Carbine.inHand == true then return true end
	M4Carbine.CounterChoose(choice)
	return false
end

function M4Carbine.CounterLoaded()
	local counter = M4Carbine.counter
	local ok, value = pcall(System.GetCVar, counter.ammoCvar)
	local loaded, inHandRow = string.match(ok and tostring(value) or "", "^(%-?%d+) (%-?%d+)$")
	loaded, inHandRow = tonumber(loaded), tonumber(inHandRow)
	if not loaded or loaded < 0 then return true end
	local row = M4Carbine.inHand == true and inHandRow >= 0 and inHandRow or counter.barKnown and counter.barSlot
	if not row then return true end
	return math.floor(loaded / 2 ^ row) % 2 == 1
end

function M4Carbine.CounterTick()
	local counter = M4Carbine.counter
	local inHand = M4Carbine.inHand == true
	if inHand ~= counter.inHand then counter.inHand, counter.barRestX = inHand, nil end
	local onM4Slot = counter.barKnown and counter.restSlot == counter.barSlot
	local choice = counter.choice
	if choice == true and inHand
		or choice == false and not inHand and not onM4Slot
		or choice ~= nil and counter.barX == counter.restX and M4Carbine.Now() - counter.choiceAt > counter.choiceSeconds and choice ~= onM4Slot then
		counter.choice = nil
	end
	local picked = counter.choice
	if picked == nil then picked = inHand or (M4Carbine.back.entity ~= nil and onM4Slot) end
	local want = counter.text ~= nil and not counter.broken and picked and M4Carbine.CounterLoaded()
	local slotHidden = not M4Carbine.VanillaWeaponSlotVisible()
	if want ~= counter.shown then
		if not want then return M4Carbine.CounterHide() end
		local ok, result = pcall(UIAction.ShowElement, counter.element, counter.instance)
		counter.shown, counter.broken = ok, not ok
		System.LogAlways("M4CARBINE counter show " .. tostring(ok) .. " " .. tostring(result))
		if not ok then return end
		if counter.barOffset ~= 0 then counter.barOffset = nil end
		if counter.barAlpha ~= 100 then counter.barAlpha = nil end
		M4Carbine.MagShow()
	end
	if slotHidden ~= counter.slotHidden then
		counter.slotHidden, counter.barAlpha = slotHidden, nil
		if slotHidden then pcall(UIAction.CallFunction, counter.element, counter.instance, "SetFade", 0) end
	end
	if not slotHidden then M4Carbine.CounterSlide() end
end

M4Carbine.menuListener = M4Carbine.menuListener or {}

function M4Carbine.menuListener.OnHide()
	M4Carbine.Try("CounterTick", M4Carbine.CounterTick)
end

function M4Carbine.ListenToMenu()
	local listener = M4Carbine.menuListener
	if listener.registered then return end
	listener.registered = pcall(UIAction.RegisterElementListener, listener, "Menu", -1, "OnHide", "OnHide")
end

function M4Carbine.MagShow()
	local mag = M4Carbine.mag
	if not mag.rounds then return end
	local loaded = mag.reloading and 0 or mag.rounds
	M4Carbine.CounterShow(string.format("%d/%d", loaded, math.max(0, M4Carbine.MagStock() - loaded)))
end

function M4Carbine.MagSend()
	local mag = M4Carbine.mag
	System.LogAlways("M4CARBINE auto mag " .. ((mag.reloading or not mag.rounds) and 0 or mag.rounds))
	M4Carbine.MagShow()
end

function M4Carbine.MagDrawn(held)
	local mag = M4Carbine.mag
	if not held then
		mag.reloading, mag.token = false, mag.token + 1
		return
	end
	M4Carbine.counter.broken = false
	local ok, saved = pcall(Variables.GetGlobal, mag.saved)
	M4Carbine.MagStore(math.min(ok and tonumber(saved) or 0, M4Carbine.MagStock()))
	M4Carbine.MagSend()
	if mag.rounds == 0 then M4Carbine.MagReload() end
end

function M4Carbine.MagSpent(spent)
	local mag = M4Carbine.mag
	if not M4Carbine.inHand or not mag.rounds then return end
	M4Carbine.MagStore(math.max(0, mag.rounds - spent))
	M4Carbine.MagShow()
	if mag.rounds == 0 then M4Carbine.MagReload() end
end

function M4Carbine.MagReload()
	local mag, model = M4Carbine.mag, M4Carbine.magModel
	if mag.reloading or model.held or not M4Carbine.inHand or not mag.rounds or mag.rounds >= mag.size then return end
	if M4Carbine.MagStock() <= mag.rounds then return end
	local scene = mag.rounds == 0 and model.empty or model.partial
	model.playing = M4Carbine.reloads[scene]
	mag.reloading, mag.token = true, mag.token + 1
	local token = mag.token
	M4Carbine.MagSend()
	System.LogAlways(string.format("M4CARBINE mag reload from %d plays %s", mag.rounds, scene))
	M4Carbine.MagModelStart()
	M4Carbine.OverlayLower()
	M4Carbine.At(model.playing.reloadSeconds, function()
		if mag.token ~= token or not mag.reloading then return end
		mag.reloading = false
		M4Carbine.MagStore(math.min(mag.size, M4Carbine.MagStock()))
		M4Carbine.MagSend()
		System.LogAlways(string.format("M4CARBINE mag loaded %d", mag.rounds))
		if M4Carbine.overlay.aimHeld then System.LogAlways("M4CARBINE aim rearmed " .. tostring(KCD2ForgeViewProbe.Rearm())) end
	end)
end

-- The magazine leaves the gun during a reload. The reload hides the held gun's
-- slot 0 behind m4carbine_nomag.cdf in slot 2 and loads m4_mag.cgf as slot 1 of
-- the gun entity; loading the full gun back into slot 0 blinked it out (A758). The cgf
-- keeps the gun's own coordinates, so with no slot offset it sits in the well.
-- A character attachment on RightHandSlot never drew, while a slot on the gun
-- entity did (A751). The Studio clip of an empty or partial reload (reloads)
-- plays on its own layer over the hold: it moves the arms and the rifle and
-- keys bone, a child of RightHandSlot, wherever the Studio put the magazine.
-- The hold pulls the left hand onto that bone (LeftWeapon_IKBlend weighs 1), so
-- the clip weighs it 0 and the left arm plays as authored. MagTick reads that bone in the slot's frame each
-- frame and places slot 1 there, hidden between hidden[1] and hidden[2] seconds;
-- the gun model comes back when the reload ends or is dropped. At hidden[1] the
-- spent magazine leaves the hand: a rigid body of its own spawns where the bone
-- holds it, turned as it is and moving as the bone moved plus push, turning at
-- spin (each along Henry's right, ahead and up, in m/s and deg/s), mass kg and
-- size times its own, and falls on its own. Its Forge Studio scene sets these
-- (studio ship <scene>). The newest `kept` dropped magazines stay; older
-- ones are removed. Each of sounds plays its reload sound (the native plugin's
-- m4_reload WAVs) at that clip second, timed so the click lands on the move:
-- out as the magazine leaves the well, in and in2 on the two pushes that seat
-- it (the two clicks of the kept mag in). The bare gun has
-- no forward assist plunger either: m4_assist.cgf is slot 3, in the gun's own
-- coordinates too, moved along assist, the Studio's push as keys of a clip
-- second and the plunger's offset in the slot's frame, four numbers a key.
-- From seated seconds on the magazine rests in the well and the clip keys bone
-- on the left hand, where the hold keys it: the fade out had the left hand's
-- IK slide from the well to the grip, a twitch after the reload (A770).
M4Carbine.magModel = M4Carbine.magModel or {
	gun = "objects/manmade/weapons/firearms/m4carbine.cdf",
	bare = "objects/manmade/weapons/firearms/m4carbine_nomag.cdf",
	cgf = "objects/manmade/weapons/firearms/m4_mag.cgf",
	assistCgf = "objects/manmade/weapons/firearms/m4_assist.cgf",
	catchCgf = "objects/manmade/weapons/firearms/m4_catch.cgf",
	body = {cgf = "objects/default/primitive_box.cgf", lo = {x = -0.1, y = -0.1, z = 0}, hi = {x = 0.1, y = 0.1, z = 0.2}},
	bone = "LeftWeapon_IKTarget",
	empty = "reload2",
	partial = "reload3",
	layer = 12,
	blend = 0.1,
	aimBlend = 0.3,
	blendOut = 0.4,
	mass = 0.12,
	kept = 8,
	chatHold = 8,
	sounds = {{0.01, "out"}, {0.92, "in"}, {1.12, "in2"}},
	drops = {},
	playing = nil,
	entity = nil,
	startedAt = nil,
}

M4Carbine.reloads = M4Carbine.reloads or {
	reload = {
		clip = "rifle_run_jump_rleg_start_over",
		seconds = 1.4667,
		reloadSeconds = 2.2,
		hidden = {0.3667, 0.7667},
		seated = 1.1667,
		assist = {1.7667, -1.30385e-08, -1.49012e-08, -2.6077e-08, 1.8, -0.00153166, -0.00104479, 0.00730272, 1.8333, -0.000765827, -0.00052239, 0.00365133, 1.8667, 2.14204e-07, -6.33299e-08, -2.34693e-07},
		catchPos = {},
		catchAngles = {},
		push = {-2, -3.1, -2.9},
		spin = {170, -80, 50},
		size = 0.85,
	},
	reload2 = {
		clip = "photomode_weapon_bow",
		seconds = 2.2,
		reloadSeconds = 3.3,
		hidden = {0.3667, 0.7667},
		seated = 1.1667,
		assist = {},
		catchPos = {0, 0.0256689, 0.0126449, -0.00222611, 1.6667, 0.0256688, 0.0126451, -0.00222591, 1.7, 0.00733601, 0.00447244, -0.000841301, 2.2, 0.00733605, 0.00447251, -0.000841385},
		catchAngles = {0, 0.0155502, -0.0592045, -0.255042, 1.6667, 0.0155503, -0.0592045, -0.255042, 1.7, 0.00307123, -0.0181523, -0.0763795, 2.2, 0.00307122, -0.0181523, -0.0763795},
		sounds = {{1.68, "catch"}},
		push = {-2, -3.1, -2.9},
		spin = {170, -80, 50},
		size = 0.85,
	},
	reload3 = {
		clip = "photomode_weapon_shsw",
		seconds = 2.2,
		reloadSeconds = 3.3,
		hidden = {0.3667, 0.7667},
		seated = 1.1667,
		assist = {},
		catchPos = {},
		catchAngles = {},
		push = {-2, -3.1, -2.9},
		spin = {170, -80, 50},
		size = 0.85,
	},
}

function M4Carbine.MagGun()
	local item = M4Carbine.ItemInHand()
	return item and item.entity ~= 0 and System.GetEntity(item.entity) or nil
end

function M4Carbine.MagModelStart()
	local model = M4Carbine.magModel
	if model.endedAt then
		model.catch = nil
		M4Carbine.MagModelEnd(System.GetEntity(model.entity))
	end
	local gun = model.entity == nil and M4Carbine.MagGun()
	if not gun then return end
	gun:LoadCharacter(2, model.bare)
	if M4Carbine.light.lit == gun.id then M4Carbine.LightGlow(gun, true) end
	gun:DrawSlot(0, 0)
	gun:LoadObject(1, model.cgf)
	gun:LoadObject(3, model.assistCgf)
	gun:LoadObject(4, model.catchCgf)
	model.entity, model.startedAt, model.dropped, model.was, model.sounded = gun.id, M4Carbine.Now(), false, nil, {}
	-- From the aim the arms are far from the clip's first key, so they blend in
	-- slower (A759, user: "still not good enough").
	model.blendIn = M4Carbine.overlay.state == "on" and model.blend or model.aimBlend
	M4Carbine.Dude():StartAnimation(0, model.playing.clip, model.layer, model.blendIn, M4Carbine.MagPace(), false)
end

-- A reload's clip seconds stretched over its reloadSeconds: a scene's 2.2 s played
-- too fast in game. The reload scene ends at 1.4667 s, before its right hand leaves
-- the grip for the forward assist (user: "I don't want the forward assist
-- anymore"); the arms fade back to the hold over blendOut.
function M4Carbine.MagPace()
	local reload = M4Carbine.magModel.playing
	return reload.seconds / reload.reloadSeconds
end

function M4Carbine.MagModelEnd(gun)
	local model = M4Carbine.magModel
	local now, catch = M4Carbine.Now(), model.catch
	if M4Carbine.inHand and model.chatAt and now - model.chatAt < model.chatHold and not M4Carbine.overlay.aimHeld then
		if not model.held then System.LogAlways("M4CARBINE reload end held for a talk") end
		model.held = true
		return
	end
	if model.held then System.LogAlways("M4CARBINE reload end released, in hand " .. tostring(M4Carbine.inHand)) end
	model.held = nil
	-- StopAnimation cut the clip in one frame and the gun snapped 7 cm (A759
	-- M4QUIRK), so the empty clip fades it out.
	if not model.endedAt then M4Carbine.Dude():StartAnimation(0, M4Carbine.overlay.blank, model.layer, model.blendOut, 1, false) end
	model.endedAt = model.endedAt or now
	local k = gun and catch and M4Carbine.inHand and math.min(1, (now - model.endedAt) / model.blendOut) or 1
	if k < 1 then
		local w = 1 - k * k * (3 - 2 * k)
		gun:SetSlotPos(4, {x = catch.pos.x * w, y = catch.pos.y * w, z = catch.pos.z * w})
		gun:SetSlotAngles(4, {x = catch.angles.x * w, y = catch.angles.y * w, z = catch.angles.z * w})
		return
	end
	model.entity, model.endedAt, model.catch = nil, nil, nil
	if not gun then return end
	gun:FreeSlot(1)
	gun:FreeSlot(2)
	gun:FreeSlot(3)
	gun:FreeSlot(4)
	gun:DrawSlot(0, 1)
end

-- Where a part sits at clip second t: between its keys around t, and at
-- rest outside them.
function M4Carbine.PartAt(keys, t)
	for i = 5, #keys, 4 do
		if t >= keys[i - 4] and t <= keys[i] then
			local k = (t - keys[i - 4]) / math.max(1e-6, keys[i] - keys[i - 4])
			local function at(j) return keys[i - 4 + j] + (keys[i + j] - keys[i - 4 + j]) * k end
			return {x = at(1), y = at(2), z = at(3)}
		end
	end
	return {x = 0, y = 0, z = 0}
end

-- The angles (x, then y, then z) of bone's turn in frame's, a bone's or the
-- world's when frame is nil. GetBoneLocal turns a world vector into the bone's
-- frame, so its axes come back as the rows of the bone's turn.
function M4Carbine.BoneAngles(player, bone, frame)
	local rows = {}
	for k, unit in ipairs(M4Carbine.units) do
		local a, c = frame and player:GetBoneLocal(frame, unit) or unit, player:GetBoneLocal(bone, unit)
		rows[k] = {{a.x, a.y, a.z}, {c.x, c.y, c.z}}
	end
	local function r(i, j)
		return rows[1][1][i] * rows[1][2][j] + rows[2][1][i] * rows[2][2][j] + rows[3][1][i] * rows[3][2][j]
	end
	return {x = math.atan2(r(3, 2), r(3, 3)), y = math.asin(math.max(-1, math.min(1, -r(3, 1)))), z = math.atan2(r(2, 1), r(1, 1))}
end

-- Where bone sits in RightHandSlot's frame, the gun's: a position and the
-- angles SetSlotAngles takes. GetBoneLocal turns a vector but returns it unit
-- length (A756 trace), so the distance is put back.
function M4Carbine.MagPlacement(player, bone)
	local slot = 'RightHandSlot'
	local s, b = player:GetBonePos(slot), player:GetBonePos(bone)
	local d = {x = b.x - s.x, y = b.y - s.y, z = b.z - s.z}
	local u = player:GetBoneLocal(slot, d)
	local k = math.sqrt(d.x * d.x + d.y * d.y + d.z * d.z) / math.max(1e-6, math.sqrt(u.x * u.x + u.y * u.y + u.z * u.z))
	return {x = u.x * k, y = u.y * k, z = u.z * k}, M4Carbine.BoneAngles(player, bone, slot), b
end

-- A vector along Henry's right, ahead and up axes, scaled by k, in the world.
function M4Carbine.AlongHenry(player, v, k)
	local out = {x = 0, y = 0, z = 0}
	for axis = 0, 2 do
		local d = player:GetDirectionVector(axis)
		out.x, out.y, out.z = out.x + d.x * v[axis + 1] * k, out.y + d.y * v[axis + 1] * k, out.z + d.z * v[axis + 1] * k
	end
	return out
end

-- A vector in entity e's own axes, turned into the world's. Scale is left out.
local turnedKeys = {'x', 'y', 'z'}

function M4Carbine.Turned(e, v)
	local out = {x = 0, y = 0, z = 0}
	for axis, k in ipairs(turnedKeys) do
		local d = e:GetDirectionVector(axis - 1)
		local n = math.sqrt(d.x * d.x + d.y * d.y + d.z * d.z)
		out.x, out.y, out.z = out.x + d.x / n * v[k], out.y + d.y / n * v[k], out.z + d.z / n * v[k]
	end
	return out
end

-- The spent magazine leaves the hand where the bone holds it, turned as it is,
-- at the speed the bone moved at since was plus the push, and turning at the
-- spin. m4_mag.cgf has no physics proxy (mass 0, measured live), so a hidden
-- box (body) scaled to the magazine's bounds falls instead, and MagDropsTick
-- puts the magazine on it each frame.
function M4Carbine.MagDrop(player, model, was, t)
	local at = player:GetBonePos(model.bone)
	model.count = (model.count or 0) + 1
	-- A flags field in the spawn table dropped its scale (measured live), so
	-- the flags go on after.
	local function spawn(name, cgf, scale)
		local e = System.SpawnEntity({
			class = "BasicEntity",
			name = name .. model.count,
			position = at,
			scale = scale,
			properties = {
				object_Model = cgf,
				bCanTriggerAreas = false,
				Physics = {bPhysicalize = false, bRigidBody = true, bPushableByPlayers = true, Density = -1, Mass = -1},
				MultiplayerOptions = {bNetworked = false},
			},
		})
		if e then e:SetFlags(ENTITY_FLAG_NO_SAVE, 0) end
		return e
	end
	local mag = spawn("m4_spent_mag_", model.cgf, {x = model.playing.size, y = model.playing.size, z = model.playing.size})
	if not mag then return end
	-- SetWorldAngles and SetWorldPos reset an entity's scale to 1 (measured
	-- live); SetAngles and SetPos keep it.
	local angles = M4Carbine.BoneAngles(player, model.bone)
	mag:SetAngles(angles)
	local b = model.body
	local lo, hi = mag:GetLocalBBox()
	local scale, off = {}, {}
	for _, k in ipairs({'x', 'y', 'z'}) do
		scale[k] = math.max(0.001, hi[k] - lo[k]) / (b.hi[k] - b.lo[k])
		off[k] = (lo[k] + hi[k]) / 2 - scale[k] * (b.lo[k] + b.hi[k]) / 2
	end
	local body = spawn("m4_spent_mag_body_", b.cgf, scale)
	if not body then
		System.RemoveEntity(mag.id)
		return
	end
	body:SetAngles(angles)
	local o = M4Carbine.Turned(mag, off)
	body:SetPos({x = at.x + o.x, y = at.y + o.y, z = at.z + o.z})
	body:DrawSlot(0, 0)
	-- A mass is taken at scale 1 and shrinks with the scale, so the box gets the
	-- density that weighs it mass.
	local volume = 1
	for _, k in ipairs({'x', 'y', 'z'}) do volume = volume * scale[k] * (b.hi[k] - b.lo[k]) end
	body:Physicalize(0, PE_RIGID, {mass = -1, density = model.mass / volume, flags = 0})
	System.LogAlways(string.format("M4CARBINE mag dropped at=%.3f,%.3f,%.3f body=%.3f,%.3f,%.3f mass=%s", at.x, at.y, at.z, scale.x * 0.2, scale.y * 0.2, scale.z * 0.2, tostring((body:GetPhysicalStats() or {}).mass)))
	table.insert(model.drops, {mag = mag.id, body = body.id, off = off, at = M4Carbine.Now()})
	if #model.drops > model.kept then
		local old = table.remove(model.drops, 1)
		System.RemoveEntity(old.mag)
		System.RemoveEntity(old.body)
	end
	local v = M4Carbine.AlongHenry(player, model.playing.push, 1)
	if was and t > was.t then
		v = {x = v.x + (at.x - was.pos.x) / (t - was.t), y = v.y + (at.y - was.pos.y) / (t - was.t), z = v.z + (at.z - was.pos.z) / (t - was.t)}
	end
	body:SetPhysicParams(PHYSICPARAM_VELOCITY, {v = v, w = M4Carbine.AlongHenry(player, model.playing.spin, math.pi / 180)})
end

-- Each dropped magazine where its body is.
function M4Carbine.MagDropsTick()
	for _, drop in ipairs(M4Carbine.magModel.drops) do
		local mag, body = System.GetEntity(drop.mag), System.GetEntity(drop.body)
		if mag and body then
			local p, a = body:GetWorldPos(), body:GetWorldAngles()
			local last = drop.placed
			if not (last and last.p.x == p.x and last.p.y == p.y and last.p.z == p.z and last.a.x == a.x and last.a.y == a.y and last.a.z == a.z) then
				local o = M4Carbine.Turned(body, drop.off)
				mag:SetAngles(a)
				mag:SetPos({x = p.x - o.x, y = p.y - o.y, z = p.z - o.z})
				drop.placed = {p = p, a = a}
			end
			if not drop.landed then M4Carbine.Try("DropLanding", M4Carbine.DropLanding, drop, body, p) end
		end
	end
end

-- The game's surface types (Libs/MaterialEffects/SurfaceTypes.xml) by the
-- magazine drop sound each lands with; any other lands as grass.
M4Carbine.dropSurfaces = {
	mat_water = "water", mat_mudwater = "water", mat_rockwater = "water", mat_slowwater = "water", mat_deep_water_barrier = "water",
	mat_rock = "rock", mat_gravel = "rock", mat_road = "rock", mat_stairs_stone = "rock", mat_rock_unwalk = "rock",
	mat_rock_horse_ignore = "rock", mat_plaster = "rock", mat_ceramics = "rock",
	mat_wood = "wood", mat_wood_soft = "wood", mat_wood_unwalk = "wood", mat_stairs_wood = "wood", mat_door_wood = "wood",
	mat_railing_wood_seethrough = "wood", mat_shield_wood = "wood", mat_wicker = "wood",
	mat_metal = "metal", mat_metal_seethrough = "metal", mat_door_metal = "metal", mat_plate = "metal", mat_chainmail = "metal",
	mat_chainmail_plate = "metal", mat_bells = "metal",
	mat_grass_tall = "plants", mat_bushes = "plants", mat_vegetation = "plants", mat_vegetation_seethrough = "plants",
	mat_vegetation_horse_ignore = "plants", mat_canopy = "plants", mat_thatch = "plants", mat_organic_stuff = "plants",
	mat_glass = "glass", mat_window = "glass",
	mat_fabric = "cloth", mat_fabric_items = "cloth", mat_sack = "cloth", mat_leather = "cloth", mat_flesh = "cloth",
	mat_fabric_plate = "cloth", mat_fabric_chainmail = "cloth", mat_fabric_chainmail_plate = "cloth", mat_leather_chainmail = "cloth",
}

-- The first thing a ray from from along dir meets, water included, past the
-- dropped magazine and its body.
function M4Carbine.DropHit(drop, from, dir)
	local hits = {}
	local types = (ent_static or 1) + (ent_sleeping_rigid or 2) + (ent_rigid or 4) + (ent_terrain or 0x100) + (ent_water or 0x200)
	local n = Physics.RayWorldIntersection(from, dir, 1, types, drop.mag, drop.body, hits)
	if type(n) == "table" then hits, n = n, #n end
	return (n or 0) > 0 and hits[1] or nil
end

-- A dropped magazine lands when a ray along its path, a frame and its size
-- ahead, meets something, or when it loses half its speed at once. The ground
-- under it then picks the drop sound. Its first 0.1 s skips the gun it leaves.
function M4Carbine.DropLanding(drop, body, p)
	local age = M4Carbine.Now() - drop.at
	local v = body:GetVelocity()
	local speed = math.sqrt(v.x * v.x + v.y * v.y + v.z * v.z)
	local last = drop.speed
	drop.speed = speed
	if age < 0.1 then return end
	if age > 4 then
		drop.landed = true
		return
	end
	local reach = speed * System.GetFrameTime() * 2 + 0.15
	local hit = speed > 0.05 and M4Carbine.DropHit(drop, p, {x = v.x / speed * reach, y = v.y / speed * reach, z = v.z / speed * reach})
	if not hit and not (last and last > 1 and speed < last * 0.5) then return end
	drop.landed = true
	hit = hit or M4Carbine.DropHit(drop, {x = p.x, y = p.y, z = p.z + 0.25}, {x = 0, y = 0, z = -0.75})
	local name = hit and hit.surface and System.GetSurfaceTypeNameById(hit.surface)
	local sound = M4Carbine.dropSurfaces[name or ""] or "grass"
	M4Carbine.MarkTrigger("c_f_m4a1_drop_" .. sound, string.format("mag landed on %s at %.1f m/s, drop sound %s", tostring(name), last or 0, sound))
end

local magZeroPos, magZeroAngles = {x = 0, y = 0, z = 0}, {x = 0, y = 0, z = 0}

function M4Carbine.MagSounds(model, sounds, t)
	for _, s in ipairs(sounds) do
		if t >= s[1] and not model.sounded[s[2]] then
			model.sounded[s[2]] = true
			M4Carbine.MarkTrigger("c_f_m4a1_reload_" .. s[2], "reload sound " .. s[2])
		end
	end
end

function M4Carbine.MagTick()
	local model = M4Carbine.magModel
	if model.entity == nil then return end
	local gun = System.GetEntity(model.entity)
	if not gun or not M4Carbine.mag.reloading then
		M4Carbine.MagModelEnd(gun)
		return
	end
	local now = M4Carbine.Now() - model.startedAt
	local t = math.min(now * M4Carbine.MagPace(), model.playing.seconds)
	local player = M4Carbine.Dude()
	local at, angles, pos = magZeroPos, magZeroAngles, nil
	-- While the clip blends in, the bone is part way from the hold's left hand,
	-- and once seated it is on the left hand, so the magazine stays in the well.
	if not (now < model.blendIn or t >= model.playing.seated) then at, angles, pos = M4Carbine.MagPlacement(player, model.bone) end
	if model.sounds then
		M4Carbine.MagSounds(model, model.sounds, t)
		if model.playing.sounds then M4Carbine.MagSounds(model, model.playing.sounds, t) end
	end
	if not model.dropped and t >= model.playing.hidden[1] then
		model.dropped = true
		M4Carbine.MagDrop(player, model, model.was, now)
	end
	model.was = {pos = pos or player:GetBonePos(model.bone), t = now}
	gun:DrawSlot(1, (t >= model.playing.hidden[1] and t < model.playing.hidden[2]) and 0 or 1)
	if M4Carbine.trace then
		local spine, out = player:GetBonePos('Spine2'), ''
		for _, b in ipairs({'RightHandSlot', 'LeftHand', 'Head'}) do
			local p = player:GetBonePos(b)
			local r = player:GetBoneLocal('Spine2', {x = p.x - spine.x, y = p.y - spine.y, z = p.z - spine.z})
			out = out .. string.format(' %s=%.4f,%.4f,%.4f', b, r.x, r.y, r.z)
		end
		local view = System.GetViewCameraDir()
		System.LogAlways(string.format("M4RELOAD t=%.3f mag=%.4f,%.4f,%.4f view=%.3f,%.3f,%.3f%s", t, at.x, at.y, at.z, view.x, view.y, view.z, out))
	end
	gun:SetSlotPos(1, at)
	gun:SetSlotAngles(1, angles)
	gun:SetSlotPos(3, M4Carbine.PartAt(model.playing.assist, t))
	local catch = {pos = M4Carbine.PartAt(model.playing.catchPos, t), angles = M4Carbine.PartAt(model.playing.catchAngles, t)}
	gun:SetSlotPos(4, catch.pos)
	gun:SetSlotAngles(4, catch.angles)
	model.catch = #model.playing.catchAngles > 0 and catch or nil
end

-- Temporary per-frame trace while the M4 is in hand (M4SW lines).
function M4Carbine.TraceAim(p)
	local o = M4Carbine.overlay
	local now = M4Carbine.Now()
	local vd = System.GetViewCameraDir()
	local out = ''
	for _, b in ipairs({'Spine4', 'RightHandSlot', 'LeftHand', 'Head', 'Hips'}) do
		local q = p:GetBoneDir(b)
		out = out .. string.format(' %s=%.3f,%.3f,%.3f', b, q.x, q.y, q.z)
	end
	local L = ''
	for l = 0, 15 do L = L .. (p:IsAnimationRunning(0, l) and '1' or '0') end
	local h, w = p:GetBonePos('Hips'), p:GetWorldPos()
	local ea = p:GetWorldAngles()
	local lh, rs = p:GetBonePos('LeftHand'), p:GetBonePos('RightHandSlot')
	out = out .. string.format(' lg=%.1f,%.1f,%.1f', (lh.x - rs.x) * 100, (lh.y - rs.y) * 100, (lh.z - rs.z) * 100) .. ' lt='
	for l = 0, 15 do out = out .. string.format('%.3f,', p:GetAnimationTime(0, l) or -1) end
	local la, ra, lu, ru, v = p:GetBonePos('LeftArm'), p:GetBonePos('RightArm'), p:GetBonePos('LeftUpLeg'), p:GetBonePos('RightUpLeg'), p:GetVelocity()
	out = out .. string.format(' arms=%.1f legs=%.1f vel=%.2f,%.2f sw=%.3f cr=%s', math.deg(math.atan2(la.y - ra.y, la.x - ra.x)), math.deg(math.atan2(lu.y - ru.y, lu.x - ru.x)), v.x, v.y, o.stanceW, tostring(M4Carbine.Crouched()))
	out = out .. string.format(' ent=%.2f,%.2f,%.2f', math.deg(ea.x), math.deg(ea.y), math.deg(ea.z))
	out = out .. string.format(' tw=%.1f gf=%.1f rp=%.3f tww=%.2f gd=%d href=%s', o.twist, o.gunFix, o.raiseP, o.twistW, o.gridDir, tostring(o.hipsRef[o.aimPlaying] and math.floor(o.hipsRef[o.aimPlaying] * 10) / 10))
	System.LogAlways(string.format('M4SW ih=' .. tostring(M4Carbine.inHand) .. ' t=%.3f ft=%.4f ap=%s L=%s st=%s held=%s idle=%s hz=%.3f at=%.3f vd=%.3f,%.3f,%.3f%s',
		now, System.GetFrameTime(), tostring(o.aimPlaying), L, tostring(o.state), tostring(o.aimHeld), tostring(o.idleClip),
		h.z - w.z, p:GetAnimationTime(0, o.layer) or -1, vd.x, vd.y, vd.z, out))
end

-- Look IK turns the torso, head and both arms toward a look target that trails
-- the view out of a sprint: aiming from a sprint and a quick turn swung the spine
-- 55-147 deg off the view with the gun 50 deg off, fading out over 0.9 s, and no
-- torso turn on layer 9 can undo it because it bends the arms too (logger
-- 2026-09-23). With ca_UseLookIK 0 it was gone (user, 2026-09-23) and the
-- settled aim read the same. That cvar is global and stopped every NPC's head
-- tracking, so the script now asks the plugin (m4_look.c) to skip look IK for
-- the player's character alone; ca_UseLookIK stays the game's.
M4Carbine.lookOff = nil

function M4Carbine.SyncLookIK(held)
	local off = held and not (M4Carbine.Mounted() and M4Carbine.overlay.state ~= "aim")
	if off == M4Carbine.lookOff then return end
	M4Carbine.lookOff = off
	System.LogAlways("M4CARBINE auto look " .. (off and "1" or "0"))
end

-- Aimed, the M4 zooms the view as the bow does: a bow aim halves the 63.09 deg
-- view (FOVPROBE 2026-10-07). The engine eases every cl_fov change linearly over
-- 1 s from where the view is, too slow for the M4, and a set every frame dropped
-- the game to 20-35 fps, so the zoom sets it only a few times per move: it aims
-- past the goal so the ease gets there in seconds (in or out); a frame short of
-- the goal it aims again to land in landSeconds, until the view is within
-- settle of the goal, or has passed it, which then takes it. A target below
-- zero eases like any other (NEGFOV 2026-10-07: -60 eased at 123 deg/s), and the
-- view lands on the goal long before it gets there. A target of 241 (A740, a
-- 0.15 s zoom out) pinned the view at 75 deg and no later set moved it, while
-- 137 (A739) and 170 (FOLLOW probe) eased, so targets stay within floor and
-- ceiling, both probed live. That caps the zoom out at about 0.23 s. A738's
-- 0.5 s in felt slow ("0.5 feels slow"), and a release should zoom out faster
-- than an aim zooms in (A739). The table outlives a script reload, so a reload
-- mid-aim still zooms back out.
M4Carbine.zoom = M4Carbine.zoom or {factor = 0.5, inSeconds = 0.3, outSeconds = 0.2, landSeconds = 0.1, settle = 0.1, floor = -60, ceiling = 170, base = nil, on = false, goal = nil, rate = 0, startAt = -1}

function M4Carbine.ZoomAim(view, rate)
	local zoom = M4Carbine.zoom
	zoom.target = math.min(zoom.ceiling, math.max(zoom.floor, view + rate))
	zoom.rate = zoom.target - view
	System.SetCVar('cl_fov', string.format("%.4f", zoom.target))
	System.LogAlways(string.format("M4CARBINE zoom %.4f view %.4f goal %.4f", zoom.target, view, zoom.goal))
end

function M4Carbine.SyncChatBubbles(zoomed)
	local zoom = M4Carbine.zoom
	if zoomed and zoom.bubbles == nil then
		local ok, value = pcall(function() return tonumber(System.GetCVar('wh_dlg_ChatBubbles')) end)
		if not ok or value == nil then return end
		zoom.bubbles = value
		System.SetCVar('wh_dlg_ChatBubbles', 0)
		System.LogAlways(string.format("M4CARBINE chat bubbles off, game value %s", tostring(value)))
	elseif not zoomed and zoom.bubbles ~= nil then
		System.SetCVar('wh_dlg_ChatBubbles', zoom.bubbles)
		System.LogAlways(string.format("M4CARBINE chat bubbles back to %s", tostring(zoom.bubbles)))
		zoom.bubbles = nil
	end
end

function M4Carbine.ZoomTick()
	M4Carbine.ZoomStep()
	local zoom = M4Carbine.zoom
	M4Carbine.SyncChatBubbles(zoom.goal ~= nil or (zoom.on and zoom.base ~= nil))
end

function M4Carbine.ZoomStep()
	local zoom, overlay = M4Carbine.zoom, M4Carbine.overlay
	local want = (M4Carbine.inHand and overlay.aimHeld and not overlay.holstering and (overlay.state == "aim" or overlay.state == "fading")) or false
	local view = math.deg(System.GetViewCameraFov())
	if want ~= zoom.on then
		zoom.on = want
		if want and not zoom.goal then zoom.base = tonumber(System.GetCVar('cl_fov')) end
		if not zoom.base then return end
		zoom.goal, zoom.startAt = want and zoom.base * zoom.factor or zoom.base, M4Carbine.Now()
		M4Carbine.ZoomAim(view, (zoom.goal - view) / (want and zoom.inSeconds or zoom.outSeconds))
		return
	end
	if not zoom.goal then return end
	local gap = zoom.goal - view
	if math.abs(gap) <= zoom.settle or gap * zoom.rate < 0 or M4Carbine.Now() - zoom.startAt > 2 then
		zoom.target = zoom.goal
		System.SetCVar('cl_fov', string.format("%.4f", zoom.goal))
		zoom.goal = nil
	elseif math.abs(gap) <= math.abs(zoom.rate) * System.GetFrameTime() * 1.5 then
		M4Carbine.ZoomAim(view, gap / zoom.landSeconds)
	end
end

function M4Carbine.Sync()
	local held = M4Carbine.HoldsCarbine()
	if held ~= M4Carbine.inHand then
		M4Carbine.inHand = held
		if held then
			M4Carbine.overlay.drawStartAt = M4Carbine.Now()
			local talkAt = M4Carbine.overlay.talkAt
			M4Carbine.overlay.afterTalk = talkAt ~= nil and M4Carbine.Now() - talkAt < 2
			M4Carbine.overlay.drawSped, M4Carbine.overlay.drawRushed, M4Carbine.overlay.drawOwed = false, false, nil
			-- An aim pressed before the M4 is in the hand is still held.
			M4Carbine.overlay.aimHeld = M4Carbine.overlay.rmb
			-- A right button held before the draw shows only as a block. The draw
			-- took it as an aim from the plugin's tap 0.25 s after the M4 was in
			-- hand (A698 log 19:06:21, user: "still not good enough"), so the aim
			-- starts at once and drops if no tap confirms it within aimGuessSeconds.
			if M4Carbine.overlay.blockHeld and M4Carbine.PressedDraw() then
				local overlay = M4Carbine.overlay
				overlay.aimHeld, overlay.aimGuess = true, true
				M4Carbine.At(overlay.aimGuessSeconds, function()
					if overlay.aimGuess and overlay.aimHeld then
						overlay.aimGuess, overlay.aimHeld = false, false
						System.LogAlways("M4CARBINE draw aim guess dropped")
						M4Carbine.OverlayLower()
					end
				end)
			end
			M4Carbine.overlay.blockHeld = false
			-- The game reports a right button held before the draw as a block, so
			-- a quick slot draw taps the aim once in hand: the press it sends tells
			-- a held aim to fade the draw into the aim (A691, user: "does not work").
			if M4Carbine.PressedDraw() then M4Carbine.charge.pending = true end
			pcall(M4Carbine.MarkProps, M4Carbine.Dude())
		else
			-- A holster swallows the aim release, and a held aim left behind kept
			-- the idle hold off after the next draw and asked for recharges the
			-- shots never got (A680, user: "when I aimed. I could not shoot").
			M4Carbine.overlay.aimHeld, M4Carbine.charge.pending = false, false
		end
		local projectile = M4Carbine.CaptureProjectile()
		System.ExecuteCommand("wh_w_AimByProjectile " .. (held and 0 or projectile))
		M4Carbine.SyncLookIK(held)
		M4Carbine.MagDrawn(held)
		M4Carbine.SyncBuff(held)
		M4Carbine.MarkShotSound(held)
		M4Carbine.back.dirty = true
		System.LogAlways("M4CARBINE in hand " .. tostring(held) .. " projectile " .. tostring(held and 0 or projectile))
	end
end

-- The M4's tuned idle arms live on the spare DBA entry rifle_walk_jump_rleg_start
-- (A337), while rifle_idle_loaded is vanilla again for the handgonnes. Layer 9
-- holds a looping clip over Mannequin until it is stopped (A336). StopAnimation
-- snaps within a frame, which flashed the vanilla hold before every aim (A341), so
-- the overlay leaves by crossfading into the vanilla `blank` clip (one HeadEnd
-- controller) over the transition's own blend and stops once that has finished.
-- It fades in over the draw's 0.49 s exit blend, timed from the draw start, the frame the
-- game puts the M4 in hand (a press lands about 0.22 s before it; a press during the
-- draw is ignored by the game). rifle_draw_player_over is 38 frames = 1.27 s. A fade that runs alongside the
-- aim-in still mixed the vanilla hold in (A342), and holding it for the 0.2 s
-- MotionIdle blend still dipped the hand, because the raise takes about 0.6 s
-- (A343). Crossfading the overlay into the tuned aim arms (the spare
-- rifle_walk_jump_rleg_land) broke the raise (A339, A344), so an aim press still
-- holds the overlay for the 0.2 s blend and then fades (a 0.3 s hold, A346, was
-- worse).
-- A save load kills the tick's timer, so the overlay starts on the first input
-- after the load screen. With no draw press it used to wait out a draw and fade
-- in, showing the vanilla hold for over a second (A346), so the first start
-- after a load is instant. A release used to snap the overlay onto the tuned aim
-- arms and crossfade to the hold over 0.45 s (A344), but those arms are level:
-- the snap dropped the pitch aim pose, so aiming down and lowering threw the hand
-- 12 cm up (A348). Fading in from whatever is on screen fixed that (A349), but
-- the torso takes about 0.6 s to leave the aim twist, and a 0.1 s fade put the
-- hold arms on a still twisted torso, gun behind the back (A350). So a level
-- lower keeps A344's snap and 0.45 s crossfade, which follows the torso, and
-- only a lower while looking up or down fades in from the screen over 0.2 s.
-- Every timed crossfade still mixed the vanilla hold into the transition
-- (A351 to A362); OverlayLower keeps the overlay full across Mannequin's blends.
-- A crossfade between the idle and aim arms still slid both hands off the M4:
-- each joint slerps on its own and the arms hung off Mannequin's shoulders (A380
-- grip log: left hand 35.4 cm idle, 47 mid-raise, 40.4 aim). The raise and lower
-- now play authored transition clips (kcd2-forge-overlay-transition.py) that
-- carry the shoulders and keep both hands on the rifle on every key; their last
-- keys hold the end pose, so the switch to the loop is seamless.
-- rifle_idle_aim is vanilla again for the handgonnes (A382): the M4's tuned aim
-- body and arms live on rifle_walk_jump_rleg_land, which the raise ends on and
-- holds while aimed. The overlay stays on while Henry moves and shoots: parking it
-- for movement pulled the gun down and up again on every start and stop (A391).
-- The idle arms carry no torso, so a crouch's forward lean tipped the rifle into
-- Henry's legs. While Henry crouches (below) the overlay plays
-- crouchClip, the same hold re-solved to keep the rifle where the standing idle
-- holds it against the head (kcd2-forge-crouch-hold.py). A crouched aim plays
-- crouchAimClip, which `scene edit crouch-aim` fills. Crouched, the raise and lower
-- crossfade between the crouch clips: the standing raise and lower clips flashed the
-- standing arms, and the weapon IK keeps the left hand on the gun through a fade.
-- A crouched Fire plays its own held clip (kcd2-forge-crouch-shot.py), not the handgonne shot.
-- Measured live (A526): the lowest hips of every half second sit at 0.63 m
-- crouch walking, 0.73 m at most crouch running, and 0.76 m at least standing
-- running. Henry counts as crouched while the hips dipped below crouchHips at any
-- point in the last crouchWindow seconds, so the step bob never swaps the hold.
-- Hips above standHips end the crouch at once: the window alone held the crouch
-- hold 0.6 s into standing, 10 degrees left of the standing sight. A shot
-- cancels the stance animation and the hips jump 0.3 m in one frame; the
-- hold then swaps at once, since any crossfade shows the other stance's hold.
-- Aimed, no swap works: the stance change takes half a second and the gun
-- turns with the hips, so any swap, instant or faded, showed the other hold
-- for part of it (logger 2026-09-23). Once the aim has settled on its hold,
-- it plays a stance clip instead: standStance runs from the standing hold to
-- the crouched one, crouchStance the other way (kcd2-forge-stance-blend.py).
-- StanceScrub sets its time from the hips every frame. A clip's time only
-- takes when it started with no blend and at a speed above 0, and only from
-- the frame after it starts (live probes, 2026-09-23), so the aim starts on
-- the clip whose first key is the stance Henry is in, at stanceSpeed. The
-- blend follows the hips' height between stanceBottom and stanceTop. It used to
-- follow crouch presses, but the game ignores some, delays others until the
-- aim is up, and stands up without one: walking aimed, the hold stayed crouched
-- on a standing body, or the other way, and the gun swung 10-17 deg (A729, A730
-- M4SW). Walking, the hips sit at 0.58-0.65 m crouched and 0.76-0.86 m standing
-- (A728-A730 M4SW), so the walking band is stanceWalkBottom to stanceWalkTop,
-- inside the stride bob; it widens to the still band stillAfter seconds after
-- Henry stops. The blend moves at most 1/stanceSlew per second, except on a
-- shot's snapped stance, which lands at once.
-- The head is no use: live it crossed every line while crouch walking (A528, A529).
-- A run stride dips the hips to 0.68 m too, so a run flipped the hold to the crouch
-- one several times a second. The skeleton box top splits them: running it stays
-- at 1.76 m or above, crouch walking at 1.72 m or below (logger 2026-10-02).
M4Carbine.overlay = {clip = "rifle_walk_jump_rleg_start", crouchClip = "rifle_walk_jump_rleg_start_over", mountClip = "rifle_run_jump_rleg_land", mountAimClip = "photomode_tracking", mountGridClip = "photomode_victorious_01", mountEyeClip = "photomode_victorious_02", mountGrid = {yaws = 641, pitches = 25, downs = 1, down = 0, downStep = 0.6, rolls = 1, rollStep = 4, rollSpan = 0, rollRef = 66, rollSign = 0, roll = 0, yawStep = 0.05, pitchStep = 2.5, yawSpan = 16, pitchSpan = 30, gain = 0, steer = true, steerPole = 0.3, steerRamp = 0.25, hold = 0, aimYaw = -0.05, aimPitch = -0.05, camDrop = -0.05, camYaw = -0.05, camJump = 6, camHoldMax = 2, camHeld = 0, camMove = 0.003, camSettle = 0.3, eye = {pole = 0.3, layer = 13, rows = 801, planes = 9, plane = 4, span = 0.04, step = 0.0001, right = {x = 0.022495, y = 0.997421, z = 0.068151}, up = {x = 0.985039, y = -0.010463, z = -0.172012}}}, mountFix = nil, crouchHips = 0.70, crouchTop = 1.74, standHips = 0.8, crouchWindow = 0.6, crouchDipAt = -1, airHips = 0.95, landing = false, landLow = 0, landRise = 0.02, crouchFloor = 0.53, stanceBlend = 0.2, lastHips = nil, snapAt = -1, idleClip = nil, aimClip = "rifle_walk_jump_rleg_land", crouchAimClip = "rifle_walk_jump_rleg_land_over", aimPlaying = nil,
	standStance = "rifle_walk_jump_lleg_start_over", crouchStance = "rifle_walk_jump_lleg_land_over", stanceSpeed = 0.01, stanceDelay = 0.1, stanceTop = 0.78, stanceBottom = 0.52, stanceWalkTop = 0.75, stanceWalkBottom = 0.66, stillAfter = 0.3, movedAt = -1, stanceSlew = 0.1, shiftRise = 0.35, shiftHold = 0.3, shiftAt = -1, stancePressAt = -1, pressWindow = 1, riseLast = nil, gunFix = 0, gunRef = nil, gunGain = 0.7, gunMax = 15, gunFade = 20, stanceRamp = 0.15, stanceW = 0, stanceSteps = 97, clipKeys = 784, twistMax = 135, twistRate = 2400, twistDead = 1, twistBand = 2, twistWalkDead = 15, twistFull = false, twist = 0, twistStandW = 0.5, hipsRef = {rifle_walk_jump_lleg_start_over = 64.5, rifle_walk_jump_lleg_start = 64.5, rifle_walk_jump_lleg_land_over = 56.7}, twistStable = 0, hipsLast = nil,
	gridSteps = 28, gridTurns = 8, gridKeys = 1008, raiseP = 0, gridDir = 0, twistW = 0, twistIn = 0.05, gridLive = true, gridAt = -1, gridWait = 0.15, blank = "blank", layer = 9,
	drawSeconds = 1.02, drawGrace = 0.1, drawBlend = 0, drawPlateau = 0.6, drawFade = 0.2, drawTailSpeed = 4, drawAimSpeed = 2, drawAimBlend = 0.15, blockHeld = false, aimGuess = false, aimGuessSeconds = 0.6, drawHiddenSpeed = 10, drawSped = false, drawRushed = false, rmb = false, drawRunning = false, drawOwed = false, onUntil = 0, aimHold = 0.05, aimBlend = 0.2, holsterBlend = 0.05, raiseClip = "rifle_walk_jump_lleg_start", lowerClip = "rifle_walk_jump_lleg_land", clipSeconds = 2, raiseSeconds = 0.18, crouchRaiseSeconds = 0.25, raiseHandoff = 0.1, lowerSeconds = 0.45, raiseAt = -1, lowerAt = -1, loaded = false,
	disabled = false, state = nil, fadeToken = 0, holdUntil = 0, aimHeld = false, drawnSince = nil, pressAt = nil, drawStartAt = nil, holstering = false, holsterAsk = nil, itemLayer = 6, tickAt = -1, health = nil}

M4Carbine.mountedMemo = {frame = -1}

function M4Carbine.MountedRead() return M4Carbine.Dude().human:IsMounted() end

function M4Carbine.Mounted()
	local memo = M4Carbine.mountedMemo
	if memo.frame == M4Carbine.frameNo then return memo.value end
	local ok, mounted = pcall(M4Carbine.MountedRead)
	memo.frame, memo.value = M4Carbine.frameNo, ok and mounted
	return memo.value
end

-- In the saddle the legs fold like a crouch, so a rider never counts as crouched.
function M4Carbine.CrouchRead(overlay, now)
	local player = M4Carbine.Dude()
	local hips = player:GetBonePos('Hips').z
	local raw = hips - player:GetWorldPos().z
	if overlay.lastHips and math.abs(raw - overlay.lastHips) > 0.1 then overlay.snapAt = now end
	overlay.lastHips = raw
	local legs = hips - math.min(player:GetBonePos('LeftToeBase').z, player:GetBonePos('RightToeBase').z)
	if raw > overlay.airHips or player.actor:IsFlying() then
		overlay.landing, overlay.landLow = true, legs
	elseif overlay.landing then
		overlay.landLow = math.min(overlay.landLow, legs)
		if legs < overlay.crouchFloor or (legs > overlay.crouchHips and legs > overlay.landLow + overlay.landRise and not player:IsAnimationRunning(0, M4Carbine.charge.landLayer)) then overlay.landing = false end
	end
	local _, box = player:GetLocalBBox()
	if legs < overlay.crouchHips and box.z < overlay.crouchTop and not overlay.landing then overlay.crouchDipAt = now end
	if legs > overlay.standHips then overlay.crouchDipAt = -1 end
end

function M4Carbine.CrouchedNow()
	local overlay = M4Carbine.overlay
	local now = M4Carbine.Now()
	if M4Carbine.Mounted() then
		overlay.crouchDipAt = -1
		return false
	end
	pcall(M4Carbine.CrouchRead, overlay, now)
	return overlay.crouchDipAt >= 0 and now >= overlay.crouchDipAt and now - overlay.crouchDipAt < overlay.crouchWindow
end

M4Carbine.crouchedMemo = {frame = -1, open = false}

function M4Carbine.Crouched()
	local memo = M4Carbine.crouchedMemo
	if not memo.open then return M4Carbine.CrouchedNow() end
	if memo.frame ~= M4Carbine.frameNo then memo.frame, memo.value = M4Carbine.frameNo, M4Carbine.CrouchedNow() end
	return memo.value
end

function M4Carbine.StanceBlend()
	local overlay = M4Carbine.overlay
	return M4Carbine.Now() - overlay.snapAt < 0.15 and 0 or overlay.stanceBlend
end

-- The overlay tick runs every 100 ms, so FrameTick also swaps the stance hold:
-- a snapped stance otherwise showed the other stance's hold for up to 100 ms.
function M4Carbine.StanceSwap(player, wanted)
	local overlay = M4Carbine.overlay
	local idle = wanted and overlay.state == "on" and M4Carbine.IdleClip()
	if idle and overlay.idleClip ~= idle then
		overlay.idleClip = idle
		overlay.gridDir = 0
		player:StartAnimation(0, overlay.idleClip, overlay.layer, M4Carbine.StanceBlend(), 1, true)
		System.LogAlways("M4CARBINE overlay idle " .. overlay.idleClip)
		return true
	end
	return false
end

-- In the saddle the horse aim pose turns the torso to the view and drops our
-- spine keys, so the torso turn does nothing; mountAimClip is the standing aim
-- hold turned 16.9 deg right and 8.7 deg down about the eye, then moved 2.4 cm right, 1 cm down and 0.5 cm out (kcd2-forge-mount-hold.py),
-- the offset the horse aim pose left it at (probe 2026-10-04).
function M4Carbine.AimClip()
	if M4Carbine.Mounted() then return M4Carbine.overlay.mountAimClip end
	return M4Carbine.Crouched() and M4Carbine.overlay.crouchAimClip or M4Carbine.overlay.aimClip
end

-- aimPlaying keeps the stance clip after a release, so without the state check
-- the next aim press scrubbed the lower or idle clip back to its first key, a
-- 13 degree snap in one frame (logger 2026-09-23).
function M4Carbine.OnStance()
	local overlay = M4Carbine.overlay
	return overlay.state == "aim" and (overlay.aimPlaying == overlay.standStance or overlay.aimPlaying == overlay.crouchStance or overlay.aimPlaying == overlay.raiseClip)
end

-- Swaps the settled aim hold for the stance clip that starts on it.
function M4Carbine.PlayStance(player)
	local overlay = M4Carbine.overlay
	overlay.stanceW = overlay.aimPlaying == overlay.crouchAimClip and 1 or 0
	overlay.aimPlaying = overlay.stanceW == 1 and overlay.crouchStance or overlay.standStance
	player:StartAnimation(0, overlay.aimPlaying, overlay.layer, 0, overlay.stanceSpeed, true)
	overlay.twist, overlay.twistStable, overlay.hipsLast, overlay.gridDir, overlay.twistW = 0, 0, nil, 0, 0
	overlay.gunFix, overlay.gunRef, overlay.riseLast = 0, nil, nil
	System.LogAlways("M4CARBINE overlay aim " .. overlay.aimPlaying)
end

local function wrap(a) return (a + 180) % 360 - 180 end
local function clamp(x, lo, hi) return math.max(lo, math.min(hi, x)) end

-- The hips' yaw against the view, from the thigh joints. Layer 9 keys no hip
-- or leg bone, so the torso turn below never moves it.
function M4Carbine.HipsYaw(player)
	local l, r = player:GetBonePos('LeftUpLeg'), player:GetBonePos('RightUpLeg')
	local vd = M4Carbine.AimAxes(player)
	return wrap(math.deg(math.atan2(l.y - r.y, l.x - r.x)) - math.deg(math.atan2(vd.y, vd.x)))
end

-- After a spin, a sprint stop or a strafe the hips under the overlay point
-- away from the view, up to 117 deg after a left strafe with the gun lowered,
-- and swing back at up to 1200 deg/s once aimed; the aim poses only correct
-- pitch, so the gun rode them and the shoulders showed (logger 2026-09-23).
-- The stance clips carry their hold turned about the world up at Spine
-- (kcd2-forge-stance-blend.py --twist-deg 135). With look IK off (SyncLookIK)
-- the settled torso sits a fixed turn off the hips, so the torso turns back by
-- the hips' offset from their settled angle, one frame ahead, past a
-- twistDead dead zone. It used to chase the shoulder line, which showed each
-- turn a frame or two late, so it took 30% steps and ignored 5 deg to stay
-- stable, and a strafe aim swung the gun 6 to 27 deg past the center while
-- the hips came back (logger 2026-09-23). Settled, the hips sit 64.5 deg off
-- the view standing, still or walking any way (logger 2026-09-23); the
-- crouched angle is learned while Henry and the hips hold still. Only that one:
-- an aim out of a walk once settled with the hips turned, the standing angle
-- learned 33 deg, and every later aim started off and crept back to the
-- center over a second (logger 2026-10-02). The engine's
-- own torso turn toward the view fades over the first 4 frames of an aim from
-- a strafe, and a turn faster than twistRate swung the gun past the center in
-- those frames (replay of the logged frames). Crouched, the settled hips sit
-- 56.7 deg off the view (A723 M4SW, twice), so mid-blend the hips' reference and,
-- while Henry moves, the turn's share follow the stance blend w. Walking aimed,
-- the thighs swing 8 deg either way with a crouch walk stride while the gun holds
-- still, and a crouch while walking turned the gun 12 deg off the sights and
-- back as the turn chased them (A728 M4SW). So on the stance clips a walk turns
-- the torso back only past twistWalkDead. The raise kept twistDead, and a raise
-- while running turned the torso 15 deg more than the stance clip then did: the
-- gun swung 10-17 deg as the raise handed over (A733 M4SW), so it moves on the
-- same dead zone. A dead zone that only trims the turn left the gun 14 deg off
-- the view while the hips swung back after a strafe aim (A735 M4SW, user: "the
-- weapon sways to the side when I walk and aim"), so once the hips pass
-- twistWalkDead the turn follows them fully until they come back within twistDead.
-- Each shot swings the standing hips 25 deg and back, and the turn left the
-- gun twistDead off the view through every burst (A774 M4SW, user: "the weapon
-- strays leftwards when shooting"), so past the dead zone the part it leaves
-- fades to nothing over twistBand.
function M4Carbine.Twist(player, own, w)
	local overlay = M4Carbine.overlay
	local dt = System.GetFrameTime()
	if dt <= 0 then return end
	local hips = M4Carbine.HipsYaw(player)
	local turn = overlay.hipsLast and wrap(hips - overlay.hipsLast) or 0
	local v = player:GetVelocity()
	local still = v.x * v.x + v.y * v.y < 0.09 and M4Carbine.Now() - overlay.movedAt >= overlay.stillAfter
	if overlay.hipsLast and own and still and overlay.gridDir == 0 and math.abs(turn) < 10 * dt then
		overlay.twistStable = overlay.twistStable + dt
	else
		overlay.twistStable = 0
	end
	overlay.hipsLast = hips
	local ref = overlay.hipsRef[overlay.aimPlaying]
	if overlay.twistStable > 0.15 and overlay.aimPlaying == overlay.crouchStance then
		ref = ref and ref + wrap(hips - ref) * 0.1 or hips
		overlay.hipsRef[overlay.aimPlaying] = ref
	end
	w = w or 0
	local crouching = overlay.aimPlaying == overlay.crouchStance
	local to = overlay.hipsRef[crouching and overlay.standStance or overlay.crouchStance]
	if ref and to then ref = ref + wrap(to - ref) * w end
	local share = still and 1 or 1 - (crouching and 1 - w or w)
	local target = 0
	if ref and share > 0 then
		local off = wrap(hips + turn - ref)
		if still or math.abs(off) > overlay.twistWalkDead then
			overlay.twistFull = true
		elseif math.abs(off) < overlay.twistDead then
			overlay.twistFull = false
		end
		local dead = (overlay.twistFull or overlay.stanceW < overlay.twistStandW) and overlay.twistDead or overlay.twistWalkDead
		local keep = clamp((dead + overlay.twistBand - math.abs(off)) / overlay.twistBand, 0, 1)
		target = share * clamp(clamp(off, -dead, dead) * keep - off, 3 - overlay.twistMax, overlay.twistMax - 3)
	end
	local step = overlay.twistRate * dt
	overlay.twist = overlay.twist + clamp(target - overlay.twist, -step, step)
end

-- A hit logs M4HIT frames for watch.hitSeconds (Watch).
function M4Carbine.HitTick(player)
	local overlay, now, health = M4Carbine.overlay, M4Carbine.Now(), player.actor:GetHealth()
	if overlay.health and health < overlay.health - 0.5 then
		M4Carbine.watch.hitAt = now
		System.LogAlways(string.format("M4HIT t=%.3f health %.1f to %.1f state %s", now, overlay.health, health, tostring(overlay.state)))
	end
	overlay.health = health
end

-- The raise clip carried no torso turn, so the gun rode the hips' swing back
-- through the raise: 25 to 50 deg past the center after a strafe, the shoulder
-- on screen after a sprint and a quick turn (logger 2026-09-23). Cutting the
-- raise to the stance clip fixed that, but the gun then popped into the aim
-- (user 2026-09-23: "the weapon literally SPAWNS on screen when sprinting and
-- then aiming"). A layer 9 clip scrubs along one axis and a crossfaded one never
-- scrubs, so the raise clip is now a grid (kcd2-forge-raise-grid.py): gridSteps
-- copies of the raise, each turned by the gridTurns 0, 45, 90, 135, -135, -90,
-- -45 and 0 deg at Spine, then the standing stance clip. Its key 0 is the idle
-- hold, so the raise starts where the gun is. GridScrub holds the raise step
-- for raiseP and scrubs the turn inside it; the lower runs the same steps back
-- over lowerSeconds while the turn fades with them, and a re-aim during the
-- lower turns around on the same clip. twistW eases the turn in over twistIn,
-- while the engine's own torso turn toward the view fades.
function M4Carbine.GridRaise(player)
	local overlay = M4Carbine.overlay
	overlay.fadeToken = overlay.fadeToken + 1
	overlay.raiseAt = M4Carbine.Now()
	if overlay.aimPlaying ~= overlay.raiseClip or overlay.gridDir >= 0 then
		M4Carbine.StartGrid(player)
		overlay.aimPlaying, overlay.raiseP, overlay.twistW = overlay.raiseClip, 0, 1
		overlay.twist, overlay.twistStable, overlay.hipsLast = 0, 0, nil
		overlay.gunFix, overlay.gunRef, overlay.riseLast = 0, nil, nil
		overlay.stanceW = 0
	end
	overlay.gridDir = 1
	overlay.state = "aim"
	System.LogAlways(string.format("M4CARBINE overlay raise grid from %.2f", overlay.raiseP))
end

-- The raise grid started over another clip shows a frame or two late, and a
-- time set before it shows is dropped. The raise ran on unseen, and its first
-- shown frame jumped the gun 27 deg after a spin (A727 M4SW), so the grid
-- holds still until the time it sets reads back, or for gridWait at most.
function M4Carbine.StartGrid(player)
	local overlay = M4Carbine.overlay
	player:StartAnimation(0, overlay.raiseClip, overlay.layer, 0, overlay.stanceSpeed, true)
	overlay.gridLive, overlay.gridAt = false, M4Carbine.Now()
end

function M4Carbine.GridScrub(player)
	local overlay = M4Carbine.overlay
	if overlay.gridDir == 0 or overlay.aimPlaying ~= overlay.raiseClip then return end
	overlay.gridLive = overlay.gridLive or M4Carbine.Now() - overlay.gridAt > overlay.gridWait
	local dt = overlay.gridLive and System.GetFrameTime() or 0
	if overlay.gridDir > 0 then
		overlay.raiseP = math.min(1, overlay.raiseP + dt / overlay.raiseSeconds)
		overlay.twistW = math.min(1, overlay.twistW + dt / overlay.twistIn)
		if overlay.raiseP >= 1 and overlay.twistW >= 1 then
			overlay.gridDir = 0
			return
		end
	else
		overlay.raiseP = math.max(0, overlay.raiseP - dt / overlay.lowerSeconds)
		overlay.twistW = math.min(overlay.twistW, overlay.raiseP)
		if overlay.raiseP <= 0 then
			overlay.gridDir, overlay.aimPlaying = 0, nil
			overlay.idleClip = M4Carbine.IdleClip()
			player:StartAnimation(0, overlay.idleClip, overlay.layer, 0, 1, true)
			return
		end
	end
	M4Carbine.Twist(player, true)
	local key = M4Carbine.GridKey(math.floor(overlay.raiseP * (overlay.gridSteps - 1) + 0.5), overlay.twistW * overlay.twist)
	local time = key / (overlay.gridKeys - 1)
	player:SetAnimationTime(0, overlay.layer, time)
	overlay.gridLive = overlay.gridLive or math.abs((player:GetAnimationTime(0, overlay.layer) or -1) - time) < 0.001
end

-- The raise grid and the stance grids share one layout: each step holds the
-- turns 0, 45, 90, 135, -135, -90, -45 and 0 deg, and a turn scrubs between two.
function M4Carbine.GridKey(step, turn)
	local overlay = M4Carbine.overlay
	turn = clamp(turn, 3 - overlay.twistMax, overlay.twistMax - 3)
	return step * overlay.gridTurns + (turn >= 0 and turn / 45 or overlay.gridTurns - 1 + turn / 45)
end

-- The horse aim pose turns the torso after the view by an amount that changes with
-- the direction, so no one mounted hold stays on the sight line (A608). The mount grid
-- is that hold turned by every yaw and pitch offset (kcd2-forge-mount-hold.py
-- --grid-into); each frame the sight's miss against the view moves the offset,
-- so the sights stay centred wherever the rider looks. While the view turns faster
-- than turnRate deg/s the miss is the torso catching up, and steering on it wound
-- the offset to its end and swung the gun past the view (A612), so it waits.
-- In the saddle the crosshair dot sits 56 px above the screen centre, and the sights
-- sat on its height 27 px left of it, so the steering aims aimYaw right (A613).
function M4Carbine.PlayMountGrid(player)
	local overlay = M4Carbine.overlay
	overlay.mountFix = {y = 0, p = 0, a = 0, b = 0, vel = {0, 0, 0, 0}, at = M4Carbine.Now(), r = overlay.mountGrid.roll, row = (overlay.mountGrid.yaws - 1) / 2, rollRow = (overlay.mountGrid.rolls - 1) / 2}
	overlay.aimPlaying = overlay.mountGridClip
	player:StartAnimation(0, overlay.mountGridClip, overlay.layer, 0, overlay.stanceSpeed, true)
	player:StartAnimation(0, overlay.mountEyeClip, overlay.mountGrid.eye.layer, 0, overlay.stanceSpeed, true)
	overlay.eyeOn = true
	M4Carbine.MountScrub(player)
	System.LogAlways("M4CARBINE overlay mount grid")
end

function M4Carbine.SightRoll(player)
	local sight = M4CarbineSightProfile
	local x, y, z = M4Carbine.BoneAxes(player, 'RightHandSlot')
	local front, rear = sight.front, sight.rear
	local dx, dy, dz = front.x - rear.x, front.y - rear.y, front.z - rear.z
	local k = dx / (dx * dx + dy * dy + dz * dz)
	local sx, sy, sz = 1 - dx * k, -dy * k, -dz * k
	local gx, gy, gz = x.x * sx + x.y * sy + x.z * sz, y.x * sx + y.y * sy + y.z * sz, z.x * sx + z.y * sy + z.z * sz
	local v, u = System.GetViewCameraDir(), System.GetViewCameraUpDir()
	local rx, ry, rz = v.y * u.z - v.z * u.y, v.z * u.x - v.x * u.z, v.x * u.y - v.y * u.x
	return math.deg(math.atan2(gx * u.x + gy * u.y + gz * u.z, gx * rx + gy * ry + gz * rz))
end

function M4Carbine.RollReference(player)
	local overlay = M4Carbine.overlay
	if overlay.state ~= "aim" or M4Carbine.Mounted() then return end
	local grid = overlay.mountGrid
	grid.rollRef = grid.rollRef + 0.05 * ((M4Carbine.SightRoll(player) - grid.rollRef + 180) % 360 - 180)
end

local function mountDot(a, b) return a.x * b.x + a.y * b.y + a.z * b.z end
local function mountPoint(out, o, x, y, z, q)
	out.x, out.y, out.z = o.x + mountDot(x, q), o.y + mountDot(y, q), o.z + mountDot(z, q)
	return out
end
local function mountSpan(player, m, rear, w) return mountDot(m, player:VectorToLocal(0, w)) - mountDot(rear, w) end
local mountKeys = {"y", "p", "a", "b"}
local mountLimits, mountQuanta, mountPoles = {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}
local scrubD, scrubF, scrubRear = {x = 0, y = 0, z = 0}, {x = 0, y = 0, z = 0}, {x = 0, y = 0, z = 0}
local steerM, steerE1, steerE2 = {x = 0, y = 0, z = 0}, {x = 0, y = 0, z = 0}, {x = 0, y = 0, z = 0}
local camRear, camFront, camLine, camAngles = {x = 0, y = 0, z = 0}, {x = 0, y = 0, z = 0}, {x = 0, y = 0, z = 0}, {x = 0, y = 0, z = 0}

function M4Carbine.MountScrub(player)
	local overlay = M4Carbine.overlay
	local grid, fix = overlay.mountGrid, overlay.mountFix
	if overlay.state ~= "aim" or overlay.aimPlaying ~= overlay.mountGridClip or not fix or not M4Carbine.Mounted() then return end
	local sight = M4CarbineSightProfile
	local x, y, z = M4Carbine.BoneAxes(player, 'RightHandSlot')
	local dot = mountDot
	local d = scrubD
	d.x, d.y, d.z = sight.front.x - sight.rear.x, sight.front.y - sight.rear.y, sight.front.z - sight.rear.z
	local f = scrubF
	f.x, f.y, f.z = dot(x, d), dot(y, d), dot(z, d)
	local v, u, r = M4Carbine.AimAxes(player)
	local ahead = dot(f, v)
	if ahead <= 0 then return end
	local yaw, pitch = math.deg(math.atan2(dot(f, r), ahead)) - grid.aimYaw, math.deg(math.atan2(dot(f, u), ahead)) - grid.aimPitch
	if not overlay.camOwned then
		local rear = scrubRear
		rear.x, rear.y, rear.z = dot(x, sight.rear), dot(y, sight.rear), dot(z, sight.rear)
		M4Carbine.MountSteer(player, yaw, pitch, rear, v, u, r)
	end
	local row = (fix.y + grid.yawSpan) / grid.yawStep
	if math.abs(row - fix.row) > 0.5 + grid.hold then fix.row = math.floor(row + 0.5) end
	local cant = (M4Carbine.SightRoll(player) - grid.rollRef + 180) % 360 - 180
	fix.r = clamp(fix.r - grid.rollSign * grid.gain * cant, -grid.rollSpan, grid.rollSpan)
	local rollRow = (fix.r + grid.rollSpan) / grid.rollStep
	if math.abs(rollRow - fix.rollRow) > 0.5 + grid.hold then fix.rollRow = math.floor(rollRow + 0.5) end
	local key = ((fix.rollRow * grid.downs + grid.down) * grid.yaws + fix.row) * grid.pitches + (fix.p + grid.pitchSpan) / grid.pitchStep
	player:SetAnimationTime(0, overlay.layer, key / (grid.rolls * grid.downs * grid.yaws * grid.pitches - 1))
	local eye = grid.eye
	if overlay.eyeOn then
		local eyeRow = clamp(math.floor((fix.b + eye.span) / eye.step + 0.5), 0, eye.rows - 1)
		local eyeKey = (eye.plane * eye.rows + eyeRow) * 2 + clamp((fix.a + eye.span) / (2 * eye.span), 0, 1)
		player:SetAnimationTime(0, eye.layer, math.min(eyeKey / (eye.planes * eye.rows * 2 - 1), 0.99999))
	end
end

function M4Carbine.MountSteer(player, yaw, pitch, rear, v, u, r)
	local overlay = M4Carbine.overlay
	local grid, fix = overlay.mountGrid, overlay.mountFix
	local eye = grid.eye
	local dot = mountDot
	local c, o = player:GetHelperPos('camera', true), player:GetHelperPos('RightHandSlot', true)
	local m = steerM
	m.x, m.y, m.z = c.x - o.x, c.y - o.y, c.z - o.z
	local hx, hy, hz = M4Carbine.BoneAxes(player, 'Head')
	local e1, e2 = steerE1, steerE2
	e1.x, e1.y, e1.z = dot(hx, eye.right), dot(hy, eye.right), dot(hz, eye.right)
	e2.x, e2.y, e2.z = dot(hx, eye.up), dot(hy, eye.up), dot(hz, eye.up)
	local m11, m12, m21, m22 = dot(e1, r), dot(e2, r), dot(e1, u), dot(e2, u)
	local det = m11 * m22 - m12 * m21
	local want = {yaw, -pitch, 0, 0}
	if math.abs(det) > 0.2 then
		local sr, su = -mountSpan(player, m, rear, r), -mountSpan(player, m, rear, u)
		want[3], want[4] = (sr * m22 - m12 * su) / det, (m11 * su - sr * m21) / det
	end
	local last = fix.want
	local ramp = math.min(1, (M4Carbine.Now() - fix.at) / grid.steerRamp)
	local keys, limits, quanta, poles = mountKeys, mountLimits, mountQuanta, mountPoles
	limits[1], limits[2], limits[3], limits[4] = grid.yawSpan, grid.pitchSpan, eye.span, eye.span
	quanta[1], quanta[2], quanta[3], quanta[4] = grid.yawStep, 0, 0, eye.step
	poles[1], poles[2], poles[3], poles[4] = grid.steerPole, grid.steerPole, eye.pole, eye.pole
	for i = 1, 4 do
		local key = keys[i]
		local pole = poles[i]
		fix.vel[i] = last and ramp == 1 and fix.vel[i] + (1 - pole) ^ 2 * want[i] or 0
		local to = clamp(fix[key] + (1 - pole * pole) * (0.1 + 0.9 * ramp) * want[i] + fix.vel[i], -limits[i], limits[i])
		if math.abs(to) == limits[i] then fix.vel[i] = 0 end
		if quanta[i] > 0 then to = math.floor(to / quanta[i] + 0.5) * quanta[i] end
		fix[key] = to
	end
	fix.want = want
	if not M4Carbine.trace then return end
	local w = player:GetWorldPos()
	local chest = player:VectorToLocal(0, player:GetBoneLocal('Spine4', M4Carbine.units[1]))
	System.LogAlways(string.format('M4MNT t=%.3f w=%.2f,%.2f miss=%.3f,%.3f eye=%.2f,%.2f relief=%.1f chest=%.4f,%.4f,%.4f fix=%.2f,%.2f,%.2f,%.2f',
		M4Carbine.Now(), w.x, w.y, yaw, pitch, want[3] * 1000, want[4] * 1000, -mountSpan(player, m, rear, v) * 1000, chest.x, chest.y, chest.z, fix.y, fix.p, fix.a * 1000, fix.b * 1000))
end

-- Steering the gun onto the view never held: the horse aim pose moved the sights
-- off the eye by up to 5 cm, a new amount in every direction (A623 logs). So in
-- the saddle the view follows the gun instead: the camera sits eye_relief behind
-- the rear sight and looks down the sight line, camDrop degrees lower and camYaw
-- degrees left so the ring lands on the HUD dot (live test 2026-10-05).
function M4Carbine.MountCamera(player)
	local overlay = M4Carbine.overlay
	local grid = overlay.mountGrid
	if not overlay.eyeOn and not overlay.camOwned and not M4Carbine.Mounted() then
		grid.camW = nil
		return
	end
	-- While the horse moves an owned view drags the aim off the sights (scripted ride
	-- 2026-10-05), so the game camera takes over until the horse has stood camSettle s.
	local w, now, lastW = player:GetWorldPos(), M4Carbine.Now(), grid.camW
	grid.camW = {x = w.x, y = w.y, z = w.z}
	if lastW and (w.x - lastW.x) ^ 2 + (w.y - lastW.y) ^ 2 > grid.camMove ^ 2 then grid.camMovedAt = now end
	local aiming = overlay.state == "aim" and M4Carbine.Mounted()
	if overlay.eyeOn and not (aiming and overlay.aimPlaying == overlay.mountGridClip) then
		overlay.eyeOn = false
		player:StartAnimation(0, overlay.blank, grid.eye.layer, overlay.aimBlend, 1, true)
		M4Carbine.At(overlay.aimBlend + 0.1, function()
			if not overlay.eyeOn then pcall(function() M4Carbine.Dude():StopAnimation(0, grid.eye.layer) end) end
		end)
	end
	if grid.steer or not aiming or now - (grid.camMovedAt or -1) < grid.camSettle then
		if overlay.camOwned then
			overlay.camOwned, grid.camLast = false, nil
			pcall(function() player.actor:PlayerSetViewAngles(player.actor:GetAngles()) end)
			CryAction.ResetToNormalCamera()
		end
		return
	end
	local sight = M4CarbineSightProfile
	local o = player:GetBonePos('RightHandSlot')
	local x, y, z = M4Carbine.BoneAxes(player, 'RightHandSlot')
	local dot = mountDot
	local rear, front = mountPoint(camRear, o, x, y, z, sight.rear), mountPoint(camFront, o, x, y, z, sight.front)
	local d = camLine
	d.x, d.y, d.z = front.x - rear.x, front.y - rear.y, front.z - rear.z
	local n = math.sqrt(dot(d, d))
	d = {x = d.x / n, y = d.y / n, z = d.z / n}
	local eye = {x = rear.x - d.x * sight.eye_relief, y = rear.y - d.y * sight.eye_relief, z = rear.z - d.z * sight.eye_relief}
	-- Each saddle shot snaps the gun up to 70 deg for one frame (A623 frame log), so a
	-- jump past camJump keeps the last view for up to camHoldMax frames.
	local last = grid.camLast
	if last and math.deg(math.acos(clamp(dot(d, last.d), -1, 1))) > grid.camJump and grid.camHeld < grid.camHoldMax then
		grid.camHeld = grid.camHeld + 1
		eye, d = last.eye, last.d
	else
		grid.camHeld, grid.camLast = 0, {eye = eye, d = d}
	end
	local angles = camAngles
	angles.x, angles.y, angles.z = math.deg(math.asin(d.z)) - grid.camDrop, 0, math.deg(math.atan2(-d.x, d.y)) + grid.camYaw
	CryAction.SetViewCameraByAngles(eye, angles)
	overlay.camOwned = true
end

-- The body's stance change and its hips' height are not in step: standing up
-- while walking aimed, the body turns the gun 4 deg before the hips rise past
-- stanceWalkBottom and 6 deg more after they pass stanceWalkTop, so the gun
-- swung 5-8 deg off the sights and back on every toggle (A731 M4SW). While the
-- hips rise or sink faster than shiftRise, and for shiftHold after, the torso
-- turns the gun back to the yaw it held settled; after that the fix fades at
-- gunFade deg/s. Settled, the gun's yaw against the view is learned. A fix that
-- added gunGain of each frame's miss wound up and swung the gun 2 deg past the
-- other way as the body settled (A732 M4SW), so the fix is now the miss the body
-- alone makes: the gun read less the fix it was drawn with a frame earlier.
-- The walking bob moves the hips up to 0.56 m/s, so the hold also ran on every
-- step and steered on the gun's lag behind a turning view: the gun swung 11 deg
-- (A736 M4SW, user: "plenty of times where the gun sways"). It runs only within
-- pressWindow of a stance press.
function M4Carbine.GunHold(player, now, dt)
	local overlay = M4Carbine.overlay
	local hips = overlay.lastHips
	if dt <= 0 or not hips then return end
	if overlay.riseLast and math.abs(hips - overlay.riseLast) > overlay.shiftRise * dt and now - overlay.stancePressAt < overlay.pressWindow then overlay.shiftAt = now end
	overlay.riseLast = hips
	local d = player:GetBoneDir('RightHandSlot')
	local vd = M4Carbine.AimAxes(player)
	local gun = wrap(math.deg(math.atan2(d.y, d.x)) - math.deg(math.atan2(vd.y, vd.x)))
	if now - overlay.shiftAt < overlay.shiftHold and overlay.gunRef then
		overlay.gunFix = clamp(overlay.gunGain * wrap(overlay.gunRef - (gun - overlay.gunFix)), -overlay.gunMax, overlay.gunMax)
		return
	end
	local fade = overlay.gunFade * dt
	overlay.gunFix = overlay.gunFix - clamp(overlay.gunFix, -fade, fade)
	if overlay.gunFix == 0 and (overlay.stanceW == 0 or overlay.stanceW == 1) then
		overlay.gunRef = overlay.gunRef and overlay.gunRef + wrap(gun - overlay.gunRef) * 0.1 or gun
	end
end

function M4Carbine.StanceScrub(player)
	local overlay = M4Carbine.overlay
	if not M4Carbine.OnStance() or not overlay.aimHeld or overlay.gridDir ~= 0 then return end
	local now = M4Carbine.Now()
	local v = player:GetVelocity()
	if v.x * v.x + v.y * v.y > 0.04 then overlay.movedAt = now end
	local crouched = M4Carbine.Crouched()
	local walking = now - overlay.movedAt < overlay.stillAfter
	local top = walking and overlay.stanceWalkTop or overlay.stanceTop
	local bottom = walking and overlay.stanceWalkBottom or overlay.stanceBottom
	local dt = System.GetFrameTime()
	if overlay.lastHips and not overlay.landing then
		local blend = crouched or overlay.stanceW > 0 or now - overlay.stancePressAt < overlay.pressWindow
		local w = blend and clamp((top - overlay.lastHips) / (top - bottom), 0, 1) or 0
		local step = now - overlay.snapAt < 0.05 and 1 or dt / overlay.stanceSlew
		overlay.stanceW = overlay.stanceW + clamp(w - overlay.stanceW, -step, step)
	end
	-- The stance clips are grids (kcd2-forge-stance-grid.py), so the torso turn
	-- holds through the blend. The blend keys alone carried no turn: it dropped
	-- as Henry crouched while aimed with the hips off the view, and the gun swung
	-- 10 deg in one frame (A722 M4SW), or 26 deg off the sights when it faded (A723).
	local w = overlay.aimPlaying == overlay.crouchStance and 1 - overlay.stanceW or overlay.stanceW
	M4Carbine.Twist(player, w < 0.001, w)
	-- A crouched raise lands on the unturned aim hold, and the stance clip took
	-- the turn in one frame: the gun swung 10-18 deg (A724 M4SW, crouch and aim spam).
	overlay.twistW = math.min(1, overlay.twistW + dt / overlay.stanceRamp)
	M4Carbine.GunHold(player, now, dt)
	local key = M4Carbine.GridKey(math.floor(w * (overlay.stanceSteps - 1) + 0.5), overlay.twistW * overlay.twist + overlay.gunFix)
	local base, keys = 0, overlay.clipKeys
	if overlay.aimPlaying == overlay.raiseClip then base, keys = overlay.gridSteps * overlay.gridTurns, overlay.gridKeys end
	player:SetAnimationTime(0, overlay.layer, math.min((base + key) / (keys - 1), 0.99999))
end

-- In the saddle the overlay plays the mounted idle's arms (mountClip), so every
-- gait keeps the mounted hold while the body still rides.
function M4Carbine.IdleClip()
	if M4Carbine.Mounted() then return M4Carbine.overlay.mountClip end
	if M4Carbine.Crouched() then return M4Carbine.overlay.crouchClip end
	return M4Carbine.overlay.clip
end

function M4Carbine.OverlayWanted(player)
	local overlay = M4Carbine.overlay
	if not (player.human:IsWeaponDrawn() and M4Carbine.HoldsCarbine()) then
		overlay.drawnSince = nil
		overlay.holstering = false
		return false
	end
	-- The M4 holster clip starts in the overlay's hold and its row blends in over
	-- 0.05 s, so the overlay leaves as the clip starts and the clip alone moves the hands.
	if overlay.holstering then return false end
	local now = M4Carbine.Now()
	local pressed = overlay.pressAt and now - overlay.pressAt < 2
	overlay.drawnSince = overlay.drawnSince or (pressed and overlay.drawStartAt) or (overlay.loaded and not overlay.afterTalk and now) or now - overlay.drawSeconds
	-- A standing raise straight off the draw handed the faded aim hold to the
	-- stance clip with a 10.5 cm snap (A686 M4QUIRK), so an aim held through
	-- the draw comes on the hold first and raises on the grid from there.
	-- A reload plays from the hold, so an aim held through it waits for its end
	-- (A758, user: "while aiming and reloading, the animation is completely mangled").
	local aiming = overlay.aimHeld and not M4Carbine.mag.reloading
	if M4Carbine.PressedDraw() then return not M4Carbine.Drawing() and (not aiming or overlay.state == nil) end
	local clipless = now - overlay.drawnSince >= overlay.drawGrace and not player:IsAnimationRunning(0, overlay.itemLayer)
	return not aiming and (clipless or now - overlay.drawnSince >= overlay.drawSeconds)
end

-- An aim pressed within drawSeconds of a draw, as after mounting or dismounting,
-- comes before the hold is back on, so it fades onto the aim hold from the vanilla
-- arms instead of staying on them for the whole aim.
function M4Carbine.AimFromOff(player)
	local overlay = M4Carbine.overlay
	return overlay.state == nil and overlay.aimHeld and not overlay.holstering and not M4Carbine.Drawing() and player.human:IsWeaponDrawn() and M4Carbine.HoldsCarbine()
end

-- A quick slot draw: the game ignores an aim pressed while its clip plays, so
-- the overlay raised a gun that never charged and the shots did nothing until
-- a recharge 1.3 s later (A681, user: "drawing and aiming enters this weird
-- state"). The raise waits for the draw, runs on the grid like an aim from the
-- hold, and asks for the recharge at once.
function M4Carbine.PressedDraw()
	local overlay, now = M4Carbine.overlay, M4Carbine.Now()
	return overlay.pressAt ~= nil and now - overlay.pressAt < 2 and overlay.drawStartAt ~= nil and overlay.drawStartAt >= overlay.pressAt
end

-- The draw clip holds the gun still from drawPlateau, then its tail drops it
-- 20 cm to the vanilla carry before the clip ends 1.4 s after the M4 is in the
-- hand. The hold came on with no blend at 1.02 s, in that tail, and the aim
-- waited for it (A683, user: "it is fucking slow" and "a little jerk at the
-- end of the draw animiation"; M4PATH 2026-10-06). The draw now ends at the
-- plateau: the hold or the aim fades in over drawFade, and the tail it hides
-- plays drawTailSpeed times faster, so the game takes the aim sooner.
function M4Carbine.Drawing()
	local player, layer = M4Carbine.Dude(), M4Carbine.overlay.itemLayer
	return M4Carbine.PressedDraw() and player:IsAnimationRunning(0, layer) and (player:GetAnimationTime(0, layer) or 1) < M4Carbine.overlay.drawPlateau
end

-- The game ignores an aim press while the draw clip plays, so the plugin taps
-- the aim once as the clip ends; a tap is a no-op unless the right button is
-- down, and RefusedGuard retries one the game refuses. Taps every 0.3 s (A685)
-- and then 0.7 s (A690) re-pressed each aim before its charge showed 0.47-0.66 s
-- later, so the gun never took the view (user: "same issue").
function M4Carbine.DrawTap()
	local overlay, charge = M4Carbine.overlay, M4Carbine.charge
	local player = M4Carbine.Dude()
	if not (M4Carbine.inHand and not overlay.holstering and M4Carbine.PressedDraw()) then return end
	local running, ended = player:IsAnimationRunning(0, overlay.itemLayer), overlay.drawRunning
	overlay.drawRunning = running
	-- The game can drop an aim press during the draw before any script sees
	-- it, so the draw's end owes a tap (user, A682: "draw m4 and just hold aim
	-- and have it aim when it is ready").
	-- The clip blips back on for a frame after it ends, so the tap is owed once.
	if overlay.drawOwed == nil and ended and not running then overlay.drawOwed = true end
	-- An aim held through the draw still waited 1.2 s from the key for the
	-- plateau (A687, user: "still very slow"), so with the right button down
	-- the draw clip plays drawAimSpeed times faster.
	if running and not overlay.drawRushed and (overlay.rmb or overlay.aimHeld) and M4Carbine.Drawing() then
		overlay.drawRushed = true
		player:SetAnimationSpeed(0, overlay.itemLayer, overlay.drawAimSpeed)
	end
	if overlay.state ~= nil and not overlay.drawSped and running and not M4Carbine.Drawing() then
		overlay.drawSped = true
		player:SetAnimationSpeed(0, overlay.itemLayer, overlay.drawTailSpeed)
	end
	if overlay.drawOwed then
		overlay.drawOwed = false
		if not player:IsAnimationRunning(0, charge.chargeLayer) then charge.pending = true end
	end
end

-- An aim held while the M4 comes into the hand fades from the draw straight
-- onto the aim (A688, user: "i expect the draw to blend into the AIM if aim is
-- held down... it should not play the entire draw animation first"). The stance
-- clip's first key is the untwisted aim, and a crossfaded clip never scrubs, so
-- the fade plays it near that key and it restarts unblended once the fade is in.
function M4Carbine.DrawAimReady(player)
	local overlay = M4Carbine.overlay
	return overlay.state == nil and overlay.aimHeld and not overlay.holstering and M4Carbine.PressedDraw() and not M4Carbine.Mounted() and player.human:IsWeaponDrawn() and M4Carbine.HoldsCarbine()
end

function M4Carbine.DrawAim(player)
	local overlay = M4Carbine.overlay
	local crouched = M4Carbine.Crouched()
	overlay.fadeToken = overlay.fadeToken + 1
	local token = overlay.fadeToken
	overlay.state, overlay.gridDir, overlay.raiseAt = "fading", 0, M4Carbine.Now()
	player:StartAnimation(0, crouched and overlay.crouchStance or overlay.standStance, overlay.layer, overlay.drawAimBlend, overlay.stanceSpeed, true)
	System.LogAlways("M4CARBINE overlay draw into aim " .. overlay.drawAimBlend)
	M4Carbine.At(overlay.drawAimBlend, function()
		if overlay.fadeToken ~= token or overlay.state ~= "fading" then return end
		local dude = M4Carbine.Dude()
		if overlay.aimHeld and M4Carbine.HoldsCarbine() then
			-- The gun follows the view only once the game takes the aim, which
			-- waits for the draw clip, now hidden: it plays out drawHiddenSpeed
			-- times faster (A689, user: "the weapon does not follow the aim for
			-- some time").
			if dude:IsAnimationRunning(0, overlay.itemLayer) then
				overlay.drawRushed, overlay.drawSped = true, true
				dude:SetAnimationSpeed(0, overlay.itemLayer, overlay.drawHiddenSpeed)
			end
			overlay.state = "aim"
			if crouched then
				overlay.aimPlaying = overlay.crouchAimClip
				M4Carbine.PlayStance(dude)
				return
			end
			-- The stance clip took the torso turn at once and the gun snapped 5-8 cm
			-- (A689 M4QUIRK); the grid's last raise step is the same aim and
			-- eases the turn in over twistIn.
			M4Carbine.StartGrid(dude)
			overlay.aimPlaying, overlay.raiseP, overlay.twistW = overlay.raiseClip, 1, 0
			overlay.twist, overlay.twistStable, overlay.hipsLast = 0, 0, nil
			overlay.stanceW, overlay.gridDir = 0, 1
			M4Carbine.GridScrub(dude)
			System.LogAlways("M4CARBINE overlay draw aim on the grid")
		else
			dude:StopAnimation(0, overlay.layer)
			overlay.state = nil
		end
	end)
end

local function overlaySyncBody(overlay, fadeIn, fadeOut)
		local player = M4Carbine.Dude()
		if overlay.disabled or not player or not player.human then
			overlay.state = nil
			return
		end
		local wanted = M4Carbine.OverlayWanted(player)
		if wanted and overlay.state ~= "on" then
			if not overlay.loaded then
				overlay.loaded = true
				if not (overlay.pressAt and M4Carbine.Now() - overlay.pressAt < 2) then fadeIn = 0 end
			end
			overlay.idleClip = M4Carbine.IdleClip()
			if M4Carbine.PressedDraw() or M4Carbine.Now() - overlay.drawnSince < overlay.drawSeconds then fadeIn = overlay.drawFade end
			player:StartAnimation(0, overlay.idleClip, overlay.layer, fadeIn, 1, true)
			overlay.state, overlay.gridDir, overlay.onUntil = "on", 0, M4Carbine.Now() + fadeIn
			System.LogAlways("M4CARBINE overlay on blend " .. fadeIn)
		elseif not wanted and M4Carbine.DrawAimReady(player) then
			M4Carbine.DrawAim(player)
		elseif not wanted and (overlay.state == "on" or M4Carbine.AimFromOff(player)) and M4Carbine.Now() >= math.max(overlay.holdUntil, overlay.onUntil) then
			local lowering = overlay.aimPlaying == overlay.raiseClip and overlay.gridDir < 0
			local afterDraw = overlay.state == nil and M4Carbine.PressedDraw()
			if overlay.state == "on" and overlay.aimHeld and not M4Carbine.Crouched() and not M4Carbine.Mounted() and (lowering or M4Carbine.Now() - overlay.lowerAt >= overlay.lowerSeconds) then
				if not M4Carbine.LandWait(player) then M4Carbine.GridRaise(player) end
				return
			end
			overlay.gridDir = 0
			overlay.state = "fading"
			overlay.fadeToken = overlay.fadeToken + 1
			local token = overlay.fadeToken
			local raising = overlay.aimHeld
			local raiseBlend = M4Carbine.PressedDraw() and overlay.drawFade or overlay.crouchRaiseSeconds
			if raising then
				overlay.raiseAt = M4Carbine.Now()
				-- A crouched or mounted raise, and a re-aim during a lower that did not run on the
				-- grid, fade straight onto the aim hold: the idle arms would snap the
				-- gun back down in one frame (logger 2026-09-23).
				overlay.aimPlaying = M4Carbine.AimClip()
				player:StartAnimation(0, overlay.aimPlaying, overlay.layer, raiseBlend, 1, true)
				System.LogAlways("M4CARBINE overlay raise " .. overlay.raiseSeconds)
			else
				player:StartAnimation(0, overlay.blank, overlay.layer, fadeOut, 1, true)
				System.LogAlways("M4CARBINE overlay fade blend " .. fadeOut)
			end
			-- The stance clip starts on the aim hold, so it takes over with no blend
			-- once the fade is done, and the torso turn runs from there.
			M4Carbine.At(raising and math.max(overlay.raiseSeconds, raiseBlend) or fadeOut + 0.1, function()
				if overlay.fadeToken == token and overlay.state == "fading" then
					pcall(function()
						local dude = M4Carbine.Dude()
						if raising and overlay.aimHeld and M4Carbine.HoldsCarbine() then
							-- The stance changed during the raise: the stance clip of the hold on
							-- screen takes over and blends to the new stance. Cutting to the other
							-- stance's hold snapped the gun 9 cm (A721 M4QUIRK, aim pressed while
							-- standing up).
							if overlay.aimPlaying ~= M4Carbine.AimClip() then
								overlay.state = "aim"
								M4Carbine.PlayStance(dude)
								return
							end
							M4Carbine.At(overlay.stanceDelay, function()
								if overlay.fadeToken == token and overlay.state == "aim" and overlay.aimHeld then
									if M4Carbine.Mounted() then
										pcall(M4Carbine.PlayMountGrid, M4Carbine.Dude())
									elseif not M4Carbine.OnStance() then
										pcall(M4Carbine.PlayStance, M4Carbine.Dude())
									end
								end
							end)
							overlay.state = "aim"
							return
						end
						if raising then dude:StartAnimation(0, overlay.blank, overlay.layer, 0, 1, true) end
						dude:StopAnimation(0, overlay.layer)
						overlay.state = nil
					end)
					if overlay.state == "fading" then overlay.state = nil end
				end
			end)
		elseif M4Carbine.StanceSwap(player, wanted) then
		-- A holster with the aim still held kept the aim hold over the holster
		-- clip and over whatever came into the hand (scenario harness 2026-10-06).
		elseif not wanted and overlay.state == "aim" and (not overlay.aimHeld or overlay.holstering or not M4Carbine.inHand) then
			overlay.state, overlay.gridDir = "fading", 0
			overlay.fadeToken = overlay.fadeToken + 1
			local token = overlay.fadeToken
			player:StartAnimation(0, overlay.blank, overlay.layer, fadeOut, 1, true)
			System.LogAlways("M4CARBINE overlay aim fade blend " .. fadeOut)
			M4Carbine.At(fadeOut + 0.1, function()
				if overlay.fadeToken == token and overlay.state == "fading" then
					pcall(function() M4Carbine.Dude():StopAnimation(0, overlay.layer) end)
					overlay.state = nil
				end
			end)
		end
end

function M4Carbine.OverlaySync(fadeIn, fadeOut)
	local overlay = M4Carbine.overlay
	local ok, err = pcall(overlaySyncBody, overlay, fadeIn, fadeOut)
	if not ok then System.LogAlways("M4CARBINE overlay error " .. tostring(err)) end
end

-- Lowers the overlay from the aim back to the tuned hold. A plain 0.3 s fade
-- landed the hold on a still twisted torso, gun through the body (A371). The
-- lower clip starts on the aim arms; a release during the raise blends onto it.
function M4Carbine.OverlayLower()
	local overlay = M4Carbine.overlay
	local ok, err = pcall(function()
		local player = M4Carbine.Dude()
		if overlay.disabled or overlay.state == "on" or not M4Carbine.OverlayWanted(player) then return end
		local blend = (M4Carbine.Now() - overlay.raiseAt < overlay.raiseSeconds) and overlay.aimBlend or 0
		overlay.fadeToken = overlay.fadeToken + 1
		local token = overlay.fadeToken
		overlay.state = "on"
		overlay.lowerAt = M4Carbine.Now()
		overlay.idleClip = M4Carbine.IdleClip()
		-- A standing aim on the stance clip (after a crouched raise) lowered on the
		-- unturned lower clip with no blend: an 81 deg turn dropped in one frame,
		-- 25 cm (M4QUIRK 2026-10-06). The grid's last step is the same aim hold
		-- turned, so it lowers there and unturns with the raise.
		local grid = overlay.idleClip ~= overlay.crouchClip
		if overlay.aimPlaying == overlay.standStance and overlay.stanceW < 0.001 and grid then
			M4Carbine.StartGrid(player)
			overlay.aimPlaying, overlay.raiseP = overlay.raiseClip, 1
			overlay.gridDir = -1
			M4Carbine.GridScrub(player)
			System.LogAlways("M4CARBINE overlay lower grid from the stance clip")
			return
		end
		if overlay.aimPlaying == overlay.raiseClip and overlay.stanceW < 0.001 and grid then
			overlay.gridDir = -1
			System.LogAlways(string.format("M4CARBINE overlay lower grid from %.2f", overlay.raiseP))
			return
		end
		overlay.gridDir = 0
		if overlay.idleClip == overlay.crouchClip or overlay.idleClip == overlay.mountClip then
			player:StartAnimation(0, overlay.idleClip, overlay.layer, overlay.lowerSeconds, 1, true)
			System.LogAlways("M4CARBINE overlay lower crouched " .. overlay.lowerSeconds)
			return
		end
		-- The lower clip starts on the unturned standing aim hold; cut in from a
		-- stance clip still blending or turned, it popped the gun 5-9 cm (A722 M4QUIRK).
		if overlay.aimPlaying ~= overlay.aimClip then blend = math.max(blend, overlay.aimBlend) end
		player:StartAnimation(0, overlay.lowerClip, overlay.layer, blend, overlay.clipSeconds / overlay.lowerSeconds, false)
		M4Carbine.At(overlay.lowerSeconds, function()
			if overlay.fadeToken ~= token or overlay.state ~= "on" then return end
			overlay.idleClip = M4Carbine.IdleClip()
			pcall(function() M4Carbine.Dude():StartAnimation(0, overlay.idleClip, overlay.layer, 0, 1, true) end)
		end)
		System.LogAlways("M4CARBINE overlay lower " .. overlay.lowerSeconds .. " blend " .. blend)
	end)
	if not ok then System.LogAlways("M4CARBINE overlay error " .. tostring(err)) end
end

-- The frame loop starts the overlay on time (A369 moved it off the 100 ms tick).
-- A368's draws were five times too fast (the ADB's speed 5 from A16), so the
-- draw fade is timed from the vanilla speed again. The shoulders stay within 5 mm of the neck in
-- every transition (A368), so they cannot tell the base pose apart.
-- Layer 8 (TorsoDetail) only plays sword combat idles, hit flinches and the
-- holster, none of them the M4's. Stopped only once aimed, a hit during a raise
-- turned the gun 72 deg off the view and the stop snapped it back 21 cm in one
-- frame (M4QUIRK 2026-10-06), so it stops whenever the overlay holds the gun.
-- It runs every frame: on the 30 ms pose tick a layer 4 clip started by a
-- fragment reinstall showed for up to two frames. A stop still lets the first
-- frame show, so the crouched aimed run carry is muted in the ADB (A730).
M4Carbine.push = {aimed = {4, 8}, held = {8}}

function M4Carbine.PushGuard()
	local overlay = M4Carbine.overlay
	if overlay.state == nil or overlay.holstering or not M4Carbine.inHand then
		return
	end
	local player = M4Carbine.Dude()
	for _, layer in ipairs(overlay.state == "aim" and M4Carbine.push.aimed or M4Carbine.push.held) do
		local ok, on = pcall(player.IsAnimationRunning, player, 0, layer)
		if ok and on then
			pcall(player.StopAnimation, player, 0, layer)
		end
	end
end

M4Carbine.aimPose = {clip = "rifle_aimposes_aim_upperbody_player", layer = 14, blend = 0.15}

function M4Carbine.AimPoseGuard()
	if not M4Carbine.inHand or M4Carbine.Mounted() then return end
	local player, pose = M4Carbine.Dude(), M4Carbine.aimPose
	local ok, on = pcall(player.IsAnimationRunning, player, 0, pose.layer)
	if ok and not on then
		pcall(player.StartAnimation, player, 0, pose.clip, pose.layer, pose.blend, 1, true)
	end
end

M4Carbine.charge = {landLayer = 1, pending = false, asks = nil, chargedAt = -1, tapAt = -1, tapSeconds = 0.35, tapRelease = false,
	chargeLayer = 10, chargeBy = 0.8, refusedAfter = 0.5, retries = 0, maxRetries = 3, unchargedSince = nil, airAt = nil, landWait = 0.35, landTries = 0}

-- A charged aim runs layer 10 from about 0.47 s after the press. The recharge
-- tap after a jump can still be refused, and the overlay then raised the aim
-- hold on a body that never aimed: the gun hung 25-40 deg off the view until
-- the next jump (logger 2026-09-23). An aim held without layer 10 asks the
-- plugin for another recharge, at most maxRetries times until one takes.
-- Layer 10 came on 0.47 to 0.66 s after the press, so a raise faster than that
-- counted every aim as refused and the taps looped (logger 2026-09-23); an aim
-- counts as refused only chargeBy seconds after the raise started. Each shot
-- drops layer 10 for up to 0.19 s, and at 0.1 s a burst was taken for a
-- refusal and the re-tap lowered the gun mid-burst (logger 2026-09-23), so
-- layer 10 has to stay off refusedAfter seconds. In full auto layer 10 is on
-- for only 30-80 ms of each 110 ms shot, the 30 ms timer missed those windows
-- and a long burst still re-tapped (logger 2026-09-23), so the off time counts
-- only from the last shot.
-- An aim pressed in the air is refused, and ChargeGuard taps it again once the
-- landing clip ends. The body still ran out its landing at 6 m/s for 0.16 s
-- before the aim took it, and the raise rode that body: the gun came up 43 deg
-- off the view and snapped across as the charge started (A733 M4SW, run jump
-- and aim). So that aim raises once the charge layer runs. A taken tap charges
-- 0.19 s after it, but after a sprint jump the game refused the first tap, and
-- a raise after a fixed wait hung the gun 9 deg off the view on a body that
-- never aimed until RefusedGuard tapped again 1.3 s later (A734 M4SW, twice).
-- So a tap that has not charged in landWait is tapped again, maxRetries times.
function M4Carbine.LandWait(player)
	local charge = M4Carbine.charge
	if not charge.airAt or charge.tapAt < charge.airAt then return false end
	if player:IsAnimationRunning(0, charge.chargeLayer) or charge.landTries >= charge.maxRetries then
		charge.airAt, charge.landTries = nil, 0
		return false
	end
	if not charge.pending and M4Carbine.Now() - charge.tapAt > charge.landWait then
		charge.landTries, charge.pending = charge.landTries + 1, true
	end
	return true
end

local function dudeRunning(layer) return M4Carbine.Dude():IsAnimationRunning(0, layer) end

function M4Carbine.RefusedGuard()
	local charge, overlay = M4Carbine.charge, M4Carbine.overlay
	if overlay.state ~= "aim" or not overlay.aimHeld or charge.pending then
		charge.unchargedSince = nil
		return
	end
	local ok, charged = pcall(dudeRunning, charge.chargeLayer)
	if not ok then return end
	local now = M4Carbine.Now()
	if charged then charge.chargedAt = now end
	if charged or now - math.max(overlay.raiseAt, charge.tapAt) < charge.chargeBy or (charge.chargedAt >= overlay.raiseAt and now - M4Carbine.wall.shotAt < charge.refusedAfter) then
		charge.unchargedSince = nil
		if charged then charge.retries = 0 end
		return
	end
	charge.unchargedSince = charge.unchargedSince or now
	if now - charge.unchargedSince >= charge.refusedAfter and charge.retries < charge.maxRetries then
		charge.retries = charge.retries + 1
		charge.unchargedSince = nil
		charge.pending = true
		System.LogAlways("M4CARBINE aim uncharged, recharge try " .. charge.retries)
	end
end

function M4Carbine.ChargeGuard()
	local charge = M4Carbine.charge
	if charge.asks == nil then
		charge.asks = 0
		System.LogAlways("M4CARBINE auto recharge 0")
	end
	M4Carbine.RefusedGuard()
	if not charge.pending then return end
	local ok, landing = pcall(dudeRunning, charge.landLayer)
	if ok and landing then return end
	charge.pending = false
	charge.tapAt = M4Carbine.Now()
	charge.asks = charge.asks % 9 + 1
	System.LogAlways("M4CARBINE auto recharge " .. charge.asks)
end

function M4Carbine.HolsterStarted()
	local overlay = M4Carbine.overlay
	local ask = overlay.holsterAsk
	if not ask then return end
	local player = M4Carbine.Dude()
	local running = player:IsAnimationRunning(0, overlay.itemLayer)
	local time = player:GetAnimationTime(0, overlay.itemLayer) or 0
	-- A quick slot press the game does not sheathe for (a bandage, a potion) must not
	-- leave the ask armed, or the next clip on itemLayer reads as the sheathe.
	if M4Carbine.Now() - ask.at > 1 then
		overlay.holsterAsk = nil
	elseif M4Carbine.inHand and running and (not ask.running or time < ask.time) then
		overlay.holsterAsk = nil
		overlay.holstering = true
		System.LogAlways(string.format("M4CARBINE holster started on layer %d", overlay.itemLayer))
		if ask.hidesCounter then M4Carbine.CounterChoose(false) end
	elseif not M4Carbine.inHand or (ask.running and not running) then
		overlay.holsterAsk = nil
	else
		ask.running, ask.time = running, time
	end
end

function M4Carbine.PoseTick()
	local overlay = M4Carbine.overlay
	pcall(M4Carbine.HolsterStarted)
	M4Carbine.OverlaySync(overlay.drawBlend, (overlay.holstering or not M4Carbine.inHand) and overlay.holsterBlend or overlay.aimBlend)
	M4Carbine.AutoArmTick()
	pcall(M4Carbine.StarterKit)
	M4Carbine.AimPoseGuard()
	pcall(M4Carbine.DrawTap)
	M4Carbine.ChargeGuard()
	-- The tuck starts about 0.3 s after the shot, so the round has to go in the
	-- same frames it lands in, not on the sweep once the burst has landed.
	if M4Carbine.wall.shotAt >= 0 and M4Carbine.Now() - M4Carbine.wall.shotAt < M4Carbine.spent.afterShot then
		M4Carbine.SweepSpent(M4Carbine.spent.nearRadius)
	elseif M4Carbine.wall.shotAt >= 0 and M4Carbine.spent.sweptFor ~= M4Carbine.wall.shotAt then
		M4Carbine.spent.sweptFor = M4Carbine.wall.shotAt
		M4Carbine.SweepSpent()
	end
end

-- The native plugin gives the M4 its own ammo mask per weapon (A433), so the ammo keeps
-- its item for good. A432 parked ammo as copies; a save can still hold them, so any
-- copy is folded back into its ammo when a save loads.
M4Carbine.parkedAmmo = {["a90feb23-dbd1-4144-b139-c97712b059bb"] = "f10ded12-a41c-40bf-a8ae-883d4e845059",
	["e9b7db9e-9705-489c-b4fe-9995c3ed24c7"] = "fb30c64e-2360-4ed7-b805-531b3424fe4d",
	["b1343d2e-0d31-4208-88fa-0f0129d16b37"] = "b302ee9d-d64c-4dc2-b045-05cd7d238eb4",
	["2235bb42-7aa1-4b12-9d7e-4192bc5a2ce1"] = "ef51da7c-92e7-4e56-8c7f-3d22a27fe9a2"}

M4Carbine.starter = {saved = "m4_starter_kit", rounds = 300, wait = 3, at = nil, done = false}

function M4Carbine.StarterKit()
	local starter = M4Carbine.starter
	if starter.done then return end
	starter.at = starter.at or M4Carbine.Now()
	if math.abs(M4Carbine.Now() - starter.at) < starter.wait then return end
	local inventory = M4Carbine.Dude().inventory
	starter.done = true
	local ok, given = pcall(Variables.GetGlobal, starter.saved)
	local first = not (ok and tonumber(given) == 1) or (inventory:GetCountOfClass(M4Carbine.flash.ammo) or 0) == 0
	if (inventory:GetCountOfClass(M4Carbine.itemClass) or 0) == 0 then
		inventory:CreateItem(M4Carbine.itemClass, 1, 1)
		M4Carbine.back.dirty = true
		if first then inventory:CreateItem(M4Carbine.flash.ammo, 1, starter.rounds) end
		local text = first and string.format("M4A1 and %d rounds added to your inventory", starter.rounds) or "M4A1 added to your inventory"
		pcall(UIAction.CallFunction, "hud", -1, "ShowInfoText", text, 1, 6000, false)
		System.LogAlways("M4CARBINE starter kit given" .. (first and "" or ", gun only"))
	end
	Variables.SetGlobal(starter.saved, 1)
end

function M4Carbine.UnparkAmmo()
	pcall(function()
		local inventory = M4Carbine.Dude().inventory
		for copy, ammo in pairs(M4Carbine.parkedAmmo) do
			local count = inventory:GetCountOfClass(copy) or 0
			if count > 0 then
				inventory:DeleteItemOfClass(copy, count)
				inventory:CreateItem(ammo, 1, count)
				System.LogAlways(string.format("M4CARBINE ammo unparked %s x%d", ammo, count))
			end
		end
	end)
end

-- Every shot leaves two pickable entities where the round stopped: the spent
-- m4_556 and its ball_broken debris. Both carry an invisible cylinder model
-- that still collides, so a burst walls off an alley and shoves the weapon
-- aside. Measured in the running game: deleting them clears the obstruction.
M4Carbine.spent = {names = {"m4_556", "ball_broken"}, radius = 60, nearRadius = 15,
	afterShot = 2, sweptFor = -1}

function M4Carbine.SweepSpent(radius)
	pcall(function()
		local here = M4Carbine.Dude():GetWorldPos()
		local removed = 0
		for _, entity in pairs(System.GetEntitiesInSphere(here, radius or M4Carbine.spent.radius) or {}) do
			local name = entity:GetName()
			for _, spent in ipairs(M4Carbine.spent.names) do
				if string.find(name, spent, 1, true) then
					System.RemoveEntity(entity.id)
					removed = removed + 1
					break
				end
			end
		end
		if removed > 0 then
			System.LogAlways(string.format("M4CARBINE swept %d spent rounds", removed))
		end
	end)
end

-- The native m4_autofire.asi plugin repeats presses of a held fire button only
-- while this line says the M4 is drawn; it reads kcd.log (A409).
local function autoArm()
	local armed = M4Carbine.inHand == true and M4Carbine.Dude().human:IsWeaponDrawn() == true
	if armed ~= M4Carbine.autoArmed then
		M4Carbine.autoArmed = armed
		System.LogAlways("M4CARBINE auto armed " .. (armed and "1" or "0"))
	end
end

function M4Carbine.AutoArmTick()
	pcall(autoArm)
end

M4Carbine.back = {angles = {x = math.rad(5.3), y = math.rad(-17.1), z = math.rad(-54.5)}, pos = {x = 0.084, y = 0.044, z = 0.028}, entity = nil, dirty = true}

function M4Carbine.SyncBack()
	if not M4Carbine.back.dirty then return end
	M4Carbine.back.dirty = false
	local entity, owned = nil, nil
	for _, id in pairs(M4Carbine.Dude().inventory:GetInventoryTable()) do
		local item = ItemManager.GetItem(id)
		if item and item.class == M4Carbine.itemClass then
			owned = id
			if item.entity ~= 0 then entity = item.entity end
		end
	end
	M4Carbine.back.entity, M4Carbine.back.item = entity, owned
end

function M4Carbine.BackFrame(player)
	local back = M4Carbine.back
	local item = back.item and ItemManager.GetItem(back.item)
	local entity = item and item.entity ~= 0 and item.entity or nil
	if entity ~= back.entity then
		back.entity = entity
		System.LogAlways("M4CARBINE back entity " .. tostring(entity))
	end
	local gun = back.entity and System.GetEntity(back.entity)
	if not gun then return end
	local at, slot = gun:GetWorldPos(), player:GetBonePos('BackSlot_cannon')
	if math.abs(at.x - slot.x) + math.abs(at.y - slot.y) + math.abs(at.z - slot.z) < 0.01 then
		player:SetAttachmentPos(0, 'weapon_cannon', back.pos, false)
		M4Carbine.When(function() return true end, function() player:SetAttachmentAngles(0, 'weapon_cannon', back.angles, false) end)
		System.LogAlways("M4CARBINE back slot set")
	end
	pcall(M4Carbine.TwinBack, back)
end

-- A conversation draws DialogTwin_Dude, a fresh copy of Henry whose back slot
-- has the game's default turn, so the twin gets the back pose once per copy.
-- The turn goes on a tick after the position, as for Henry.
function M4Carbine.TwinBack(back)
	local twin = System.GetEntityByName('DialogTwin_Dude')
	if not twin then back.twin = nil; return end
	if not twin:IsHidden() then M4Carbine.overlay.talkAt = M4Carbine.Now() end
	if M4Carbine.inHand or back.twin == tostring(twin.id) then return end
	back.twin = tostring(twin.id)
	twin:SetAttachmentPos(0, 'weapon_cannon', back.pos, false)
	M4Carbine.When(function() return true end, function() twin:SetAttachmentAngles(0, 'weapon_cannon', back.angles, false) end)
	System.LogAlways("M4CARBINE twin back slot set")
end

M4Carbine.doll = {clips = {"rifle_walk_jump_rleg_start"}, layers = {8}, on = {}, found = nil, scanAt = nil, scanSeconds = 1}

local function dollItem(dummy) return ItemManager.GetItem(dummy.human:GetItemInHand(0)) end

function M4Carbine.DollDummies()
	local doll, now = M4Carbine.doll, M4Carbine.Now()
	if doll.found then
		for _, dummy in ipairs(doll.found) do
			if System.GetEntity(dummy.id) == nil then doll.found = nil; break end
		end
	end
	if not doll.found and (not doll.scanAt or math.abs(now - doll.scanAt) >= doll.scanSeconds) then
		doll.scanAt = now
		local found = System.GetEntitiesByClass("InventoryDummyPlayer") or {}
		doll.found = #found > 0 and found or nil
	end
	return doll.found or {}
end

function M4Carbine.DollTick()
	local doll = M4Carbine.doll
	local open = false
	for _, dummy in ipairs(M4Carbine.DollDummies()) do
		local hidden = dummy:IsHidden()
		open = open or not hidden
		local key = tostring(dummy.id)
		local want = false
		if not hidden then
			local ok, item = pcall(dollItem, dummy)
			want = ok and item ~= nil and item.class == M4Carbine.itemClass
			if want and item.entity ~= 0 then M4Carbine.TriggerModel(System.GetEntity(item.entity)) end
		end
		if want ~= (doll.on[key] == true) then
			doll.on[key] = want
			for i, layer in ipairs(doll.layers) do
				if want then dummy:StartAnimation(0, doll.clips[i], layer, 0, 1, true) else dummy:StopAnimation(0, layer) end
			end
			System.LogAlways("M4CARBINE doll " .. tostring(dummy:GetName()) .. " M4 pose " .. tostring(want))
		end
	end
	if doll.open and not open then M4Carbine.back.dirty = true end
	doll.open = open
end

M4Carbine.wall = {shotAt = -1}

-- A save load stops the tick's timer and clears layer 9 while the script still
-- believes the overlay is on, so loading from the menu kept the vanilla hold
-- until an aim (A347). A stopped tick therefore means a load: forget the overlay
-- and start it again. abs() covers a clock that restarts with the level.
function M4Carbine.OverlayReloaded(reason)
	local overlay = M4Carbine.overlay
	System.LogAlways("M4CARBINE overlay reset after " .. reason)
	overlay.state, overlay.gridDir = nil, 0
	overlay.fadeToken = overlay.fadeToken + 1
	overlay.loaded = false
	overlay.drawnSince = nil
	overlay.pressAt = nil
	M4Carbine.inHand = nil -- a load drops the non-persistent M4 buff; the next frame adds it back
	M4Carbine.CounterHide()
	M4Carbine.counter.barKnown = false
	M4Carbine.finger.held, M4Carbine.finger.on, M4Carbine.finger.playing, M4Carbine.tap.at = false, false, false, -1
	M4Carbine.trigger.w, M4Carbine.trigger.entity, M4Carbine.trigger.drawn = 0, nil, {}
	M4Carbine.back.dirty = true
	M4Carbine.doll.found, M4Carbine.doll.scanAt = nil, nil
	M4Carbine.props.at = nil
	M4Carbine.UnparkAmmo()
	M4Carbine.starter.done, M4Carbine.starter.at = false, nil
	M4Carbine.headshot.seq = nil
	-- A load drops the pending frame timer while the last tick is still fresh, so
	-- the M4 sat in the vanilla hold until the next key press (A746, user: "before
	-- moving henry the m4 has the handgonne idle pose"). A restart always runs.
	M4Carbine.StartFrames()
end

function M4Carbine.TickStopped()
	return math.abs(M4Carbine.Now() - M4Carbine.overlay.tickAt) > 1
end

function M4Carbine.EnsureOverlay()
	if M4Carbine.TickStopped() then
		M4Carbine.OverlayReloaded("a stopped tick")
	end
end

function M4Carbine.Attach()
	if M4Carbine.attached or type(Player) ~= "table" or type(Player.OnAction) ~= "function" then
		return false
	end
	M4Carbine.CaptureProjectile()
	local original = Player.OnAction
	Player.OnAction = function(self, action, activation, value)
		if self == System.GetEntityByName('Dude') then
			-- After the M4 is in the hand the game releases the block on its own.
			if action == "block" and not M4Carbine.inHand then M4Carbine.overlay.blockHeld = activation == "press" end
			M4Carbine.Sync()
			local overlay = M4Carbine.overlay
			if action == "gun_prepare" and activation == "press" then
				M4Carbine.charge.tapRelease = false
			end
			if action == "movedown" and activation == "press" then overlay.stancePressAt = M4Carbine.Now() end
			if action == M4Carbine.mag.key and activation == "press" and M4Carbine.inHand then M4Carbine.MagReload() end
			if action == "chat_init_with_focus" and activation == "press" then M4Carbine.magModel.chatAt = M4Carbine.Now() end
			M4Carbine.Try("LightPress", M4Carbine.LightPress, action, activation)
			if action == "gun_prepare" or action == "gun_abort" then overlay.rmb = activation == "press" end
			if action == "gun_prepare" and activation == "press" then overlay.aimGuess = false end
			local airborne = false
			pcall(function() airborne = self:IsAnimationRunning(0, M4Carbine.charge.landLayer) end)
			if action == "gun_prepare" and activation == "press" and M4Carbine.inHand and airborne then
				M4Carbine.charge.pending, M4Carbine.charge.airAt = true, M4Carbine.Now()
			elseif action == "gun_prepare" and activation == "press" and M4Carbine.inHand then
				overlay.aimHeld = true
				overlay.holdUntil = M4Carbine.Now() + overlay.aimHold
				M4Carbine.At(overlay.aimHold, function()
					M4Carbine.OverlaySync(overlay.aimBlend, overlay.aimBlend)
				end)
			end
			if (action == "gun_abort" or action == "gun_prepare") and activation == "release" then
				M4Carbine.charge.pending = false
				local charge = M4Carbine.charge
				if overlay.aimHeld and M4Carbine.Now() - charge.tapAt < charge.tapSeconds then
					charge.tapRelease = true
					M4Carbine.At(charge.tapSeconds, function()
						if charge.tapRelease and overlay.aimHeld then
							charge.tapRelease = false
							overlay.aimHeld = false
							M4Carbine.OverlayLower()
						end
					end)
				elseif overlay.aimHeld then
					overlay.aimHeld = false
					M4Carbine.OverlayLower()
				end
			end
			if action == "gun_execute" and activation == "press" then
				-- No hand trace here: at full auto every shot started a new 40-line
				-- chain, some 340 log lines a second, and the frames it cost made
				-- the gaps between shots uneven.
				M4Carbine.wall.shotAt = M4Carbine.Now()
				M4Carbine.finger.pressAt = M4Carbine.Now()
			end
			if action == "gun_execute" and (activation == "press" or activation == "release") then M4Carbine.finger.held = activation == "press" end
			-- Any quick slot press sheathes a drawn M4 (A366 used action_qam_3, which
			-- kept the overlay over the whole sheathe) or draws it. PoseTick fades the
			-- draw in, and fades the sheathe once its clip starts on itemLayer; the game
			-- ignores a press during the draw, and so does the overlay.
			if type(action) == "string" and string.find(action, "^action_qam_%d+$") and activation == "press" then
				overlay.pressAt = M4Carbine.Now()
				local hidesCounter = M4Carbine.CounterPress(action)
				if M4Carbine.inHand then
					overlay.holsterAsk = {at = M4Carbine.Now(), running = self:IsAnimationRunning(0, overlay.itemLayer), time = self:GetAnimationTime(0, overlay.itemLayer) or 0, hidesCounter = hidesCounter}
				end
			end
			if (activation == "press" or activation == "release") and type(action) == "string" and not string.find(action, "rotate", 1, true) and (M4Carbine.trace or string.find(action, "chat", 1, true) or string.find(action, "talk", 1, true)) then
				System.LogAlways(string.format("M4CARBINE action %s %s t=%.3f", action, activation, M4Carbine.Now()))
			end
			M4Carbine.EnsureOverlay()
		end
		return original(self, action, activation, value)
	end
	-- OnLoad and OnPostLoad are probes: the player script has none, and the engine
	-- may call them after a save is restored, before any input.
	for _, name in ipairs({"OnLoadAI", "OnReset", "OnInit", "OnLoad", "OnPostLoad"}) do
		local hooked = Player[name]
		Player[name] = function(self, ...)
			local results = type(hooked) == "function" and {hooked(self, ...)} or {}
			M4Carbine.OverlayReloaded("player " .. name)
			return unpack(results)
		end
	end
	M4Carbine.attached = true
	Script.ReloadScript('Scripts/m4_view.lua')
	Script.ReloadScript('Scripts/m4_view_profile.lua')
	KCD2ForgeViewProbe.StartSight(M4CarbineSightProfile)
	System.LogAlways("M4CARBINE attached to Player.OnAction")
	return true
end

function Startup:M4Carbine()
	M4Carbine.Attach()
	M4Carbine.EnsureOverlay()
end

System.LogAlways("M4CARBINE script loaded, attach " .. tostring(M4Carbine.Attach()))
-- The tick used to start on the first key press, so a loaded save showed the
-- vanilla hold until then (A343).
M4Carbine.EnsureOverlay()
