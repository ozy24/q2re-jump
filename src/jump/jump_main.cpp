// [Jump] Mode lifecycle: cvars, gamemode forcing, per-level state, shared helpers.

#include "../g_local.h"
#include "jump_local.h"
#include "jump_version.h"

#include <chrono>
#include <cstdarg>

jump_client_t jump_clients[MAX_CLIENTS];
jump_level_t  jump_level;

cvar_t *g_jump;
cvar_t *jump_version;
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
	// Read-only, purely informational; server admins and status tools read it
	// off serverinfo rather than parsing a print.
	jump_version = gi.cvar("jump_version", JUMP_VERSION_STRING, CVAR_SERVERINFO | CVAR_NOSET);

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

	gi.Com_PrintFmt("==== Jump mode enabled (v" JUMP_VERSION_STRING ") ====\n");
}

void Jump_InitLevel(const char *entities)
{
	const bool was_active = jump_level.active;

	jump_level = {};

	jump_level.active = g_jump && g_jump->integer && deathmatch->integer;
	Q_strlcpy(jump_level.mapname, level.mapname, sizeof(jump_level.mapname));

	// Safe here and only here: this hook runs from SpawnEntities, so a map is
	// loading and the configstring each soundindex allocates is part of the
	// load rather than a mid-game broadcast. The same calls in Jump_Init would
	// kill the server at startup - see the note in AGENTS.md.
	if (jump_level.active)
	{
		jump_level.sound_pb = gi.soundindex("misc/secret.wav");
		jump_level.sound_record = gi.soundindex("ctf/flagcap.wav");

		// The grapple is a Practice tool here, so nothing in the map spawns it
		// and stock's CTFPrecache never runs - the mode forces ctf off. Precache
		// it by hand, from level spawn rather than Jump_Init, for the reason in
		// AGENTS.md: a configstring allocated before a server exists overflows
		// an unallocated sizebuf and kills the game at startup.
		PrecacheItem(GetItemByIndex(IT_WEAPON_GRAPPLE));

		// Every fill level of the strafe bar, written once here so that showing
		// it during play is a stat pointing at one of these rather than a text
		// write per frame - a configstring write broadcasts to everyone, which
		// would be unaffordable at frame rate and is free at map load.
		// Two families: the ordinary bar, then the same fills again carrying the
		// dead-side marker. Which one a client sees is a stat pointing at a
		// different slot, so the marker costs no extra traffic during play.
		static_assert(2 * (jump::STRAFE_BAR_SEGMENTS + 1) <=
						  CONFIG_JUMP_STRAFE_BAR_END - CONFIG_JUMP_STRAFE_BAR + 1,
					  "strafe bar needs more configstring slots");

		for (int i = 0; i <= jump::STRAFE_BAR_SEGMENTS; i++)
		{
			gi.configstring(CONFIG_JUMP_STRAFE_BAR + i, jump::StrafeBarString(i).c_str());
			gi.configstring(CONFIG_JUMP_STRAFE_BAR + jump::STRAFE_BAR_DEAD_OFFSET + i,
							jump::StrafeBarString(i, jump::STRAFE_BAR_SEGMENTS, true).c_str());
		}

		// The speedometer's digits, same idea: ten strings written once cover
		// every number it can show. The blank is a space rather than an empty
		// string, so a leading zero draws nothing without relying on how the
		// client treats an unset configstring.
		for (int i = 0; i < 10; i++)
		{
			const char digit[2] = { (char) ('0' + i), '\0' };
			gi.configstring(CONFIG_JUMP_DIGIT + i, digit);
		}

		gi.configstring(CONFIG_JUMP_DIGIT_BLANK, " ");
	}

	Jump_InvalidateCheckpointTotal();

	// Per-map client state. Session preferences (eyecam, jumpers, the menu
	// hint) survive; team does not, because the join gate re-runs every level.
	for (auto &jc : jump_clients)
	{
		jc.state = jump_run_state_t::idle;
		jc.run_start_ms = 0;
		jc.last_time_ms = 0;
		jc.session_best_ms = 0;
		jc.pb_time_ms = 0;
		jc.checkpoints = 0;
		jc.stores.Clear();
		jc.store_marker = nullptr; // freed with TAG_LEVEL
		jc.takeoff.Reset();
		jc.takeoff_written = false;

		jc.team = jump_team_t::spectator;
		jc.team_chosen = false;
		jc.menu_prompted = false;
		jc.menu_prompt_time = 0_ms;
	}

	// PMenu_Open allocates the handle and its entries with TAG_LEVEL, which
	// SpawnEntities has just freed a few lines above this hook - but nothing
	// upstream nulls client->menu, so a player who was in a menu when the map
	// changed is left pointing at freed memory, and the next PMenu_Close frees
	// it a second time. That is not hypothetical here: the map vote menu is
	// exactly what is open when a vote passes and changes the map.
	//
	// showscores is only cleared for clients that were actually in a menu -
	// it is the shared open/closed flag for the layout slot, so blanking it
	// wholesale would also close anyone's scoreboard.
	for (uint32_t i = 0; i < game.maxclients; i++)
	{
		if (!game.clients[i].menu)
			continue;

		game.clients[i].menu = nullptr;
		game.clients[i].showscores = false;
		game.clients[i].menudirty = false;
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
	Jump_MsetFrame();
	Jump_AnnounceFrame();
	Jump_StatusbarFrame();
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

int64_t Jump_NowMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
			   std::chrono::steady_clock::now().time_since_epoch())
		.count();
}

int64_t Jump_RunTimeMs(const jump_client_t &jc)
{
	if (jc.state == jump_run_state_t::idle)
		return 0;

	if (jc.state == jump_run_state_t::finished)
		return jc.last_time_ms;

	return Jump_NowMs() - jc.run_start_ms;
}

void Jump_ResetRun(jump_client_t &jc)
{
	jc.state = jump_run_state_t::idle;
	jc.run_start_ms = 0;
	jc.checkpoints = 0;

	// The takeoff mark belongs to the run that just ended. A restart or a recall
	// puts the player back with no speed, so carrying the old number over would
	// leave them comparing against a jump from a run they are no longer on.
	jc.takeoff.Reset();
	jc.takeoff_written = false;
}

void Jump_RestartRun(edict_t *ent)
{
	jump_client_t *jc = Jump_ClientData(ent);

	if (jc)
		Jump_ResetRun(*jc);

	// Before PutClientInServer, which memsets gclient_t (p_client.cpp:2143) and
	// with it the only pointer to the grapple. See Jump_MovePlayer.
	CTFPlayerResetGrapple(ent);

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
	// A hook attached to somewhere else in the map would immediately drag the
	// player back off the position they just recalled to. Stock resets the
	// grapple on death and through a teleporter for the same reason; a recall is
	// the third way to leave a place suddenly, and it is ours.
	//
	// No-op when nothing is attached, so this costs a null check on Ranked.
	CTFPlayerResetGrapple(ent);

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
