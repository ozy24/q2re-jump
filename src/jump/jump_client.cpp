// [Jump] Per-client behaviour: spawning, the run timer, damage rules.

#include "../g_local.h"
#include "jump_local.h"

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

	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc)
		return;

	Jump_ResetRun(*jc);
	Jump_StripInventory(ent);
	Jump_ClearCheckpointFlags(ent);

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

	Jump_RefreshPlayerInstancing();
}

void Jump_ClientThink(edict_t *ent, usercmd_t *ucmd)
{
	if (!Jump_Active())
		return;

	Jump_TrackInput(ent, ucmd);

	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc)
		return;

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

	// The run starts on the first movement input, matching Q2JumpRefresh.
	// Deliberately not started by +attack (the classic mod does) so that
	// firing the blaster on the spawn pad doesn't commit you to a run.
	// The rerelease has no upmove axis: jump and crouch are buttons.
	if (ucmd->forwardmove || ucmd->sidemove || (ucmd->buttons & (BUTTON_JUMP | BUTTON_CROUCH)))
	{
		jc->state = jump_run_state_t::running;
		jc->run_start_ms = Jump_NowMs();
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
