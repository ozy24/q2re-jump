// [Jump] Entities used by jump maps.
//
// These are registered here rather than in the upstream `spawns` table so the
// upstream spawn list stays untouched; ED_CallSpawn asks us just before it
// gives up on an unknown classname.

#include "../g_local.h"
#include "jump_local.h"

void InitTrigger(edict_t *self);

// ---------------------------------------------------------------------------
// singlespawn
//
// Most jump maps are built on deathmatch scaffolding and inherit a scattering
// of info_player_deathmatch entities, which stock spawn selection then picks
// between. For a racing mode that is poison: two players on the same map start
// from different places, so their times are not measuring the same run, and a
// start line only exists if there is one of it.
//
// `singlespawn 1` keeps the first spawn the map declares and drops the rest,
// which is upstream q2jump's fix for a map its author never cleaned up
// (g_spawn.c:762-774). First-declared rather than nearest-anything so the
// choice is deterministic and matches what a map tuned against upstream got.
//
// Dropped before ED_CallSpawn rather than freed after it, via the same
// G_InhibitEntity the engine already asks - so the extras never exist, and the
// "N entities inhibited" line stays true.
//
// info_player_deathmatch only, again matching upstream. A map with a single
// info_player_start is already unambiguous, and one that has both is answered
// by the stock preference order.
// ---------------------------------------------------------------------------
bool Jump_InhibitEntity(edict_t *ent)
{
	if (!Jump_Active() || !jump_mset.singlespawn)
		return false;

	if (!ent->classname || strcmp(ent->classname, "info_player_deathmatch") != 0)
		return false;

	if (!jump_level.singlespawn_kept)
	{
		jump_level.singlespawn_kept = true;
		return false;
	}

	return true;
}

// ---------------------------------------------------------------------------
// Teleporter speed gate
// ---------------------------------------------------------------------------

// `speed` on a teleporter is the horizontal speed you must be carrying for it
// to fire. Upstream q2jump has this in teleporter_touch; without it a map that
// gates a teleport on speed becomes unplayable rather than merely different.
//
// `4kv3` is the case that found this: its spawn point sits inside a teleport
// trigger covering the whole track, gated at 4000 ups, so the ungated version
// fires on the first frame and drops the player into the finish room - an
// instant time, in a box, before they have moved.
//
// Both upstream touch paths call this: teleporter_touch (misc_teleporter) and
// trigger_teleport_touch. They are separate functions in the rerelease, which is
// the same split that once let fasttele work on one and not the other.
bool Jump_TeleportSpeedBlocked(edict_t *self, edict_t *other)
{
	if (!Jump_Active() || !other->client)
		return false;

	const float speed = jump::HorizontalSpeed(other->velocity[0], other->velocity[1]);

	if (jump::TeleportSpeedAllows(speed, self->speed))
		return false;

	// Say why, or a teleporter that ignores you is indistinguishable from one
	// that is broken. Rate-limited per player: the trigger touches every frame
	// you stand in it, and on this kind of map that is the whole track.
	jump_client_t *jc = Jump_ClientData(other);

	if (jc && level.time >= jc->tele_speed_print_time)
	{
		jc->tele_speed_print_time = level.time + 2_sec;
		gi.LocClient_Print(other, PRINT_HIGH, "Teleporter needs {} ups; you have {}.\n", (int) self->speed,
						   (int) speed);
	}

	return true;
}

// ---------------------------------------------------------------------------
// trigger_finish / weapon_finish
// ---------------------------------------------------------------------------

TOUCH(jump_finish_touch) (edict_t *self, edict_t *other, const trace_t &tr, bool other_touching_self) -> void
{
	if (!other->client)
		return;

	Jump_Finish(other);
}

static void SP_trigger_finish(edict_t *self)
{
	InitTrigger(self);
	self->touch = jump_finish_touch;
	gi.linkentity(self);
}

// ---------------------------------------------------------------------------
// cpbox_* - checkpoint pickups
// ---------------------------------------------------------------------------

TOUCH(jump_cpbox_touch) (edict_t *self, edict_t *other, const trace_t &tr, bool other_touching_self) -> void
{
	if (!other->client)
		return;

	Jump_TakeCheckpoint(other, self);
}

static void Jump_SpawnBox(edict_t *self, const vec3_t &mins, const vec3_t &maxs, const char *model, bool checkpoint)
{
	self->movetype = MOVETYPE_NONE;
	self->mins = mins;
	self->maxs = maxs;

	if (jump_box_models->integer && model)
	{
		self->s.modelindex = gi.modelindex(model);
		self->s.renderfx |= RF_TRANSLUCENT;
	}

	if (checkpoint)
	{
		self->solid = SOLID_TRIGGER;
		self->touch = jump_cpbox_touch;
		Jump_InvalidateCheckpointTotal();
	}
	else
	{
		self->solid = SOLID_BBOX;
	}

	gi.linkentity(self);
}

// Sizes match the upstream jump mods so existing maps line up.
static void SP_jumpbox_small(edict_t *self)
{
	Jump_SpawnBox(self, { -16, -16, -16 }, { 16, 16, 16 }, "models/jump/smallbox3/tris.md2", false);
}

static void SP_jumpbox_medium(edict_t *self)
{
	Jump_SpawnBox(self, { -32, -32, -16 }, { 32, 32, 48 }, "models/jump/mediumbox3/tris.md2", false);
}

static void SP_jumpbox_large(edict_t *self)
{
	Jump_SpawnBox(self, { -64, -64, -32 }, { 64, 64, 96 }, "models/jump/largebox3/tris.md2", false);
}

static void SP_cpbox_small(edict_t *self)
{
	Jump_SpawnBox(self, { -16, -16, -16 }, { 16, 16, 16 }, "models/jump/smallbox3/tris.md2", true);
}

static void SP_cpbox_medium(edict_t *self)
{
	Jump_SpawnBox(self, { -32, -32, -16 }, { 32, 32, 48 }, "models/jump/mediumbox3/tris.md2", true);
}

static void SP_cpbox_large(edict_t *self)
{
	Jump_SpawnBox(self, { -64, -64, -32 }, { 64, 64, 96 }, "models/jump/largebox3/tris.md2", true);
}

// ---------------------------------------------------------------------------
// Barriers and clip brushes
//
// These are solid geometry in classic jump maps, not decoration: deleting them
// changes the map. jump_clip walls off shortcuts, one_way_wall makes a passage
// traversable in one direction, and the cp barriers gate progress on
// checkpoints.
// ---------------------------------------------------------------------------

// Shove a player back where they came from, as the upstream barriers do.
static void Jump_PushBack(edict_t *other)
{
	gi.unlinkentity(other);

	other->s.origin = other->s.old_origin;
	other->velocity = {};
	other->client->ps.pmove.origin = other->s.origin;
	other->client->ps.pmove.velocity = {};

	gi.linkentity(other);
}

// Barrier messages repeat while the player leans on the brush, so rate-limit
// them per barrier.
static void Jump_BarrierPrint(edict_t *self, edict_t *other, const char *message)
{
	if (self->timestamp > level.time)
		return;

	self->timestamp = level.time + 5_sec;
	gi.Client_Print(other, PRINT_HIGH, message);
}

// Shared by every checkpoint barrier so the wording stays identical to upstream
// in one place.
static void Jump_CheckpointBarrierPrint(edict_t *self, edict_t *other)
{
	Jump_BarrierPrint(self, other,
					  G_Fmt("You need {} checkpoint(s) to pass this barrier.\n", self->count).data());
}

// count = checkpoints required. SPAWNFLAG 1 inverts the test, so the barrier
// closes once you have them rather than opening - upstream uses that to stop
// players going back for checkpoints they already hold.
TOUCH(jump_cpbarrier_touch) (edict_t *self, edict_t *other, const trace_t &tr, bool other_touching_self) -> void
{
	if (!other->client || other->client->resp.spectator)
		return;

	jump_client_t *jc = Jump_ClientData(other);

	if (!jc)
		return;

	const bool inverted = self->spawnflags.has(spawnflags_t(1));
	const bool has_enough = jc->checkpoints >= self->count;

	if (inverted ? !has_enough : has_enough)
		return; // barrier is open

	Jump_PushBack(other);

	if (!inverted)
		Jump_CheckpointBarrierPrint(self, other);
}

static void SP_jump_cpbarrier(edict_t *self)
{
	self->movetype = MOVETYPE_NONE;
	self->solid = SOLID_TRIGGER;

	if (self->model)
		gi.setmodel(self, self->model);

	self->svflags |= SVF_NOCLIENT;
	self->touch = jump_cpbarrier_touch;

	gi.linkentity(self);
}

// A trigger_push whose `target` starts with "checkpoint" is a barrier that gates
// on checkpoints. Unlike jump_cpwall it does not block: upstream leaves the
// ordinary push to do the shoving, so a player who is short of checkpoints gets
// the message and then the vanilla push, which is why this returns false there.
//
// Returning true means the player passed and must not be pushed at all. That
// also skips the caller's PUSH_ONCE G_FreeEdict, matching upstream - a one-shot
// barrier is not consumed by a successful pass.
bool Jump_PushBarrier(edict_t *self, edict_t *other)
{
	if (!Jump_Active() || !self->target)
		return false;

	if (!jump::IsCheckpointBarrierTarget(self->target))
		return false;

	if (!other->client || other->client->resp.spectator)
		return false;

	jump_client_t *jc = Jump_ClientData(other);

	if (!jc)
		return false;

	// A barrier with no `count` has count 0 and is therefore always open, so a
	// mapper who forgets the key just gets a plain trigger_push. Upstream does
	// the same; don't "fix" it, or those maps change behaviour.
	if (jc->checkpoints >= self->count)
		return true;

	Jump_CheckpointBarrierPrint(self, other);

	return false;
}

// Passable in the direction the brush faces, solid against it. SPAWNFLAG 1
// lets a fast enough player through regardless.
TOUCH(jump_one_way_wall_touch) (edict_t *self, edict_t *other, const trace_t &tr, bool other_touching_self) -> void
{
	if (!other->client || other->client->resp.spectator)
		return;

	vec3_t forward;
	AngleVectors(self->s.angles, forward, nullptr, nullptr);

	// A negative dot means the player is moving against the wall's facing.
	if (forward.dot(other->velocity.normalized()) > 0)
		return;

	if (self->spawnflags.has(spawnflags_t(1)) && other->velocity.length() >= self->speed)
		return;

	Jump_PushBack(other);
}

static void SP_jump_one_way_wall(edict_t *self)
{
	self->movetype = MOVETYPE_NONE;
	self->solid = SOLID_TRIGGER;

	if (self->model)
		gi.setmodel(self, self->model);

	self->svflags |= SVF_NOCLIENT;
	self->touch = jump_one_way_wall_touch;

	gi.linkentity(self);
}

// An invisible solid wall. With message "checkpoint" it is a checkpoint
// volume instead, which is how some maps place checkpoints without a cpbox.
static void SP_jump_clip(edict_t *self)
{
	self->movetype = MOVETYPE_NONE;

	if (self->model)
		gi.setmodel(self, self->model);

	if (self->message && !strcmp(self->message, "checkpoint"))
	{
		self->solid = SOLID_TRIGGER;
		self->touch = jump_cpbox_touch;
		Jump_InvalidateCheckpointTotal();
	}
	else
	{
		self->solid = SOLID_BSP;
	}

	// Invisible, as the mapper intended. The client can't predict against an
	// entity it never receives, so expect a single-frame correction when a
	// player runs into one.
	self->svflags |= SVF_NOCLIENT;

	gi.linkentity(self);
}

// ---------------------------------------------------------------------------
// trigger_weapon - hands out a weapon by the classic Quake II weapon number.
// ---------------------------------------------------------------------------

static item_id_t Jump_WeaponByNumber(int number)
{
	switch (number)
	{
	case 0:
		return IT_WEAPON_BFG;
	case 1:
		return IT_WEAPON_BLASTER;
	case 2:
		return IT_WEAPON_SHOTGUN;
	case 3:
		return IT_WEAPON_SSHOTGUN;
	case 4:
		return IT_WEAPON_MACHINEGUN;
	case 5:
		return IT_WEAPON_CHAINGUN;
	case 6:
		return IT_WEAPON_GLAUNCHER;
	case 7:
		return IT_WEAPON_RLAUNCHER;
	case 8:
		return IT_WEAPON_HYPERBLASTER;
	case 9:
		return IT_WEAPON_RAILGUN;
	case 11:
		return IT_AMMO_GRENADES;
	default:
		return IT_NULL;
	}
}

TOUCH(jump_trigger_weapon_touch) (edict_t *self, edict_t *other, const trace_t &tr, bool other_touching_self) -> void
{
	if (!other->client || other->client->resp.spectator)
		return;

	const item_id_t id = Jump_WeaponByNumber(self->count);

	if (id == IT_NULL)
		return;

	gitem_t *item = GetItemByIndex(id);

	if (!item)
		return;

	if (!other->client->pers.inventory[id])
	{
		other->client->pers.inventory[id] = 1;
		other->client->newweapon = item;
		ChangeWeapon(other);
	}

	// Plenty of ammo: running dry mid-route would just be frustrating.
	Add_Ammo(other, item, 1000);
}

static void SP_jump_trigger_weapon(edict_t *self)
{
	self->movetype = MOVETYPE_NONE;
	self->solid = SOLID_TRIGGER;

	if (self->model)
		gi.setmodel(self, self->model);

	self->svflags |= SVF_NOCLIENT;
	self->touch = jump_trigger_weapon_touch;

	gi.linkentity(self);
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

struct jump_spawn_t
{
	const char *name;
	void (*spawn)(edict_t *);
};

static const jump_spawn_t jump_spawns[] = {
	{ "trigger_finish", SP_trigger_finish },
	{ "weapon_finish", SP_trigger_finish }, // deprecated alias
	{ "jumpbox_small", SP_jumpbox_small },
	{ "jumpbox_medium", SP_jumpbox_medium },
	{ "jumpbox_large", SP_jumpbox_large },
	{ "cpbox_small", SP_cpbox_small },
	{ "cpbox_medium", SP_cpbox_medium },
	{ "cpbox_large", SP_cpbox_large },
	{ "jump_clip", SP_jump_clip },
	{ "one_way_wall", SP_jump_one_way_wall },
	{ "jump_cpwall", SP_jump_cpbarrier },
	{ "jump_cpbrush", SP_jump_cpbarrier },
	{ "trigger_weapon", SP_jump_trigger_weapon },
};

// Classnames from old jump maps that carry no collision and only ever drove
// cosmetic effects or the old scoreboard. Freeing these is safe; anything that
// walls off part of a map must get a real spawn function above instead.
static const char *jump_ignored_classnames[] = {
	"jump_time",	 "jump_score",		   "jumpmod_effect",		 "jump_cpeffect", "trigger_lapcounter",
	"trigger_lapcp", "trigger_quad",	   "trigger_quad_clear", "cp_clear",
	"trigger_single_cp_clear",
};

bool Jump_CallSpawn(edict_t *ent)
{
	if (!Jump_Active() || !ent->classname)
		return false;

	for (const auto &s : jump_spawns)
	{
		if (!strcmp(s.name, ent->classname))
		{
			s.spawn(ent);
			return true;
		}
	}

	for (const char *name : jump_ignored_classnames)
	{
		if (!strcmp(name, ent->classname))
		{
			Jump_Log("ignoring legacy entity %s", ent->classname);
			G_FreeEdict(ent);
			return true;
		}
	}

	return false;
}
