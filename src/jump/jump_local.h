// [Jump] Internal declarations shared across the jump module.
//
// Include order in every jump_*.cpp is: "../g_local.h" then "jump_local.h".

#pragma once

#include "jump_logic.h"
#include "jump_stats.h" // JUMP_READOUT_*, and the HUD anchors the menu talks about

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

// Whether `replay` is currently driving this client's own body through a
// saved run. Distinct from jump_run_state_t: playback requires state to be
// idle or finished (a run in progress refuses the command), and while active
// this is what the pm_type hook in p_client.cpp and the idle-kick exemption
// in jump_vote.cpp both key off.
enum class jump_replay_mode_t : uint8_t
{
	none = 0,
	playback = 1
};

// Beam-segment edicts per racer for the raceline trail - the classic mods'
// "race spark" uses 3 segments over 100ms/segment (10Hz data); this uses
// four times as many segments at roughly a third the spacing for finer
// resolution against our 40Hz data, which shrinks how much geometry any
// single straight segment can cut across at a turn. Edict cost is trivial
// either way (MAX_EDICTS is 8192).
constexpr int JUMP_RACE_TRAIL_SEGMENTS = 12;

// The scoreboard has two pages, because one svc_layout cannot hold both: the
// F1 key cycles players -> records -> closed.
enum class jump_board_t : uint8_t
{
	players = 0, // who is connected and how they are doing on this map
	records = 1	 // the map's high score table
};

// Per-client mod state. Lives in a module-owned array rather than on
// gclient_t, so it never has to be described to the savegame reflection
// tables in g_save.cpp.
struct jump_client_t
{
	// --- per map: cleared by Jump_ResetRun / Jump_InitLevel ---
	jump_run_state_t	 state = jump_run_state_t::idle;
	int64_t				 run_start_ms = 0; // Jump_NowMs() stamp; 0 when idle
	int64_t				 last_time_ms = 0;	  // most recent completed run
	int64_t				 session_best_ms = 0; // best ranked run since this map loaded
	int32_t				 checkpoints = 0;
	jump::store_ring_t	 stores;
	edict_t				*store_marker = nullptr;
	gtime_t				 last_input_time = 0_ms;
	gtime_t				 finish_deny_time = 0_ms; // rate-limit "need checkpoints" spam

	// The in-progress recording of this run, sampled from Jump_ClientThink
	// (Jump_TrackReplay) and saved on a personal best (Jump_Finish). Belongs
	// with the rest of the per-run state: a restart or a new map should not
	// splice together frames from two different runs.
	jump::replay_recorder_t replay_rec;

	// The speedometer frozen at the last takeoff. Tracked per usercmd like the
	// strafe meter, and for the same reason: the server sees every command, so
	// it catches the exact frame the feet leave the ground.
	jump::jump_takeoff_state_t takeoff;
	int						 takeoff_shown = 0;	  // what the configstring currently holds
	bool					 takeoff_written = false; // whether it holds anything at all

	// The strafe meter, accumulated per usercmd. The server sees every command
	// rather than one per rendered frame, so this is a finer sample than the
	// client half can take - it just has a coarser way of drawing the result.
	jump::strafe_state_t   strafe;
	jump::strafe_readout_t strafe_readout;
	int64_t				   strafe_last_ms = 0;


	// --- per map: the join gate ---
	// Nobody enters the map until they have answered the join menu, and every
	// level re-asks, so all three reset in Jump_InitLevel.
	bool	team_chosen = false;	   // answered the join menu on this level
	bool	menu_prompted = false;	   // the auto-open already fired this level
	gtime_t menu_prompt_time = 0_ms;   // when to auto-open; 0 = not armed

	// --- session: survives map changes, re-keyed on connect ---
	// team is not session state: the gate re-runs every level, so spectator is
	// the pre-choice holding state rather than a default anyone plays on.
	jump_team_t team         = jump_team_t::spectator;
	bool        eyecam       = true; // MuffMode default: first-person spectator follow
	bool        show_jumpers = true; // other players' models/sounds; `jumpers` toggles
	bool        menu_hint_shown = false; // %bind:inven% hint printed once
	// Whether the SERVER draws each readout for this player, for a client that
	// is NOT running the mod's own. `speedo` / `strafebar` set these.
	//
	// THE SHIPPED BASELINE, matched by the cvars the overlay registers and by
	// Jump_ResetReadouts: the speedometer on, everything else off.
	//
	// The default HUD is what you need to PLAY - timer, checkpoints, stores,
	// personal best, team, time remaining - plus the one readout that needs no
	// explaining. A number that goes up when you do well teaches on sight and
	// costs nothing to ignore. The takeoff mark, the strafe meter and CGaz all
	// have to be read before they help, so they are asked for, and Options is
	// where a player goes looking.
	//
	// Keep these three in step with jump_hud_draw.cpp's cvar defaults and with
	// Jump_ResetReadouts, or "reset" stops meaning "what a fresh install shows".
	bool        server_strafebar = false;
	bool        server_speedo = true;

	// What the mod's own client reports about its overlay cvars, via `jumphud`.
	// A stock client never sends it, so client_hud is also how the server knows
	// whether it is talking to one - which decides both who owns the readouts
	// (these values, live, rather than the two flags above) and whether stuffing
	// a cvar at this player would mean anything.
	bool        client_hud = false;
	int         client_hud_master = 1; // jump_hud
	int         client_speed = JUMP_READOUT_ON;
	int         client_strafe = JUMP_READOUT_ON;
	int         client_cgaz = JUMP_READOUT_OFF;

	// Whether the SERVER draws the takeoff mark for this player. Unlike the two
	// above there is no client copy and no handshake: the bar is the only thing
	// that draws it, so one flag governs it for everybody.
	bool        server_takeoff = false;

	// Rate limit for the "teleporter needs N ups" print. A gated teleport's
	// trigger touches every frame you are inside it, and on a speed map that
	// volume can be the whole track.
	gtime_t     tele_speed_print_time = 0_ms;

	// Last frame a start_line or weapon_clear volume touched this client. Same
	// problem as the print above - the trigger fires every frame you stand in
	// it - but here it guards work rather than spam: clearing the checkpoint
	// flags sweeps every edict in the level, which must not run at frame rate.
	// The clock reset is cheap and deliberately does run every frame.
	//
	// One timestamp for both entities, which is only safe while no map places
	// them adjacently: stepping straight out of one into the other would read as
	// one continuous touch and suppress the second one's heavy half. No corpus
	// map has both, so this stays one field until one does.
	gtime_t     line_touch_time = 0_ms;

	// A readout state the options menu wants this client's overlay to adopt,
	// encoded by Jump_EncodeHudRequest and published in JUMP_STAT_HUD_REQUEST.
	// One-shot: cleared as soon as the client reports back, so a request can
	// never outlive the pick that made it and re-apply itself a map later.
	int16_t     hud_request = 0;
	int64_t		pb_time_ms = 0; // 0 = none yet (per map; reset in Jump_InitLevel)

	// The player's own PB replay for the current map, loaded lazily on the
	// first `replay`/`race` and reused after - not per-run state, so it is
	// not touched by Jump_ResetRun, only by Jump_InitLevel (a map change
	// invalidates it outright) and by Jump_SaveReplay (a new PB invalidates
	// the cache so the next load picks up the run that just finished, rather
	// than racing or replaying a stale one for the rest of the session).
	jump::replay_t loaded_replay;
	bool           loaded_replay_valid = false;

	// Whether `replay` is driving this client's own body right now, and the
	// Jump_NowMs() stamp playback started - see jump_replay.cpp. Explicitly
	// cleared by Jump_CmdReplayStop, natural completion, a team change (the
	// idle-kick sweep can force one mid-playback) and disconnect; also reset
	// in Jump_InitLevel since a map change destroys the edict state it drives.
	jump_replay_mode_t replay_mode = jump_replay_mode_t::none;
	int64_t             replay_start_ms = 0;

	// Where the player was standing when `replay` started, restored when it
	// ends on its own or via `replay stop`. Without this the player is left
	// wherever the ghost's last driven position put them - an unlimited free
	// teleport to anywhere on their own PB path, and on a map with no
	// checkpoints (the corpus norm), a way to bank a near-instant fake time
	// by starting a fresh run right where the ghost reached the finish.
	vec3_t replay_return_origin = {};
	vec3_t replay_return_angles = {};

	// Whether this player wants their raceline ghost drawn while they run.
	// Per-map like the loaded replay above, not per-run: restarting a run
	// keeps racing the same ghost. See jump_replay.cpp / jump_chase.cpp.
	bool     race_armed = false;
	edict_t *race_beam[JUMP_RACE_TRAIL_SEGMENTS] = {};

	// Which scoreboard page the next send should build. Only meaningful while
	// client->showscores is set, which stays the open/closed authority because
	// it is what gates LAYOUTS_LAYOUT in G_SetStats.
	jump_board_t board = jump_board_t::players;
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

	// Keep only the first info_player_deathmatch the map declares, so everyone
	// starts a run from the same place. See Jump_InhibitEntity.
	bool singlespawn = false;

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

	// singlespawn: whether a spawn point has already been kept this level.
	// Safe to hold here because Jump_InitLevel runs before the entity parse
	// loop that reads it (g_spawn.cpp), so it always starts false.
	bool singlespawn_kept = false;

	// Precached in Jump_InitLevel. gi.soundindex allocates a configstring and
	// the server broadcasts it, so it can only be called while a map is
	// loading - never from Jump_Init, which runs before a server exists.
	int32_t sound_pb = 0;
	int32_t sound_record = 0;

	// When the HUD banner clears. 0 means nothing is showing, which is also
	// what gates JUMP_STAT_ANNOUNCE off.
	gtime_t announce_expire = 0_ms;
};

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

extern jump_client_t jump_clients[MAX_CLIENTS];
extern jump_level_t	 jump_level;
extern jump_mset_t	 jump_mset;

extern cvar_t *g_jump;
extern cvar_t *jump_version;
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

// Monotonic wall-clock milliseconds (matches Q2JumpRefresh Sys_Milliseconds).
int64_t Jump_NowMs();

// Elapsed run time in milliseconds; 0 unless the timer has started.
int64_t Jump_RunTimeMs(const jump_client_t &jc);

// Drops the run back to idle and clears checkpoints. Stores are untouched.
void Jump_ResetRun(jump_client_t &jc);

// Puts the player back at the spawn with a clean run.
void Jump_RestartRun(edict_t *ent);

// Teleport without the teleporter freeze: origin, angles, zeroed velocity.
// z_offset defaults to 10 - store/recall's "step up off the marker" so a
// recalled player doesn't land inside the small floating model - and must be
// passed as 0 by any caller restoring an EXACT prior position (replay's
// return-to-start): store/recall's nudge is harmless because Ranked refuses
// store and turns recall into a restart, but a caller reachable on Ranked
// repeating it (e.g. `replay`/`replay stop` mashed in a bind) would bank a
// free, repeatable altitude climb every cycle.
void Jump_MovePlayer(edict_t *ent, const vec3_t &origin, const vec3_t &angles, float z_offset = 10.f);

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

// jump_replay.cpp
//
// Sampled from Jump_ClientThink, before pmove - same call site and same
// reasoning as Jump_TrackStrafe/Jump_TrackTakeoff in jump_client.cpp: the
// server sees every usercmd, which is what lets it bucket a true 40 Hz
// regardless of the server's actual tick rate.
void Jump_TrackReplay(edict_t *ent, usercmd_t *ucmd, jump_client_t &jc);

// Called once per server frame from Jump_RunFrame, for every connected
// client: advances anyone in playback (Jump_CmdReplay) and repositions
// anyone's raceline trail (Jump_CmdRace).
void Jump_ReplayFrame();

// Encodes and writes `rec` to this player's replay file for the current map,
// and invalidates their loaded_replay cache so the next replay/race picks up
// the run that was just saved rather than a stale one.
void Jump_SaveReplay(edict_t *ent, const jump::replay_recorder_t &rec);

void Jump_CmdReplay(edict_t *ent);
void Jump_CmdReplayStop(edict_t *ent);
void Jump_CmdRace(edict_t *ent);
void Jump_CmdRaceOff(edict_t *ent);

// Frees any race_beam[] edicts and clears race_armed. Called on `race off`,
// a team switch off Ranked, and disconnect.
void Jump_FreeRaceTrail(jump_client_t &jc);

// Cancels an in-progress replay (restores PM_NORMAL) without requiring the
// player to still be present to ask for it themselves - the backstop for the
// idle-kick sweep forcing a team change mid-playback.
void Jump_CancelReplay(edict_t *ent);

// Jump_ReplayModeActive is declared in jump.h - p_client.cpp's ClientThink
// needs it and jump.h is the only jump header upstream sources include.

// jump_client.cpp
void Jump_StripInventory(edict_t *ent);

// start_line: restart the clock where the player stands, with a spawn-clean
// inventory and no checkpoints. Does not move them and does not touch stores.
void Jump_StartLine(edict_t *ent);

// weapon_clear: the same inventory wipe on its own, leaving the run alone.
void Jump_ClearWeapons(edict_t *ent);

// jump_team.cpp
const char *Jump_TeamName(jump_team_t team);
void		Jump_JoinTeam(edict_t *ent, jump_team_t team);
void		Jump_CmdTeam(edict_t *ent);

// jump_files.cpp
const std::filesystem::path &Jump_DataRoot();
std::filesystem::path		 Jump_MapTimesPath(const char *mapname);
std::filesystem::path		 Jump_MsetPath(const char *mapname);
// Nested by map rather than flat, unlike Jump_MapTimesPath - the map corpus
// is 4000+ maps, and a replay is one file per (map, player) rather than one
// per map, so a flat layout would mean thousands of files in one directory.
std::filesystem::path		 Jump_ReplayPath(const char *mapname, const char *player_id);
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

// jump_hud.cpp
//
// Jump_Announce puts `text` on the HUD banner for `duration` and is the way to
// say something everyone should see: an ordinary print lands in the notify
// area, where the next line of chat scrolls it away. Rare events only - the
// text lives in a configstring, and writing one broadcasts to every connected
// client.
void Jump_Announce(const char *text, gtime_t duration);
void Jump_AnnounceFrame();

// Re-issues CS_STATUSBAR when the timelimit moves - the map countdown bakes an
// absolute server frame into the layout. No-op on every other frame.
void Jump_StatusbarFrame();

// jump_cmds.cpp
//
// Whether the SERVER should draw its own copy of each readout for this client.
// For a client running the mod's own half this is derived live from the cvars it
// reported; for a stock client it is the `speedo` / `strafebar` flag.
bool Jump_ServerDrawsSpeedo(const jump_client_t *jc);

// Whether the player has a speedometer at all, from either half. The takeoff
// mark is a reference for it and must not outlive it.
bool Jump_PlayerHasSpeedo(const jump_client_t *jc);

// Every performance readout off, whichever half draws it - the shipped default
// state. Reachable from Options and from the `hudreset` command, and the only way
// a player who has run an older build gets a changed default, since the
// rerelease archives these cvars whether they were touched or not.
void Jump_ResetReadouts(edict_t *ent);
bool Jump_ServerDrawsStrafeBar(const jump_client_t *jc);

// jump_mset.cpp
enum class jump_mset_result_t
{
	ok,
	unknown_key,
	bad_value
};

void				Jump_LoadMsets(const char *entities);
void				Jump_MsetFrame();
bool				Jump_IsUsableWeapon(item_id_t id);
jump_mset_result_t	Jump_ApplyMset(const std::string &key, const std::string &value);
void				Jump_CmdMsets(edict_t *ent, bool show_server_hint);

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

// jump_chase.cpp
void Jump_EyecamOn(edict_t *ent);
void Jump_EyecamOff(edict_t *ent);
void Jump_CmdEyecam(edict_t *ent);
void Jump_CmdJumpers(edict_t *ent);
void Jump_FreeFollower(edict_t *ent);
// Hard-clears every viewer following `target`. For disconnects only - a team
// change wants Jump_RetargetClientFollowers, which keeps the follow alive.
void Jump_FreeClientFollowers(edict_t *target);
void Jump_RetargetClientFollowers(edict_t *target);
void Jump_RecountHideJumpers();
bool Jump_AnyHideJumpers();
void Jump_RefreshPlayerInstancing();


// jump_menu.cpp
void Jump_OpenMainMenu(edict_t *ent);
// Rebuilds an open main menu after a team change, which swaps which of the two
// (in-game / spectator) applies. No-op if no menu, or if it is a submenu.
void Jump_RefreshMainMenu(edict_t *ent);
void Jump_OpenVoteMenu(edict_t *ent);
void Jump_CmdMenu(edict_t *ent);

// jump_scoreboard.cpp (Jump_ScoreboardMessage is declared in jump.h)
void Jump_CmdMapTimes(edict_t *ent);
void Jump_CmdPlayerTimes(edict_t *ent);
void Jump_CmdRanks(edict_t *ent);

// Debug logging, no-op unless jump_debug is set.
void Jump_Log(const char *fmt, ...);
