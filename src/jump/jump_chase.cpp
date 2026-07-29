// [Jump] Spectator eyecam: first-person follow of another player.
//
// The stock engine already has a third-person chase cam (g_chase.cpp /
// UpdateChaseCam / ChaseNext / ChasePrev).  We extend it with first-person
// follow (eyecam), matching MuffMode's following_camera.cpp approach:
//
//   - By default spectators use the vanilla third-person orbit camera.
//   - Pressing +use (or issuing "eyecam") toggles to first-person: the
//     spectator's ps is filled from the target's ps frame each tick.
//   - invnext/invprev cycle the target; +attack drops/picks up chase.
//
// The toggle state lives in jump_client_t::eyecam so it survives a call to
// PutClientInServer.
//
// Nothing here modifies g_chase.cpp; we just replace UpdateChaseCam output
// for spectators that have eyecam on.

#include "../g_local.h"
#include "jump_local.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// True when ent is a connected, non-spectator, living player that can be
// watched.
static bool Jump_IsFollowable(edict_t *ent)
{
	if (!ent->inuse || !ent->client)
		return false;
	if (ent->client->resp.spectator)
		return false;
	if (!ent->client->pers.connected)
		return false;
	return true;
}

// ---------------------------------------------------------------------------
// Eyecam frame update (first-person follow)
// ---------------------------------------------------------------------------

// Called in place of the stock UpdateChaseCam when eyecam is active.
static void Jump_UpdateEyecam(edict_t *ent)
{
	edict_t *targ = ent->client->chase_target;

	if (!targ || !Jump_IsFollowable(targ))
	{
		// target gone: drop to spectator free-fly
		ent->client->chase_target = nullptr;
		ent->client->ps.pmove.pm_flags &= ~(PMF_NO_POSITIONAL_PREDICTION | PMF_NO_ANGULAR_PREDICTION);
		Jump_EyecamOff(ent);
		GetChaseTarget(ent);
		return;
	}

	gclient_t *tc = targ->client;
	gclient_t *ec = ent->client;

	// First-person: mirror target's player-state
	ec->ps.viewangles	= tc->ps.viewangles;
	ec->ps.viewoffset	= tc->ps.viewoffset;
	ec->ps.kick_angles	= tc->ps.kick_angles;
	ec->ps.gunangles	= tc->ps.gunangles;
	ec->ps.gunoffset	= tc->ps.gunoffset;
	ec->ps.gunindex		= tc->ps.gunindex;
	ec->ps.gunskin		= tc->ps.gunskin;
	ec->ps.gunframe		= tc->ps.gunframe;
	ec->ps.gunrate		= tc->ps.gunrate;
	ec->ps.screen_blend = tc->ps.screen_blend;
	ec->ps.damage_blend = tc->ps.damage_blend;
	ec->ps.fov			= tc->ps.fov;
	ec->ps.rdflags		= tc->ps.rdflags;

	// pmove fields needed for client-side prediction to look right
	ec->ps.pmove.origin		  = tc->ps.pmove.origin;
	ec->ps.pmove.velocity	  = tc->ps.pmove.velocity;
	ec->ps.pmove.pm_time	  = tc->ps.pmove.pm_time;
	ec->ps.pmove.gravity	  = tc->ps.pmove.gravity;
	ec->ps.pmove.viewheight	  = tc->ps.pmove.viewheight;
	ec->ps.pmove.delta_angles = {}; // fully authoritative from target's v_angle

	ec->ps.pmove.pm_type = targ->deadflag ? PM_DEAD : PM_FREEZE;

	// No positional / angular prediction — origin is locked to target
	ec->ps.pmove.pm_flags &= ~(PMF_NO_POSITIONAL_PREDICTION | PMF_NO_ANGULAR_PREDICTION);

	// Hide the spectator model
	ent->s.modelindex  = 0;
	ent->s.modelindex2 = 0;
	ent->s.modelindex3 = 0;

	ent->s.origin	  = tc->ps.pmove.origin;
	ec->v_angle		  = tc->v_angle;
	ent->viewheight	  = 0;

	gi.linkentity(ent);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void Jump_EyecamOn(edict_t *ent)
{
	if (!ent || !ent->client)
		return;

	jump_client_t *jc = Jump_ClientData(ent);
	if (!jc)
		return;

	jc->eyecam = true;

	if (!ent->client->chase_target)
		GetChaseTarget(ent);
}

void Jump_EyecamOff(edict_t *ent)
{
	if (!ent || !ent->client)
		return;

	jump_client_t *jc = Jump_ClientData(ent);
	if (jc)
		jc->eyecam = false;

	// Restore the standard orbit camera flags so vanilla UpdateChaseCam works
	ent->client->ps.pmove.pm_flags &= ~(PMF_NO_POSITIONAL_PREDICTION | PMF_NO_ANGULAR_PREDICTION);
}

// Toggle eyecam for ent.  Only meaningful while spectating.
void Jump_CmdEyecam(edict_t *ent)
{
	if (!Jump_Active())
		return;

	jump_client_t *jc = Jump_ClientData(ent);
	if (!jc)
		return;

	if (jc->team != jump_team_t::spectator)
	{
		gi.Client_Print(ent, PRINT_HIGH, "eyecam is only available to spectators.\n");
		return;
	}

	if (jc->eyecam)
	{
		Jump_EyecamOff(ent);
		gi.Client_Print(ent, PRINT_HIGH, "Eyecam off (third-person)\n");
	}
	else
	{
		Jump_EyecamOn(ent);
		gi.Client_Print(ent, PRINT_HIGH, "Eyecam on (first-person)\n");
	}
}

// Called from Jump_ClientThink to handle spectator key actions.
// invnext / invprev cycle targets; +attack toggles chase on/off.
void Jump_SpectatorThink(edict_t *ent, usercmd_t *ucmd)
{
	(void) ucmd;

	jump_client_t *jc = Jump_ClientData(ent);
	if (!jc || jc->team != jump_team_t::spectator)
		return;

	// invnext / invprev are already handled by vanilla g_cmds.cpp → ChaseNext /
	// ChasePrev when chase_target is set, so we only need to handle the case
	// where there is no target yet (first keypress).
	if (!ent->client->chase_target && ent->client->update_chase)
	{
		GetChaseTarget(ent);
		ent->client->update_chase = false;
	}
}

// Per-frame camera update called from Jump_ClientThink.
void Jump_UpdateChase(edict_t *ent)
{
	jump_client_t *jc = Jump_ClientData(ent);
	if (!jc || jc->team != jump_team_t::spectator)
		return;

	if (!ent->client->chase_target)
		return;

	if (jc->eyecam)
		Jump_UpdateEyecam(ent);
	// else: vanilla UpdateChaseCam is called by the upstream loop in ClientThink
}
