// [Jump] Finish detection and checkpoints.
//
// Following Q2JumpRefresh: touching a weapon (or a trigger_finish) ends the
// run, key items and cpboxes are checkpoints, and everything else in the map
// is inert.

#include "../g_local.h"
#include "jump_local.h"

// Number of checkpoints in the map, computed lazily on first use so it doesn't
// matter whether entities have finished spawning yet. -1 means "not counted".
static int jump_checkpoint_total_cache = -1;

void Jump_InvalidateCheckpointTotal()
{
	jump_checkpoint_total_cache = -1;
}

bool Jump_IsCheckpointEntity(edict_t *ent)
{
	if (ent->item && (ent->item->flags & IF_KEY))
		return true;

	if (!ent->classname)
		return false;

	return !strncmp(ent->classname, "cpbox_", 6);
}

int Jump_CheckpointTotal()
{
	if (jump_checkpoint_total_cache >= 0)
		return jump_checkpoint_total_cache;

	int total = 0;

	for (uint32_t i = 0; i < globals.num_edicts; i++)
	{
		edict_t *e = &g_edicts[i];

		if (!e->inuse)
			continue;

		if (Jump_IsCheckpointEntity(e))
			total++;
	}

	jump_checkpoint_total_cache = total;
	jump_level.checkpoint_total = total;

	return total;
}

bool Jump_TakeCheckpoint(edict_t *ent, edict_t *cp)
{
	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc)
		return false;

	if (ent->client->resp.spectator)
		return true;

	const int32_t playernum = ent->s.number - 1;

	if (playernum < 0 || playernum >= (int32_t) MAX_CLIENTS)
		return true;

	// Each checkpoint counts once per life; the flag is cleared on respawn.
	if (cp->item_picked_up_by[playernum])
		return true;

	cp->item_picked_up_by[playernum] = true;
	jc->checkpoints++;

	const int total = Jump_CheckpointTotal();

	gi.Client_Print(ent, PRINT_HIGH,
					G_Fmt("You reached checkpoint {}/{} in {} seconds.\n", jc->checkpoints, total,
						  jump::FormatTime(Jump_RunTimeMs(*jc)).c_str())
						.data());

	if (cp->noise_index)
		gi.sound(ent, CHAN_ITEM, cp->noise_index, 1, ATTN_NORM, 0);

	return true;
}

void Jump_ClearCheckpointFlags(edict_t *ent)
{
	const int32_t playernum = ent->s.number - 1;

	if (playernum < 0 || playernum >= (int32_t) MAX_CLIENTS)
		return;

	for (uint32_t i = 0; i < globals.num_edicts; i++)
	{
		edict_t *e = &g_edicts[i];

		if (e->inuse && Jump_IsCheckpointEntity(e))
			e->item_picked_up_by[playernum] = false;
	}
}

void Jump_Finish(edict_t *ent)
{
	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc || ent->client->resp.spectator)
		return;

	if (jc->state != jump_run_state_t::running)
		return;

	const int total = Jump_CheckpointTotal();

	if (total > 0 && jc->checkpoints < total)
	{
		gi.Client_Print(ent, PRINT_HIGH,
						G_Fmt("You need all {} checkpoints to finish ({} taken).\n", total, jc->checkpoints).data());
		return;
	}

	const int64_t time_ms = Jump_RunTimeMs(*jc);

	jc->state = jump_run_state_t::finished;
	jc->last_time_ms = time_ms;

	const std::string time_str = jump::FormatTime(time_ms);

	if (jc->pb_time_ms == 0)
	{
		gi.Broadcast_Print(PRINT_HIGH,
						   G_Fmt("{} finished in {} seconds\n", ent->client->pers.netname, time_str.c_str()).data());
		jc->pb_time_ms = time_ms;
	}
	else
	{
		const std::string delta = jump::FormatDelta(time_ms - jc->pb_time_ms);

		gi.Broadcast_Print(PRINT_HIGH, G_Fmt("{} finished in {} seconds (PB {})\n", ent->client->pers.netname,
											 time_str.c_str(), delta.c_str())
										   .data());

		if (time_ms < jc->pb_time_ms)
			jc->pb_time_ms = time_ms;
	}

	Jump_Log("%s finished: %lld ms", ent->client->pers.netname, (long long) time_ms);
}

bool Jump_ItemTouch(edict_t *ent, edict_t *other)
{
	if (!Jump_Active())
		return false;

	if (!ent->item || !other->client)
		return false;

	// Keys are checkpoints.
	if (ent->item->flags & IF_KEY)
		return Jump_TakeCheckpoint(other, ent);

	// Any weapon is the finish line. Weapons are never handed out and the
	// entity is never removed, so every player can finish on the same one.
	if (ent->item->flags & IF_WEAPON)
	{
		Jump_Finish(other);
		return true;
	}

	// Ammo, armour, health, powerups: all inert in a jump map.
	return true;
}
