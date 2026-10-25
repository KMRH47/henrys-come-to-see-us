KCD2ForgeViewProbe = KCD2ForgeViewProbe or {}

local function sample(phase)
    local player = System.GetEntityByName('Dude')
    local function vector(name, value)
        System.LogAlways(string.format('M4VIEWPROBE %s %s %.9f %.9f %.9f', phase, name, value.x, value.y, value.z))
    end
    vector('view_position', System.GetViewCameraPos())
    vector('view_direction', System.GetViewCameraDir())
    vector('view_up', System.GetViewCameraUpDir())
    for _, name in ipairs({'camera', 'Head', 'RightHand', 'LeftHand', 'RightHandSlot'}) do
        vector(name .. '_position', player:GetBonePos(name))
        vector(name .. '_direction', player:GetBoneDir(name))
    end
end

function KCD2ForgeViewProbe.Stop()
    local state = KCD2ForgeViewProbe.state
    if not state then return end
    KCD2ForgeViewProbe.state = nil
    System.LogAlways('M4CARBINE auto aim 0')
    if state.tick then Script.KillTimer(state.tick) end
    if state.watchdog then Script.KillTimer(state.watchdog) end
    if state.restore_projectile then state.restore_projectile() end
    if state.action_wrapper and Player.OnAction == state.action_wrapper then
        Player.OnAction = state.original_action
    end
    if not state.persistent then
        local ok, err = pcall(CryAction.ResetToNormalCamera)
        System.LogAlways('M4VIEWPROBE restore ' .. (ok and 'called' or tostring(err)))
    end
    Script.SetTimer(150, function()
        local measured, reason = pcall(sample, 'restored')
        if not measured then System.LogAlways('M4VIEWPROBE error ' .. tostring(reason)) end
    end)
end

local function finite_number(value)
    return type(value)=='number' and value==value and value~=math.huge and value~=-math.huge
end

function KCD2ForgeViewProbe.StartSight(config)
    if KCD2ForgeViewProbe.state then error('view probe already active') end
    if type(Player) ~= 'table' or type(Player.OnAction) ~= 'function' then error('missing player action hook') end
    if config.native_alignment and not config.persistent then error('native alignment requires persistent timing') end
    local state = {aiming=false, phase='idle', shot=0, persistent=config.persistent}
    local interval=config.persistent and 0 or 16
    local function log(message) System.LogAlways('M4SIGHT ' .. config.token .. ' ' .. message) end
    local function report_aim(value)
        if state.aim_report==value then return end
        state.aim_report=value
        System.LogAlways('M4CARBINE auto aim '..value)
    end
    if config.aim_by_projectile~=nil then
        if config.aim_by_projectile~=0 and config.aim_by_projectile~=1 then error('invalid projectile aiming mode') end
        state.restore_projectile=function()
            if state.original_projectile~=nil then
                System.ExecuteCommand('wh_w_AimByProjectile '..state.original_projectile)
                log('projectile restored '..state.original_projectile)
                state.original_projectile=nil
            end
        end
    end
    local function dot(a,b) return a.x*b.x+a.y*b.y+a.z*b.z end
    local function sub(a,b) return {x=a.x-b.x,y=a.y-b.y,z=a.z-b.z} end
    local function length(v) return math.sqrt(dot(v,v)) end
    local function vector(v) return string.format('%.7f %.7f %.7f',v.x,v.y,v.z) end
    local function angles(v)
        return {x=math.atan2(v.z,math.sqrt(v.x*v.x+v.y*v.y)),y=0,z=-math.atan2(v.x,v.y)}
    end
    local function wrap(value) return (value+math.pi)%(2*math.pi)-math.pi end
    local function native_angles(player)
        local actor=player.actor
        if type(actor.GetAngles)=='function' then
            local ok,a=pcall(actor.GetAngles,actor)
            if ok and type(a)=='table' and finite_number(a.x) and finite_number(a.z) then return {x=a.x,y=0,z=a.z} end
        end
        return angles(actor:GetHeadDir())
    end
    local function frame(player)
        local origin=player:GetBonePos('RightHandSlot')
        local x=player:GetBoneLocal('RightHandSlot',{x=1,y=0,z=0})
        local y=player:GetBoneLocal('RightHandSlot',{x=0,y=1,z=0})
        local z=player:GetBoneLocal('RightHandSlot',{x=0,y=0,z=1})
        local function point(v) return {x=origin.x+dot(x,v),y=origin.y+dot(y,v),z=origin.z+dot(z,v)} end
        local rear=point(config.rear)
        local forward=sub(point(config.front),rear)
        local size=length(forward)
        if size<0.1 or size>1 then error('invalid sight span') end
        forward={x=forward.x/size,y=forward.y/size,z=forward.z/size}
        return {position={x=rear.x-forward.x*config.eye_relief,y=rear.y-forward.y*config.eye_relief,z=rear.z-forward.z*config.eye_relief},direction=forward,rear=rear}
    end
    local function projectile_sample(sight)
        if not state.projectile_ticks or state.projectile_ticks>=60 then return end
        state.projectile_ticks=state.projectile_ticks+1
        local count=0
        for _,entity in pairs(System.GetEntitiesByClass('Arrow') or {}) do
            if not state.projectile_baseline[tostring(entity.id)] then
                count=count+1
                local position=entity:GetWorldPos()
                local ok,velocity=pcall(function() return entity:GetVelocity() end)
                local measured=ok and type(velocity)=='table' and velocity.x~=nil
                log('projectile '..state.shot..' '..state.projectile_ticks..' '..tostring(entity.id)..' '..vector(position)..' velocity '..(measured and vector(velocity) or 'unavailable')..' sight '..vector(sight.direction))
            end
        end
        local weapon=state.projectile_weapon
        if weapon then
            local x=weapon:GetBoneLocal('hand_cannon_arrow',{x=1,y=0,z=0})
            local y=weapon:GetBoneLocal('hand_cannon_arrow',{x=0,y=1,z=0})
            local z=weapon:GetBoneLocal('hand_cannon_arrow',{x=0,y=0,z=1})
            log('launch_frame '..state.shot..' '..state.projectile_ticks..' '..vector(weapon:GetBonePos('hand_cannon_arrow'))..' '..vector(x)..' '..vector(y)..' '..vector(z)..' '..vector(sight.direction)..' arrows '..count)
        end
        if state.projectile_ticks==60 then log('projectile_capture_end '..state.shot) end
    end
    local function normal(reason)
        if state.tick then Script.KillTimer(state.tick);state.tick=nil end
        if state.projectile_ticks and state.projectile_ticks<60 then log('projectile_capture_end '..state.shot..' '..reason) end
        state.projectile_ticks=nil
        if state.restore_projectile then state.restore_projectile() end
        if config.persistent and state.first_update then
            log(string.format('cadence updates %d seconds %.3f max_interval_ms %.3f',state.update_count,System.GetCurrTime()-state.first_update,state.max_update_interval*1000))
        end
        state.phase='idle';state.aiming=false;state.player=nil;state.ready_since=nil
        state.raise_anchor=nil;state.last_sight=nil
        log('normal '..reason)
        report_aim(0)
    end
    local tick
    tick=function()
        state.tick=nil
        if KCD2ForgeViewProbe.state~=state or not state.aiming then return end
        local ok,err=pcall(function()
            local player=System.GetEntityByName('Dude')
            if not player then error('player disappeared') end
            if config.persistent and (player~=state.player or not M4Carbine.HoldsCarbine()) then
                normal('player_or_weapon_changed');return
            end
            local sight=frame(player)
            local now=config.persistent and System.GetCurrTime() or 0
            if config.persistent then
                local eye_distance=length(sub(player:GetBonePos('eye_right_bone'),sight.rear))
                if state.phase=='raising' then
                    local view_direction=System.GetViewCameraDir()
                    local ready=eye_distance<0.18 and dot(sight.direction,view_direction)>0.906308
                    if config.native_alignment then
                        local native=native_angles(player)
                        local aim=angles(sight.direction)
                        local delta={x=wrap(aim.x-native.x),z=wrap(aim.z-native.z),distance=eye_distance}
                        local last=state.last_sight
                        ready=ready and last~=nil and math.abs(wrap(delta.x-last.x))<0.01 and math.abs(wrap(delta.z-last.z))<0.01
                        state.last_sight=delta
                        local anchor=state.raise_anchor
                        if not ready or not anchor then
                            state.raise_anchor=delta;state.ready_since=nil
                        end
                    end
                    -- A draw aim showed the sights before the game took the aim, and a
                    -- click there did nothing (A703, user: "I cannot shoot when the aim
                    -- is ready"); a click stays latched until the charge.
                    if ready and M4Carbine and M4Carbine.PressedDraw() and not player:IsAnimationRunning(0,M4Carbine.charge.chargeLayer) then ready=false end
                    if state.tapped and M4Carbine and not M4Carbine.overlay.aimHeld then normal('aim_released');return end
                    if not ready then state.ready_since=nil
                    elseif not state.ready_since then state.ready_since=now end
                    if not state.ready_since then
                        if now-state.prepare_time>10 then normal('raise_timeout') end
                        return
                    end
                    if state.original_projectile==nil then
                        state.original_projectile=tonumber(System.GetCVar('wh_w_AimByProjectile'))
                        if state.original_projectile==nil then error('missing projectile aiming cvar') end
                        System.ExecuteCommand('wh_w_AimByProjectile '..config.aim_by_projectile)
                    end
                    state.phase='aim';state.tapped=nil
                    if config.native_alignment then
                        state.hold_released=true
                    end
                    log(string.format('sights ready %.3f raise_seconds %.3f',eye_distance,now-state.prepare_time))
                    if state.phase=='aim' then report_aim(2) end
                elseif eye_distance>0.26 then
                    normal('weapon_lowered');return
                end
            end
            if config.capture_projectiles then projectile_sample(sight) end
            if config.persistent then
                state.first_update=state.first_update or now
                state.update_count=state.update_count+1
                if state.last_update then state.max_update_interval=math.max(state.max_update_interval,now-state.last_update) end
                state.last_update=now
            end
        end)
        if not ok then log('error '..tostring(err));KCD2ForgeViewProbe.Stop();return end
        if state.aiming then state.tick=Script.SetTimer(interval,tick) end
    end
    local function start_aim(self)
        if state.aiming then return end
        if type(System.GetCVar)=='function' and (tonumber(System.GetCVar('wh_pl_CameraFollowEntity')) or 0)~=0 then
            log('third person native')
            return
        end
        if config.aim_by_projectile~=nil then
            if not M4Carbine or not M4Carbine.HoldsCarbine() then return end
            M4Carbine.Sync()
            if not config.persistent or config.native_alignment then
                local original=tonumber(System.GetCVar('wh_w_AimByProjectile'))
                if original==nil then error('missing projectile aiming cvar') end
                state.original_projectile=original
                System.ExecuteCommand('wh_w_AimByProjectile '..config.aim_by_projectile)
                log('projectile trial '..config.aim_by_projectile)
            end
        end
        state.player=self;state.aiming=true;state.hold_released=nil;state.tapped=nil;state.phase=config.persistent and 'raising' or 'aim'
        state.first_update=nil;state.last_update=nil;state.update_count=0;state.max_update_interval=0
        if config.persistent then state.prepare_time=System.GetCurrTime() end
        state.tick=Script.SetTimer(interval,tick)
        log('aim started')
        report_aim(state.phase=='aim' and 2 or 1)
    end
    state.start_aim=start_aim
    state.original_action=Player.OnAction
    state.action_wrapper=function(self,action,activation,value)
        if self~=System.GetEntityByName('Dude') then return state.original_action(self,action,activation,value) end
        local ok,err=pcall(function()
            if action=='gun_abort' and activation=='release' then
                local charge=M4Carbine and M4Carbine.charge
                if state.aiming and charge and M4Carbine.Now()-charge.tapAt<charge.tapSeconds then
                    -- The plugin's recharge tap lets the aim go for 40 ms; ending the
                    -- aim there dropped the click waiting for the charge.
                    state.phase='raising';state.tapped=true;state.ready_since=nil;state.raise_anchor=nil;state.last_sight=nil
                    state.prepare_time=System.GetCurrTime()
                    log('aim tapped')
                    report_aim(1)
                elseif state.aiming then
                    normal('aim_released')
                end
            elseif action=='gun_prepare' and activation=='press' and not state.aiming then
                start_aim(self)
            elseif action=='gun_execute' and activation=='press' and state.aiming then
                if state.phase~='aim' then
                    log('fire ignored during '..state.phase)
                    return
                end
                state.shot=state.shot+1
                if config.aim_by_projectile~=nil then System.ExecuteCommand('wh_w_AimByProjectile '..(state.hold_released and 0 or config.aim_by_projectile)) end
                if config.capture_projectiles then
                    state.projectile_ticks=0;state.projectile_baseline={};state.projectile_weapon=nil
                    for _,entity in pairs(System.GetEntitiesByClass('Arrow') or {}) do state.projectile_baseline[tostring(entity.id)]=true end
                    local item=ItemManager.GetItem(self.human:GetItemInHand(0))
                    if item then state.projectile_weapon=System.GetEntity(item.entity) end
                    log('projectile_capture_begin '..state.shot)
                end
                log('fire '..state.shot)
            end
        end)
        if not ok then log('error '..tostring(err));KCD2ForgeViewProbe.Stop() end
        return state.original_action(self,action,activation,value)
    end
    KCD2ForgeViewProbe.state=state
    Player.OnAction=state.action_wrapper
    if not config.persistent then state.watchdog=Script.SetTimer(config.duration_ms,KCD2ForgeViewProbe.Stop) end
    log('armed idle')
end

function KCD2ForgeViewProbe.Rearm()
    local state=KCD2ForgeViewProbe.state
    if not state or state.aiming or type(state.start_aim)~='function' then return false end
    local player=System.GetEntityByName('Dude')
    if not player then return false end
    local ok,err=pcall(state.start_aim,player)
    if not ok then
        System.LogAlways('M4VIEWPROBE error '..tostring(err))
        return false
    end
    return state.aiming==true
end

function KCD2ForgeViewProbe.Start(hold_ms)
    if KCD2ForgeViewProbe.state then
        System.LogAlways('M4VIEWPROBE error already active')
        return
    end
    if not CryAction or type(CryAction.SetViewCamera) ~= 'function' or type(CryAction.ResetToNormalCamera) ~= 'function' then
        System.LogAlways('M4VIEWPROBE error missing camera API')
        return
    end
    local player = System.GetEntityByName('Dude')
    if not player then
        System.LogAlways('M4VIEWPROBE error missing player')
        return
    end
    local measured, reason = pcall(sample, 'before')
    if not measured then
        System.LogAlways('M4VIEWPROBE error ' .. tostring(reason))
        return
    end
    local state = {}
    KCD2ForgeViewProbe.state = state
    state.watchdog = Script.SetTimer(hold_ms or 1500, KCD2ForgeViewProbe.Stop)
    local function tick()
        if KCD2ForgeViewProbe.state ~= state then return end
        local ok, err = pcall(function()
            local current = System.GetEntityByName('Dude')
            if not current then error('player disappeared') end
            CryAction.SetViewCamera(current:GetBonePos('camera'), current:GetBoneDir('camera'))
        end)
        if not ok then
            System.LogAlways('M4VIEWPROBE error ' .. tostring(err))
            KCD2ForgeViewProbe.Stop()
            return
        end
        state.tick = Script.SetTimer(16, tick)
    end
    tick()
    Script.SetTimer(750, function()
        if KCD2ForgeViewProbe.state ~= state then return end
        local ok, err = pcall(sample, 'override')
        if not ok then
            System.LogAlways('M4VIEWPROBE error ' .. tostring(err))
            KCD2ForgeViewProbe.Stop()
        end
    end)
end
