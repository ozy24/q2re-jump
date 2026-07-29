// [Jump] Store / recall: a small ring of saved positions the player can
// teleport back to. Follows the Q2JumpRefresh model - no ground requirement,
// no cooldown, velocity always dropped on recall.

#include "../g_local.h"
#include "jump_local.h"

constexpr const char *STORE_MARKER_MODEL = "models/monsters/commandr/head/tris.md2";

static bool Jump_CanStore(edict_t *ent, jump_client_t *jc)
{
	if (!Jump_Active() || !jc)
		return false;

	if (ent->client->resp.spectator || ent->client->chase_target)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "You cannot store while spectating.\n");
		return false;
	}

	if (ent->health <= 0)
		return false;

	return true;
}

static void Jump_PlaceStoreMarker(edict_t *ent, jump_client_t &jc, const vec3_t &origin)
{
	Jump_FreeStoreMarker(jc);

	edict_t *marker = G_Spawn();

	marker->classname = "jump_store_marker";
	marker->movetype = MOVETYPE_NONE;
	marker->solid = SOLID_NOT;
	marker->clipmask = CONTENTS_NONE;
	marker->owner = ent;
	marker->s.modelindex = gi.modelindex(STORE_MARKER_MODEL);
	marker->s.renderfx |= RF_TRANSLUCENT;
	marker->s.origin = origin;
	marker->s.origin[2] -= 10;
	marker->s.old_origin = marker->s.origin;

	gi.linkentity(marker);

	jc.store_marker = marker;
}

void Jump_CmdStore(edict_t *ent)
{
	jump_client_t *jc = Jump_ClientData(ent);

	if (!Jump_CanStore(ent, jc))
		return;

	if (jc->team == jump_team_t::ranked)
	{
		gi.Client_Print(ent, PRINT_HIGH, "Stores are disabled on Hard. Use \"team easy\" to practice.\n");
		return;
	}

	jump::store_slot_t slot;

	slot.origin[0] = ent->s.origin[0];
	slot.origin[1] = ent->s.origin[1];
	slot.origin[2] = ent->s.origin[2];

	// Roll is zeroed so the view isn't tilted after a recall.
	slot.angles[PITCH] = ent->client->v_angle[PITCH];
	slot.angles[YAW] = ent->client->v_angle[YAW];
	slot.angles[ROLL] = 0.f;

	slot.elapsed_ms = Jump_RunTimeMs(*jc);
	slot.checkpoints = jc->checkpoints;

	jc->stores.Push(slot);

	Jump_PlaceStoreMarker(ent, *jc, ent->s.origin);
	Jump_Log("%s stored (%d held)", Jump_DisplayName(ent), jc->stores.count);
}

void Jump_CmdRecall(edict_t *ent, int which)
{
	jump_client_t *jc = Jump_ClientData(ent);

	if (!Jump_CanStore(ent, jc))
		return;

	// There is no recall on Ranked: the only way back is a fresh run. This is
	// what keeps ranked times comparable.
	if (jc->team == jump_team_t::ranked)
	{
		gi.Client_Print(ent, PRINT_HIGH, "No recall on Hard - restarting your run.\n");
		Jump_RestartRun(ent);
		return;
	}

	if (jc->stores.Empty())
	{
		gi.Client_Print(ent, PRINT_HIGH, "You have no stores.\n");
		return;
	}

	if (which < 1 || which > jump::MAX_STORES)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "Invalid store number.\n");
		return;
	}

	const jump::store_slot_t *slot = jc->stores.Get(which);

	if (!slot)
		return;

	const vec3_t origin = { slot->origin[0], slot->origin[1], slot->origin[2] };
	const vec3_t angles = { slot->angles[0], slot->angles[1], slot->angles[2] };

	Jump_MovePlayer(ent, origin, angles);

	// The elapsed time travels with the store, so recalling costs you only the
	// time you spent past that point.
	jc->checkpoints = slot->checkpoints;

	if (slot->elapsed_ms > 0)
	{
		jc->state = jump_run_state_t::running;
		jc->run_start_ms = Jump_NowMs() - slot->elapsed_ms;
	}
	else
	{
		Jump_ResetRun(*jc);
		jc->checkpoints = slot->checkpoints;
	}

	Jump_Log("%s recalled store %d", Jump_DisplayName(ent), which);
}

// Upstream's Cmd_Kill_f refuses to run for the first five seconds after a
// spawn, which is unusable in a mode whose core loop is "miss the jump, go
// again". Jump handles it instead: on Practice a stored position is the natural
// place to resume from, otherwise it's a clean run from the spawn.
void Jump_CmdKill(edict_t *ent)
{
	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc || ent->client->resp.spectator)
		return;

	if (jc->team == jump_team_t::practice && !jc->stores.Empty())
	{
		Jump_CmdRecall(ent, 1);
		return;
	}

	Jump_RestartRun(ent);
}

void Jump_CmdReset(edict_t *ent)
{
	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc)
		return;

	jc->stores.Clear();
	Jump_FreeStoreMarker(*jc);

	gi.LocClient_Print(ent, PRINT_HIGH, "Stores cleared.\n");
}
