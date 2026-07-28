// [Jump] Entities used by jump maps.
//
// These are registered here rather than in the upstream `spawns` table so the
// upstream spawn list stays untouched; ED_CallSpawn asks us just before it
// gives up on an unknown classname.

#include "../g_local.h"
#include "jump_local.h"

void InitTrigger(edict_t *self);

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
};

// Classnames from old jump maps that have no behaviour here but shouldn't
// spam the console with "doesn't have a spawn function" either.
static const char *jump_ignored_classnames[] = {
	"jump_clip",	   "jump_time",		 "jump_score",		"jumpmod_effect", "jump_cpwall",
	"jump_cpbrush",	   "jump_cpeffect",	 "one_way_wall",	"trigger_lapcounter",
	"trigger_lapcp",   "trigger_quad",	 "trigger_quad_clear", "cp_clear",
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
