// [Jump] Public API for the jump game mode.
//
// This is the only jump header the upstream sources include (once, from
// g_local.h). Everything here is a hook the vanilla code calls; every entry
// point is inert unless the mode is active, so `g_jump 0` restores stock
// behaviour without any further conditionals at the call sites.

#pragma once

#include "jump_stats.h"

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

// ClientUserinfoChanged: returns true when jump set the player's skin.
bool Jump_AssignSkin(edict_t *ent, const char *skin);
