// [Jump] Internal declarations shared across the jump module.
//
// Include order in every jump_*.cpp is: "../g_local.h" then "jump_local.h".

#pragma once

#include "jump_logic.h"

#include <filesystem>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

// Upstream q2jump calls these Easy and Hard, which reads as map difficulty
// rather than what they actually are: a practice mode and a timed one.
enum class jump_team_t : uint8_t
{
	spectator = 0,
	practice = 1,
	ranked = 2
};

enum class jump_run_state_t : uint8_t
{
	idle = 0,	 // spawned, timer not started yet
	running = 1, // timer counting
	finished = 2 // reached the finish, waiting for a respawn
};

// Per-client mod state. Lives in a module-owned array rather than on
// gclient_t, so it never has to be described to the savegame reflection
// tables in g_save.cpp.
struct jump_client_t
{
	// --- per map: cleared by Jump_ResetRun / Jump_InitLevel ---
	jump_run_state_t	 state = jump_run_state_t::idle;
	gtime_t				 run_start = 0_ms;
	int64_t				 last_time_ms = 0; // most recent completed run
	int32_t				 checkpoints = 0;
	jump::store_ring_t	 stores;
	edict_t				*store_marker = nullptr;
	gtime_t				 last_input_time = 0_ms;

	// --- session: survives map changes, re-keyed on connect ---
	jump_team_t team = jump_team_t::practice;
	int64_t		pb_time_ms = 0; // 0 = none yet (per map; reset in Jump_InitLevel)
};

// Per-map settings, reloaded on every level change.
struct jump_mset_t
{
	int32_t gravity = 800;
	bool	gravity_set = false;

	int32_t checkpoint_total = 0;
	bool	checkpoint_total_set = false;

	bool damage = true;	  // false disables even world hazards
	bool fasttele = false; // skip the teleporter freeze

	// Weapons the map wants usable rather than treated as the finish line.
	bool weapon_rocket = false;
	bool weapon_grenadelauncher = false;
	bool weapon_hyperblaster = false;
	bool weapon_bfg = false;
};

// Per-level mod state, memset by Jump_InitLevel.
struct jump_level_t
{
	bool	active = false;			 // jump owns this level
	int32_t checkpoint_total = 0;	 // checkpoints required to finish
	char	mapname[64] = { 0 };
};

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

extern jump_client_t jump_clients[MAX_CLIENTS];
extern jump_level_t	 jump_level;
extern jump_mset_t	 jump_mset;

extern cvar_t *g_jump;
extern cvar_t *jump_debug;
extern cvar_t *jump_box_models;
extern cvar_t *jump_data_dir;
extern cvar_t *jump_records_max;
extern cvar_t *jump_idle_time;
extern cvar_t *jump_map_pool;

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

// Returns nullptr for entities that aren't connected clients.
jump_client_t *Jump_ClientData(edict_t *ent);

// Elapsed run time in milliseconds; 0 unless the timer has started.
int64_t Jump_RunTimeMs(const jump_client_t &jc);

// Drops the run back to idle and clears checkpoints. Stores are untouched.
void Jump_ResetRun(jump_client_t &jc);

// Puts the player back at the spawn with a clean run.
void Jump_RestartRun(edict_t *ent);

// Teleport without the teleporter freeze: origin, angles, zeroed velocity.
void Jump_MovePlayer(edict_t *ent, const vec3_t &origin, const vec3_t &angles);

void Jump_FreeStoreMarker(jump_client_t &jc);

// jump_finish.cpp
void Jump_Finish(edict_t *ent);
bool Jump_TakeCheckpoint(edict_t *ent, edict_t *cp);
int	 Jump_CheckpointTotal();
void Jump_InvalidateCheckpointTotal();
void Jump_ClearCheckpointFlags(edict_t *ent);

// jump_store.cpp
void Jump_CmdStore(edict_t *ent);
void Jump_CmdRecall(edict_t *ent, int which);
void Jump_CmdReset(edict_t *ent);
void Jump_CmdKill(edict_t *ent);

// jump_client.cpp
void Jump_StripInventory(edict_t *ent);

// jump_team.cpp
const char *Jump_TeamName(jump_team_t team);
void		Jump_JoinTeam(edict_t *ent, jump_team_t team);
void		Jump_CmdTeam(edict_t *ent);

// jump_files.cpp
const std::filesystem::path &Jump_DataRoot();
std::filesystem::path		 Jump_MapTimesPath(const char *mapname);
bool						 Jump_ReadFile(const std::filesystem::path &path, std::string &out);
bool						 Jump_WriteFileAtomic(const std::filesystem::path &path, const std::string &contents);

// jump_records.cpp
const jump::map_records_t &Jump_Records();
const char				  *Jump_PlayerId(edict_t *ent);
const char				  *Jump_DisplayName(edict_t *ent);
void					   Jump_LoadRecords();
void					   Jump_SaveRecords();
int64_t					   Jump_PersonalBest(edict_t *ent);
int64_t					   Jump_MapRecord();
int						   Jump_SubmitTime(edict_t *ent, int64_t time_ms);
void					   Jump_PlayerTotals(const std::string &id, int &points, int &completions, int &firsts);

// jump_mset.cpp
void Jump_LoadMsets(const char *entities);
bool Jump_IsUsableWeapon(item_id_t id);

// jump_vote.cpp
void Jump_LoadMapList();
void Jump_VoteFrame();
void Jump_IdleFrame();
void Jump_TrackInput(edict_t *ent, usercmd_t *ucmd);
void Jump_CmdVoteMap(edict_t *ent);
void Jump_CmdNominate(edict_t *ent);
void Jump_CmdTimeExtend(edict_t *ent);
void Jump_CmdVote(edict_t *ent, bool yes);
void Jump_CmdMapList(edict_t *ent);
void Jump_CmdIdle(edict_t *ent);
void Jump_VoteClientDisconnect(edict_t *ent);
bool Jump_StartMapVote(edict_t *ent, const char *map);

// Every votable map: g_map_pool first, then g_map_list, de-duplicated.
std::vector<std::string> Jump_CollectVotableMaps();

bool		Jump_VoteActive();
const char *Jump_VoteDescription();
const char *Jump_VoteCaller();
int			Jump_VoteSecondsLeft();
void		Jump_VoteTally(int &yes, int &no, int &needed);
bool		Jump_HasVoted(edict_t *ent);

// jump_menu.cpp
void Jump_OpenMainMenu(edict_t *ent);
void Jump_OpenVoteMenu(edict_t *ent);
void Jump_CmdMenu(edict_t *ent);

// jump_scoreboard.cpp (Jump_ScoreboardMessage is declared in jump.h)
void Jump_CmdMapTimes(edict_t *ent);
void Jump_CmdPlayerTimes(edict_t *ent);
void Jump_CmdRanks(edict_t *ent);

// Debug logging, no-op unless jump_debug is set.
void Jump_Log(const char *fmt, ...);
