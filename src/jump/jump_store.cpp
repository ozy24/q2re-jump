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

	// A frozen replayer's own ent->s.origin is the GHOST's current position,
	// not theirs - without this, `store` during `replay` would silently save
	// a point on the recorded path instead of doing nothing.
	if (jc->replay_mode != jump_replay_mode_t::none)
	{
		gi.LocClient_Print(ent, PRINT_HIGH, "You cannot store while replaying.\n");
		return false;
	}

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
		gi.Client_Print(ent, PRINT_HIGH, "Stores are disabled on Ranked. Use \"team practice\" to practice.\n");
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
		gi.Client_Print(ent, PRINT_HIGH, "No recall on Ranked - restarting your run.\n");
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

	// Get() clamps a request past the stack depth to the oldest slot. Mirror the
	// clamp here so the confirmation below names the slot actually used rather
	// than the one asked for.
	const int effective = (which > jc->stores.count) ? jc->stores.count : which;

	const jump::store_slot_t *slot = jc->stores.Get(effective);

	if (!slot)
		return;

	const vec3_t origin = { slot->origin[0], slot->origin[1], slot->origin[2] };
	const vec3_t angles = { slot->angles[0], slot->angles[1], slot->angles[2] };

	Jump_MovePlayer(ent, origin, angles);

	// A team switch frees the marker but keeps the ring, so the recall that
	// brings you back to Practice has to put the marker back. A normal recall
	// already has one and leaves it alone.
	if (!jc->store_marker)
		Jump_PlaceStoreMarker(ent, *jc, origin);

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

	// A plain `recall` is bound to a key and used constantly, so it stays silent.
	// An explicit number is rare and worth confirming - without it there is no
	// way to tell a deep recall from one clamped to the oldest slot.
	if (which > 1)
		gi.Client_Print(ent, PRINT_HIGH,
						G_Fmt("Recalled store {} of {}.\n", effective, jc->stores.count).data());

	Jump_Log("%s recalled store %d of %d (asked %d)", Jump_DisplayName(ent), effective,
			 jc->stores.count, which);
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

	// Cancel first, state-only (not the position-restoring variant) - `kill`
	// is about to reposition the player itself either way, via recall or a
	// full restart. Without this, `kill` while replaying on Practice with
	// stores would dead-end: Jump_CmdRecall now refuses during playback
	// (Jump_CanStore's replay guard), since the player's own ent->s.origin
	// mid-replay is the ghost's position, not theirs to recall FROM.
	Jump_CancelReplay(ent);

	if (jc->team == jump_team_t::practice && !jc->stores.Empty())
	{
		Jump_CmdRecall(ent, 1);
		return;
	}

	Jump_RestartRun(ent);
}

// Dying on Practice resumes from the last store, the way `kill` already does.
//
// Upstream q2jump gets this for free by overriding the spawn point inside
// PutClientInServer for anyone on Easy (p_client.c:1180-1187), which this port
// cannot copy: it has a restart-from-spawn action that upstream does not, and
// Jump_RestartRun goes through the same function. So the recall hangs off the
// death respawn specifically.
//
// Called after respawn(), not from the death itself: a corpse cannot recall -
// Jump_CanStore refuses at health <= 0 - and the respawn is what restores the
// health, the inventory and the spawn placement this then moves away from.
//
// No store, Ranked, or spectating means do nothing at all. The respawn has
// already happened by this point, so there is nothing to fall back to.
void Jump_PostDeathRespawn(edict_t *ent)
{
	if (!Jump_Active())
		return;

	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc || jc->team != jump_team_t::practice || jc->stores.Empty())
		return;

	Jump_CmdRecall(ent, 1);
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
