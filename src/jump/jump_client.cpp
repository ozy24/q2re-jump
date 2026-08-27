// [Jump] Per-client behaviour: spawning, the run timer, damage rules.

#include "../g_local.h"
#include "jump_local.h"
#include "jump_slick.h"

// Jump players hold nothing at all, as in Refresh. Classic q2jump hands out a
// blaster but gates the bolt behind an mset that defaults off, so in both mods
// a spawned player cannot shoot; leaving a working blaster here would be
// behaviour neither has. Weapons come only from the msets or trigger_weapon.
//
// ChangeWeapon and Think_Weapon both handle a null pers.weapon, so no upstream
// hook is needed - ChangeWeapon is what zeroes ps.gunindex and hides the gun.
void Jump_StripInventory(edict_t *ent)
{
	gclient_t *client = ent->client;

	client->pers.inventory.fill(0);

	client->pers.weapon = nullptr;
	client->pers.lastweapon = nullptr;
	client->newweapon = nullptr;
	client->pers.selected_item = IT_NULL;

	client->quad_time = 0_ms;
	client->invincible_time = 0_ms;
	client->enviro_time = 0_ms;
	client->breather_time = 0_ms;
	client->silencer_shots = 0;
}

// The clean-slate half of a spawn: no run, no checkpoints, nothing carried, and
// the Practice hook handed back.
//
// Extracted because the start line puts a player into exactly this state without
// moving them, and the two must not drift - in particular the grapple re-grant,
// which the strip above would otherwise take away for good the first time
// somebody crossed the line.
//
// The grapple is a Practice tool: it makes a jump you cannot do yet reachable,
// so you can work on the part after it. On Ranked it would make times
// incomparable, which is the one thing Ranked is for, so it is not offered there
// at any setting - `use Grapple` finds nothing in the inventory.
//
// Granting it here rather than leaving it to the stock give at p_client.cpp:888
// is also what makes that absolute: the stock give runs earlier in
// PutClientInServer than the strip, so `g_allow_grapple 1` cannot arm a Ranked
// player by accident.
static void Jump_ClearRunState(edict_t *ent, jump_client_t &jc)
{
	Jump_ResetRun(jc);
	Jump_StripInventory(ent);
	Jump_ClearCheckpointFlags(ent);

	// A fresh life means a fresh body at the spawn point, which an active
	// replay would immediately fight - PutClientInServer is about to place
	// this player for real, so any playback in progress has to give up
	// control now rather than keep dragging them along the ghost path from
	// underneath the respawn. Race stays armed: unlike playback it is not
	// exclusive with playing normally, so a death or a `kill` should not
	// cost you the ghost you were racing.
	Jump_CancelReplay(ent);

	if (jc.team == jump_team_t::practice)
		ent->client->pers.inventory[IT_WEAPON_GRAPPLE] = 1;
}

// Runs at the top of PutClientInServer, above the spectator branch - which is
// the only place it can run, because that branch returns before the
// Jump_ClientSpawn hook further down, so a spectator never reaches it.
//
// Nobody enters the map without answering the join menu first, so an unchosen
// client is pinned to spectator here and the prompt is armed. The open itself
// happens from ClientThink rather than now: the client is not sending usercmds
// yet, so an svc_layout written here has nowhere useful to land.
void Jump_PreSpawn(edict_t *ent)
{
	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc || jc->team_chosen)
		return;

	jc->team = jump_team_t::spectator;

	// Both flags, not just pers: ClientEndServerFrame calls spectator_respawn
	// whenever the two disagree for five seconds, which would quietly undo the
	// gate and drop them into the map.
	ent->client->pers.spectator = true;
	ent->client->resp.spectator = true;

	if (!jc->menu_prompted)
		jc->menu_prompt_time = level.time + 100_ms;
}

void Jump_ClientSpawn(edict_t *ent)
{
	if (!Jump_Active())
		return;

	// Every jump client is pinned to spectator by Jump_PreSpawn until they answer
	// the join menu, and that branch of PutClientInServer sets SVF_NOCLIENT. The
	// vanilla ways back into the map clear it on the way past - respawn() does it
	// before PutClientInServer, spectator_respawn() after - but jump calls
	// PutClientInServer directly from Jump_JoinTeam, and its non-spectator path
	// only clears the flag when it is unwinding the awaiting_respawn limbo, which
	// force_spawn means we never enter. So the flag survived the join and every
	// player was invisible to everyone else.
	//
	// This hook is the right place for it: it runs at the end of the non-spectator
	// path only, so reaching it means this client is entering the map for real.
	// It also has to happen before the G_PostRespawn calls that follow, which
	// early-return on SVF_NOCLIENT and would otherwise eat the teleport effect.
	ent->svflags &= ~SVF_NOCLIENT;

	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc)
		return;

	Jump_ClearRunState(ent, *jc);

	// Pick the player's stored best for this map back up. This has to happen
	// here rather than at level init, because when Jump_InitLevel runs every
	// client is still flagged disconnected.
	const int64_t previous_pb = jc->pb_time_ms;
	jc->pb_time_ms = Jump_PersonalBest(ent);

	// Jump_ClientSpawn runs on every respawn, not just the first one, but the
	// PB configstring only needs writing when the value actually changed -
	// a configstring update broadcasts to every connected client, not just
	// this one.
	if (jc->pb_time_ms != previous_pb)
		Jump_UpdatePbString(ent);

	// Health is deliberately left at the default. Inflating it does nothing
	// useful once combat damage is zeroed, and it makes hazards take seconds
	// to kill instead of ending the run.

	// Every route into Ranked lands here, because they all go through
	// Jump_JoinTeam - the join menu, the `team` command, and the menu the join
	// gate re-opens after a map change. It runs after that function's
	// Jump_FreeRaceTrail rather than before it, so the ghost arms against the
	// team the player just landed on rather than the one they left. Safe to
	// call on every respawn as well: it is a no-op once armed.
	Jump_AutoArmRace(ent, *jc);

	Jump_RefreshPlayerInstancing();
}

// ---------------------------------------------------------------------------
// start_line / weapon_clear
//
// Both are resizable brush triggers in classic q2jump, so both fire on every
// frame you are stood inside the volume rather than once on entry. That is not
// a defect to guard away wholesale: for the start line it is precisely what
// makes the run begin as you LEAVE the line rather than as you enter it, since
// the clock is re-zeroed continuously while you are on it.
//
// What does have to be guarded is the expensive half. Jump_ClearCheckpointFlags
// sweeps every edict in the level, and on a large map at 40 Hz that is real
// work for no gain once the first touch has already cleared everything.
// ---------------------------------------------------------------------------

// True on the first touch of a contiguous run of them, so the heavy reset lands
// once per entry into the volume.
static bool Jump_LineTouchIsEntry(jump_client_t &jc)
{
	const bool entry = level.time > jc.line_touch_time + FRAME_TIME_S;

	jc.line_touch_time = level.time;

	return entry;
}

// The strip nulls pers.weapon but leaves ps.gunindex still drawing the old gun;
// ChangeWeapon is what actually puts it away (see the note on Jump_StripInventory).
// Only worth calling when there was something in hand - it does a modelindex
// lookup and a sound check every time.
static void Jump_WipeHeldWeapon(edict_t *ent, bool had_weapon)
{
	if (had_weapon)
		ChangeWeapon(ent);
}

void Jump_StartLine(edict_t *ent)
{
	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc || ent->client->resp.spectator || ent->client->chase_target ||
		jc->team == jump_team_t::spectator)
		return;

	// Read before Jump_ClearRunState below puts the run back to idle, because
	// it is what decides whether this is a new attempt or the same one being
	// re-armed. The entry test alone will not do: this function runs EVERY
	// frame the player stands in the volume, and only the expensive half is
	// gated behind it, so counting there would add an attempt per server frame.
	const bool was_running = jc->state == jump_run_state_t::running;

	if (Jump_LineTouchIsEntry(*jc))
	{
		const bool had_weapon = ent->client->pers.weapon != nullptr;

		Jump_ClearRunState(ent, *jc);
		Jump_WipeHeldWeapon(ent, had_weapon);
	}

	// The line restarts the clock rather than re-arming it: upstream sets
	// client_think_begin to now and leaves the run running (g_items.c:605), so
	// crossing the line does not put you back to waiting for a movement input.
	//
	// Stores are deliberately left alone. Upstream's ClearPersistants wipes them
	// here, but recall stores are a Practice tool rather than part of the timed
	// contract, and losing them on every lap of a map built around a start line
	// would make Practice worse for no gain in comparability.
	jc->state = jump_run_state_t::running;
	jc->run_start_ms = Jump_NowMs();

	// Only a genuine idle -> running transition. Walking into the line mid-run
	// re-zeroes the clock without being a new try, which does mean laps after
	// the first within one life go uncounted on a lap map.
	if (!was_running)
		Jump_CountAttempt(ent);
}

void Jump_ClearWeapons(edict_t *ent)
{
	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc || ent->client->resp.spectator || ent->client->chase_target ||
		jc->team == jump_team_t::spectator)
		return;

	if (!Jump_LineTouchIsEntry(*jc))
		return;

	const bool had_weapon = ent->client->pers.weapon != nullptr;

	Jump_StripInventory(ent);
	Jump_WipeHeldWeapon(ent, had_weapon);

	// Not a map weapon, so the wipe should not cost you it. Same reasoning as
	// Jump_ClearRunState, which is not used here because weapon_clear leaves the
	// run and the checkpoints alone - it only empties your hands.
	if (jc->team == jump_team_t::practice)
		ent->client->pers.inventory[IT_WEAPON_GRAPPLE] = 1;
}

// The strafe meter, fed one usercmd at a time.
//
// This runs BEFORE Pmove (p_client.cpp), which is what makes it exact rather
// than an estimate: ent->velocity is still the velocity the acceleration will
// see, and every flag below describes the state the movement branch is about to
// be chosen from. The client half has to work harder for the same numbers - it
// only gets one sample per rendered frame, and has to reconstruct the pre-move
// state from a wrapper around Pmove.
//
// Air, plus ice. On ordinary ground friction has already run by the time
// acceleration happens, so a reading there would be quietly wrong - but on a
// slick surface PM_Friction's drop is exactly zero (p_move.cpp:564) and the
// reconstruction is as exact as it is in the air. See jump_logic.h.
// What the last hop earned. Fed from the same place as the strafe meter, so it
// sees the exact command the feet leave the ground on rather than whichever one
// happened to fall on a rendered frame.
//
// This runs BEFORE pmove, so ent->groundentity is the state the previous command
// left behind - which is what the takeoff test wants: it is looking for the
// first command where the player is no longer stood on anything.
static void Jump_TrackTakeoff(edict_t *ent, usercmd_t *ucmd, jump_client_t &jc)
{
	const bool playable = !ent->client->resp.spectator && !ent->client->chase_target &&
						  jc.team != jump_team_t::spectator;

	if (!playable || ucmd->msec == 0 || ent->client->ps.pmove.pm_type != PM_NORMAL)
	{
		jc.takeoff.Reset();
		return;
	}

	const float speed = jump::HorizontalSpeed(ent->velocity[0], ent->velocity[1]);

	jc.takeoff.Update(ent->groundentity != nullptr, speed, ucmd->msec);
}

// Is the player stood on ground pmove will not slow them down on?
//
// pmove finds this with a trace of its own and keeps the surface in pml_t, which
// never reaches the game, so the only way to know is to repeat the trace. Shaped
// like PM_CatagorizePosition (p_move.cpp:977-991): the player hull, a quarter
// unit down.
static bool Jump_OnFrictionlessGround(edict_t *ent)
{
	if (!ent->groundentity)
		return false;

	// Mirrors PM_Trace's own mask (p_move.cpp:227-243): pmove strips
	// CONTENTS_PLAYER whenever PMF_IGNORE_PLAYER_COLLISION is set, which
	// g_disable_player_collision (forced on in Jump_Init) makes true for every
	// player here outside coop. Skipping this would let the probe hit an
	// overlapping player instead of the ground - a solid-ground read exactly
	// where pmove itself saw none, and a possible disagreement with the cgame
	// half's trace in jump_cg_move.cpp, which already excludes it.
	contents_t mask = MASK_PLAYERSOLID;

	if (ent->client->ps.pmove.pm_flags & PMF_IGNORE_PLAYER_COLLISION)
		mask &= ~CONTENTS_PLAYER;

	const vec3_t point = { ent->s.origin[0], ent->s.origin[1], ent->s.origin[2] - 0.25f };
	const trace_t tr = gi.trace(ent->s.origin, ent->mins, ent->maxs, point, ent, mask);

	return Jump_TraceHitFrictionlessGround(tr);
}

static void Jump_TrackStrafe(edict_t *ent, usercmd_t *ucmd, jump_client_t &jc)
{
	const int64_t now = Jump_NowMs();
	const int64_t dt_ms = jc.strafe_last_ms ? now - jc.strafe_last_ms : 0;

	jc.strafe_last_ms = now;

	const pmove_state_t &pm = ent->client->ps.pmove;

	const bool playable = !ent->client->resp.spectator && !ent->client->chase_target &&
						  jc.team != jump_team_t::spectator;

	// A fresh jump press clears groundentity partway through the command, so the
	// accel runs in the air branch from a frame that started on the floor and
	// neither model describes it. This used to need no test of its own - the
	// ground check excluded every such frame for free - but now that ice frames
	// are admitted, the takeoff stroke off an ice brush would slip through.
	// Held jump does not re-trigger (p_move.cpp:1078), so holding stays gradeable.
	const bool jumping = (ucmd->buttons & BUTTON_JUMP) && !(pm.pm_flags & PMF_JUMP_HELD);

	// Only trace when the answer can matter - once per command per grounded
	// player, and never for a spectator or a frame that is excluded anyway.
	const bool gradeable_ground = playable && !jumping && ent->groundentity && Jump_OnFrictionlessGround(ent);

	// Every remaining exclusion is a case where the reconstruction would not be
	// exact.
	const bool usable = playable && ucmd->msec > 0 && pm.pm_type == PM_NORMAL && !jumping &&
						(!ent->groundentity || gradeable_ground) && !ent->waterlevel &&
						!(pm.pm_flags & PMF_ON_LADDER) && pm.pm_time == 0;

	jump::strafe_frame_t frame;

	if (usable)
	{
		const float vel[2] = { ent->velocity[0], ent->velocity[1] };

		// The view angles pmove is about to build its move axes from -
		// PM_ClampAngles adds the delta angles to the command's, and MoveAxes
		// applies the pitch/3 rule from there.
		const vec3_t angles = ucmd->angles + pm.delta_angles;

		frame = jump::StrafeFrame(vel, angles[PITCH], angles[YAW], angles[ROLL], ucmd->forwardmove,
								  ucmd->sidemove, ucmd->msec / 1000.f,
								  (pm.pm_flags & PMF_DUCKED) != 0, pm_config.airaccel,
								  gradeable_ground);
	}

	jc.strafe_readout = jc.strafe.Add(frame, usable, (uint64_t) (dt_ms > 0 ? dt_ms : 0));
}

void Jump_ClientThink(edict_t *ent, usercmd_t *ucmd)
{
	if (!Jump_Active())
		return;

	Jump_TrackInput(ent, ucmd);

	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc)
		return;

	Jump_TrackStrafe(ent, ucmd, *jc);
	Jump_TrackTakeoff(ent, ucmd, *jc);
	Jump_TrackReplay(ent, ucmd, *jc);

	// The join menu, armed by Jump_PreSpawn. Opening from here rather than the
	// spawn path is MuffMode's trick: ClientThink only runs once the client is
	// really in the world and talking to us, so the short delay is enough and
	// the layout cannot be sent too early to be drawn.
	if (!jc->menu_prompted && jc->menu_prompt_time && level.time > jc->menu_prompt_time &&
		!jc->team_chosen && !ent->client->menu && !level.intermissiontime)
	{
		Jump_OpenMainMenu(ent);
		jc->menu_prompt_time = 0_ms;
		jc->menu_prompted = true;
	}

	if (jc->state != jump_run_state_t::idle)
		return;

	if (ent->client->resp.spectator || ent->client->chase_target)
		return;

	// A replaying player's own input must never start a real run - the
	// G_TouchTriggers guard in p_client.cpp stops a ghost path from
	// finishing one, but this stops it from ever starting in the first
	// place, which also keeps Jump_TrackReplay (gated on state == running)
	// from recording over the buffer that is about to be watched back.
	if (jc->replay_mode != jump_replay_mode_t::none)
		return;

	// The run starts on the first movement input, matching Q2JumpRefresh.
	// Deliberately not started by +attack (the classic mod does) so that
	// firing the blaster on the spawn pad doesn't commit you to a run.
	// The rerelease has no upmove axis: jump and crouch are buttons.
	if (ucmd->forwardmove || ucmd->sidemove || (ucmd->buttons & (BUTTON_JUMP | BUTTON_CROUCH)))
	{
		jc->state = jump_run_state_t::running;
		jc->run_start_ms = Jump_NowMs();
		Jump_CountAttempt(ent);
		Jump_Log("%s started a run", Jump_DisplayName(ent));
	}
}

void Jump_ClientDisconnect(edict_t *ent)
{
	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc)
		return;

	Jump_FreeClientFollowers(ent);
	Jump_FreeStoreMarker(*jc);
	Jump_FreeRaceTrail(*jc);
	Jump_ResetRun(*jc);
	jc->stores.Clear();
	jc->last_time_ms = 0;

	Jump_VoteClientDisconnect(ent);
	*jc = jump_client_t {};
	Jump_RecountHideJumpers();
	Jump_RefreshPlayerInstancing();
}

bool Jump_FilterDamage(edict_t *targ, edict_t *attacker, const mod_t &mod, int &damage)
{
	if (!Jump_Active())
		return false;

	// A map can turn damage off entirely, including its own hazards.
	if (!jump_mset.damage)
		return true;

	// A trigger_hurt with `dmg 1` is not a hazard at all: jump maps use it to
	// take a weapon back off the player partway through a course. Only
	// hurt_touch raises MOD_TRIGGER_HURT, and it passes the trigger itself as
	// the attacker, so its `dmg` key is readable here and needs no upstream
	// hook. dmg 0 is coerced to 5 in SP_trigger_hurt, so 1 is always deliberate.
	//
	// The two upstream mods disagree on what to strip: classic q2jump removes
	// only the rocket launcher then re-arms the blaster, while Refresh clears
	// all ten weapons and leaves the player holding nothing (and leaks the BFG
	// through a missing line). Resetting to the standard jump loadout gets
	// Refresh's intent without either wrinkle.
	if (mod.id == MOD_TRIGGER_HURT && targ->client && attacker && attacker->dmg == 1)
	{
		Jump_StripInventory(targ);
		ChangeWeapon(targ);
		return true;
	}

	// World hazards are a fail condition, not a health bar: lava does 3 damage
	// a tick, so left alone it would let a player wade out of the pit and
	// carry on. Touching one ends the run outright.
	switch (mod.id)
	{
	case MOD_WATER:
	case MOD_SLIME:
	case MOD_LAVA:
	case MOD_CRUSH:
	case MOD_TRIGGER_HURT:
	case MOD_SUICIDE:
		if (targ->client)
			damage = max(damage, targ->health + 100);
		return false;
	default:
		break;
	}

	// Everything else takes no health, but the damage still has to flow
	// through T_Damage rather than being swallowed here: knockback is applied
	// independently of the damage value, and that knockback is what makes
	// rocket jumping work on maps that enable the rocket mset.
	damage = 0;

	return false;
}
