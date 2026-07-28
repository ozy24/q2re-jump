// [Jump] Public API for the jump game mode.
//
// This is the only jump header the upstream sources include (once, from
// g_local.h). Everything here is a hook the vanilla code calls; every entry
// point is inert unless the mode is active, so `g_jump 0` restores stock
// behaviour without any further conditionals at the call sites.

#pragma once

// ---------------------------------------------------------------------------
// HUD stats
//
// The stat table is full, so jump reuses the CTF block (18-31). This is safe
// because Jump_Init() forces ctf/teamplay off and Jump_SetStats() runs after
// SetCTFStats() in G_SetStats(), so jump is the last writer.
// ---------------------------------------------------------------------------

constexpr player_stat_t JUMP_STAT_TIME_SEC = STAT_CTF_TEAM1_PIC;		  // 18: whole seconds of the current run
constexpr player_stat_t JUMP_STAT_TIME_MS = STAT_CTF_TEAM1_CAPS;		  // 19: milliseconds remainder / 100
constexpr player_stat_t JUMP_STAT_RUN_STATE = STAT_CTF_TEAM2_PIC;		  // 20: jump_run_state_t
constexpr player_stat_t JUMP_STAT_STORES = STAT_CTF_TEAM2_CAPS;			  // 21: stores held
constexpr player_stat_t JUMP_STAT_TEAM = STAT_CTF_FLAG_PIC;				  // 22: jump_team_t
constexpr player_stat_t JUMP_STAT_PB_SEC = STAT_CTF_JOINED_TEAM1_PIC;	  // 23: personal best, whole seconds
constexpr player_stat_t JUMP_STAT_PB_MS = STAT_CTF_JOINED_TEAM2_PIC;	  // 24: personal best, ms / 100
constexpr player_stat_t JUMP_STAT_CHECKPOINTS = STAT_CTF_TEAM1_HEADER;	  // 25: checkpoints taken
constexpr player_stat_t JUMP_STAT_CHECKPOINT_TOTAL = STAT_CTF_TEAM2_HEADER; // 26: checkpoints required
// 27-31 reserved (replay rank, vote countdown).

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// True when the mode is enabled and the current level is running under it.
bool Jump_Active();

void Jump_PreInit();	// PreInitGame: claim the gamemode before anything latches
void Jump_Init();		// InitGame: register cvars, force the gamemode
void Jump_InitLevel();	// SpawnEntities: reset per-map state, load map config
void Jump_RunFrame();	// G_RunFrame_: per-frame housekeeping
void Jump_Shutdown();	// ShutdownGame: flush anything pending

// ---------------------------------------------------------------------------
// Client hooks
// ---------------------------------------------------------------------------

void Jump_ClientSpawn(edict_t *ent);				  // PutClientInServer
void Jump_ClientThink(edict_t *ent, usercmd_t *ucmd); // ClientThink, before pmove
void Jump_ClientDisconnect(edict_t *ent);			  // ClientDisconnect

// Returns true when the command was handled and upstream should stop.
bool Jump_ClientCommand(edict_t *ent);

// Returns true to swallow the damage entirely.
bool Jump_FilterDamage(edict_t *targ, edict_t *attacker, const mod_t &mod);

// ---------------------------------------------------------------------------
// World hooks
// ---------------------------------------------------------------------------

// Touch_Item: returns true when jump consumed the touch (finish, checkpoint,
// or an item that is inert in this mode).
bool Jump_ItemTouch(edict_t *ent, edict_t *other);

// ED_CallSpawn: returns true when this classname is a jump entity.
bool Jump_CallSpawn(edict_t *ent);

// G_InitStatusbar: returns true when jump installed its own statusbar.
bool Jump_InitStatusbar();

// G_SetStats: must run last so it wins over SetCTFStats.
void Jump_SetStats(edict_t *ent);
