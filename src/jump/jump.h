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
// SpawnEntities: reset per-map state and load the map config. The raw entity
// string is passed so worldspawn msets can be read without touching the
// upstream spawn field table.
void Jump_InitLevel(const char *entities);
void Jump_RunFrame();	// G_RunFrame_: per-frame housekeeping
void Jump_Shutdown();	// ShutdownGame: flush anything pending

// ---------------------------------------------------------------------------
// Client hooks
// ---------------------------------------------------------------------------

// PutClientInServer, above the spectator branch: pins a client who has not yet
// answered the join menu to spectator, and arms the prompt.
void Jump_PreSpawn(edict_t *ent);

void Jump_ClientSpawn(edict_t *ent);				  // PutClientInServer
void Jump_ClientThink(edict_t *ent, usercmd_t *ucmd); // ClientThink, before pmove
void Jump_ClientDisconnect(edict_t *ent);			  // ClientDisconnect

// ClientBeginServerFrame, immediately after the death respawn: on Practice a
// stored position is where the run resumes from, so the map spawn the respawn
// just used is only ever a fallback. Inert without a store, and on Ranked.
void Jump_PostDeathRespawn(edict_t *ent);

// Spectator follow hooks. The controls hook replaces stock spectator input;
// the eyecam hook replaces the stock orbit camera when first-person is on.
bool Jump_HandleSpectatorControls(edict_t *ent, usercmd_t *ucmd);
bool Jump_UpdateEyecam(edict_t *ent);
// UpdateChaseCam, on the branch that gives up and nulls chase_target: clears the
// follow presentation the viewer inherited from the target.
void Jump_ChaseTargetLost(edict_t *ent);
void Jump_SyncFollowPresentation(edict_t *ent);
bool Jump_EntityVisibility(edict_t *ent, edict_t *viewer, bool &visible);

// Rare player body sounds (jump, water, gasp, …) that respect jumpers.
// Falls back to gi.sound when nobody has jumpers off. Footsteps/falls stay as
// entity events — instancing already mutes them for jumpers-off viewers.
void Jump_PlayerSound(edict_t *ent, soundchan_t channel, int soundindex, float volume, float attenuation,
					  float timeofs);

// Returns true when the command was handled and upstream should stop.
bool Jump_ClientCommand(edict_t *ent);

// Applies the jump damage rules. Returns true to swallow the hit entirely;
// otherwise `damage` may have been zeroed, which leaves knockback intact so
// rocket jumping still works.
bool Jump_FilterDamage(edict_t *targ, edict_t *attacker, const mod_t &mod, int &damage);

// ---------------------------------------------------------------------------
// World hooks
// ---------------------------------------------------------------------------

// Touch_Item: returns true when jump consumed the touch (finish, checkpoint,
// or an item that is inert in this mode).
bool Jump_ItemTouch(edict_t *ent, edict_t *other);

// ED_CallSpawn: returns true when this classname is a jump entity.
bool Jump_CallSpawn(edict_t *ent);

// G_InhibitEntity, above the deathmatch branch that returns first: true when a
// mset says this entity should never spawn. Today that is only `singlespawn`,
// which keeps the first info_player_deathmatch and drops the rest.
bool Jump_InhibitEntity(edict_t *ent);

// trigger_push_touch: a push whose `target` starts with "checkpoint" is a
// checkpoint barrier. Returns true when the player has earned their way past,
// meaning no push should be applied at all.
bool Jump_PushBarrier(edict_t *self, edict_t *other);

// use_target_changelevel: a player reaching a map exit ends their run. Returns
// true when jump handled it, which suppresses the level change.
bool Jump_LevelExit(edict_t *activator);

// ServerCommand: returns true when jump handled an `sv <cmd>` console command.
bool Jump_ServerCommand();

// G_InitStatusbar: returns true when jump installed its own statusbar.
bool Jump_InitStatusbar();

// G_SetStats: must run last so it wins over SetCTFStats.
void Jump_SetStats(edict_t *ent);

// Refreshes ent's CONFIG_JUMP_PB_STRING configstring from jc->pb_time_ms.
// Call whenever pb_time_ms changes (seeded on connect, improved on finish) -
// not every frame, since a configstring write broadcasts to every client.
void Jump_UpdatePbString(edict_t *ent);

// ClientUserinfoChanged: returns true when jump set the player's skin.
bool Jump_AssignSkin(edict_t *ent, const char *skin);

// DeathmatchScoreboardMessage: returns true when jump sent its own board.
bool Jump_ScoreboardMessage(edict_t *ent);

// Cmd_Score_f: the scoreboard key cycles players -> records -> closed rather
// than toggling one board. Returns true when jump handled the press.
bool Jump_ScoreCycle(edict_t *ent);

// teleporter_touch: true when the map's fasttele mset is on, meaning the
// teleport should not interrupt the player at all - no velocity clear, no
// freeze, and no snapping the view to the destination's angle.
bool Jump_FastTeleport();

// teleporter_touch / trigger_teleport_touch: true when the teleporter's `speed`
// key gates it and this player is too slow, in which case do not teleport them.
// Prints why, rate-limited. Both touch functions need the call - they are
// separate in the rerelease, which is what once let fasttele work on one and
// not the other.
bool Jump_TeleportSpeedBlocked(edict_t *self, edict_t *other);

// ClientThink's pm_type derivation: whether `replay` is currently driving
// this client's own body. PM_FREEZE alone is not enough to hold it, because
// ClientThink recomputes pm_type from scratch on every call and none of its
// other branches (menu, awaiting_respawn, spectator, dead, grapple) apply to
// a live, normal, ungrappled player - so without this a replaying player's
// own next command would fall through to PM_NORMAL and run real physics
// between Jump_ReplayFrame's ticks.
bool Jump_ReplayModeActive(edict_t *ent);
