// [Jump] Per-client behaviour: spawning, the run timer, damage rules.

#include "../g_local.h"
#include "jump_local.h"

// Jump players carry nothing but the blaster, which is harmless because
// Jump_FilterDamage swallows all combat damage.
void Jump_StripInventory(edict_t *ent)
{
	gclient_t *client = ent->client;

	client->pers.inventory.fill(0);
	client->pers.inventory[IT_WEAPON_BLASTER] = 1;

	client->pers.weapon = GetItemByIndex(IT_WEAPON_BLASTER);
	client->pers.lastweapon = client->pers.weapon;
	client->newweapon = client->pers.weapon;
	client->pers.selected_item = IT_WEAPON_BLASTER;

	client->quad_time = 0_ms;
	client->invincible_time = 0_ms;
	client->enviro_time = 0_ms;
	client->breather_time = 0_ms;
	client->silencer_shots = 0;
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
	jc->pb_time_ms = Jump_PersonalBest(ent);

	// Health is deliberately left at the default. Inflating it does nothing
	// useful once combat damage is zeroed, and it makes hazards take seconds
	// to kill instead of ending the run.
}

void Jump_ClientThink(edict_t *ent, usercmd_t *ucmd)
{
	if (!Jump_Active())
		return;

	Jump_TrackInput(ent, ucmd);

	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc || jc->state != jump_run_state_t::idle)
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
}

bool Jump_FilterDamage(edict_t *targ, edict_t *attacker, const mod_t &mod, int &damage)
{
	if (!Jump_Active())
		return false;

	// A map can turn damage off entirely, including its own hazards.
	if (!jump_mset.damage)
		return true;

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
	(void) targ;
	(void) attacker;
	damage = 0;

	return false;
}
