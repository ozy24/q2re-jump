// [Jump] Mode lifecycle: cvars, gamemode forcing, per-level state, shared helpers.

#include "../g_local.h"
#include "jump_local.h"

#include <cstdarg>

jump_client_t jump_clients[MAX_CLIENTS];
jump_level_t  jump_level;

cvar_t *g_jump;
cvar_t *jump_debug;
cvar_t *jump_box_models;
cvar_t *jump_data_dir;
cvar_t *jump_records_max;
cvar_t *jump_idle_time;
cvar_t *jump_map_pool;

bool Jump_Active()
{
	return jump_level.active;
}

void Jump_PreInit()
{
	g_jump = gi.cvar("g_jump", "1", CVAR_SERVERINFO | CVAR_LATCH);

	if (!g_jump->integer)
		return;

	// Jump is a deathmatch-only mode and owns the team/scoring stats, so the
	// other team gamemodes have to be off. Mirrors what CTF does here.
	if (!deathmatch->integer)
	{
		gi.Com_Print("Jump: forcing deathmatch.\n");
		gi.cvar_set("deathmatch", "1");
	}
	if (coop->integer)
		gi.cvar_set("coop", "0");
	if (teamplay->integer)
		gi.cvar_set("teamplay", "0");
	if (ctf->integer)
		gi.cvar_set("ctf", "0");
}

void Jump_Init()
{
	jump_debug = gi.cvar("jump_debug", "0", CVAR_NOFLAGS);

	// Off-switch for the jumpbox/cpbox models, which ship with the jump map
	// packs rather than with Quake II itself.
	jump_box_models = gi.cvar("jump_box_models", "1", CVAR_NOFLAGS);

	// Empty means "next to game_x64.dll"; set it to relocate the records.
	jump_data_dir = gi.cvar("jump_data_dir", "", CVAR_NOFLAGS);
	jump_records_max = gi.cvar("jump_records_max", "15", CVAR_NOFLAGS);

	// Seconds of no input before a player is moved to spectator; 0 disables.
	jump_idle_time = gi.cvar("jump_idle_time", "300", CVAR_NOFLAGS);

	// Maps that can be voted for but are not in the rotation. Same format as
	// the engine's g_map_list: whitespace separated map names.
	jump_map_pool = gi.cvar("g_map_pool", "", CVAR_NOFLAGS);

	if (!g_jump->integer)
		return;

	// Players run through each other and never take fall damage.
	gi.cvar_set("g_disable_player_collision", "1");
	gi.cvar_set("g_dm_no_fall_damage", "1");

	// Nothing in a jump map should be randomised or instagib'd.
	gi.cvar_set("g_dm_random_items", "0");
	gi.cvar_set("g_instagib", "0");

	Jump_LoadMapList();

	gi.Com_PrintFmt("==== Jump mode enabled ====\n");
}

void Jump_InitLevel(const char *entities)
{
	const bool was_active = jump_level.active;

	jump_level = {};

	jump_level.active = g_jump && g_jump->integer && deathmatch->integer;
	Q_strlcpy(jump_level.mapname, level.mapname, sizeof(jump_level.mapname));

	Jump_InvalidateCheckpointTotal();

	// Per-map client state. Session fields (team) deliberately survive.
	for (auto &jc : jump_clients)
	{
		jc.state = jump_run_state_t::idle;
		jc.run_start = 0_ms;
		jc.last_time_ms = 0;
		jc.pb_time_ms = 0;
		jc.checkpoints = 0;
		jc.stores.Clear();
		jc.store_marker = nullptr; // freed with TAG_LEVEL
	}

	Jump_LoadMsets(entities);
	Jump_LoadRecords();

	// PB is seeded per client in Jump_ClientSpawn, not here: SpawnEntities
	// flags every client disconnected a few lines above this hook, so there is
	// nobody to seed yet.

	if (jump_level.active && !was_active)
		Jump_Log("level init: %s", jump_level.mapname);
}

void Jump_RunFrame()
{
	if (!Jump_Active())
		return;

	Jump_VoteFrame();
	Jump_IdleFrame();
}

void Jump_Shutdown()
{
	if (Jump_Active())
		Jump_SaveRecords();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

jump_client_t *Jump_ClientData(edict_t *ent)
{
	if (!ent || !ent->client)
		return nullptr;

	const ptrdiff_t index = ent->client - game.clients;

	if (index < 0 || index >= (ptrdiff_t) game.maxclients)
		return nullptr;

	return &jump_clients[index];
}

int64_t Jump_RunTimeMs(const jump_client_t &jc)
{
	if (jc.state == jump_run_state_t::idle)
		return 0;

	if (jc.state == jump_run_state_t::finished)
		return jc.last_time_ms;

	return (level.time - jc.run_start).milliseconds();
}

void Jump_ResetRun(jump_client_t &jc)
{
	jc.state = jump_run_state_t::idle;
	jc.run_start = 0_ms;
	jc.checkpoints = 0;
}

void Jump_RestartRun(edict_t *ent)
{
	jump_client_t *jc = Jump_ClientData(ent);

	if (jc)
		Jump_ResetRun(*jc);

	PutClientInServer(ent);
	G_PostRespawn(ent);
}

void Jump_FreeStoreMarker(jump_client_t &jc)
{
	if (jc.store_marker)
	{
		G_FreeEdict(jc.store_marker);
		jc.store_marker = nullptr;
	}
}

void Jump_MovePlayer(edict_t *ent, const vec3_t &origin, const vec3_t &angles)
{
	gi.unlinkentity(ent);

	ent->s.origin = origin;
	ent->s.origin[2] += 10;
	ent->s.old_origin = ent->s.origin;

	ent->velocity = {};
	ent->client->ps.pmove.velocity = {};
	ent->client->ps.pmove.origin = ent->s.origin;

	// Unlike a teleporter, a recall does not freeze the player: they should be
	// able to move the instant they land back on the store.
	ent->client->ps.pmove.pm_time = 0;
	ent->client->ps.pmove.pm_flags &= ~PMF_TIME_TELEPORT;

	ent->client->ps.pmove.delta_angles = angles - ent->client->resp.cmd_angles;
	ent->s.angles = {};
	ent->client->ps.viewangles = {};
	ent->client->v_angle = {};
	AngleVectors(ent->client->v_angle, ent->client->v_forward, nullptr, nullptr);

	gi.linkentity(ent);
}

void Jump_Log(const char *fmt, ...)
{
	if (!jump_debug || !jump_debug->integer)
		return;

	char	buffer[1024];
	va_list args;

	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	gi.Com_PrintFmt("[jump] {}\n", buffer);
}
