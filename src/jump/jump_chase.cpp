// [Jump] Spectator follow, first-person eyecam, and jumpers visibility.
//
// Follow/eyecam mirrors MuffMode's following_camera.cpp on top of the
// rerelease's chase_target field. jumpers hides other player models per-viewer
// via the same SVF_INSTANCED + Entity_IsVisibleToPlayer path.

#include "../g_local.h"
#include "jump_local.h"

static int jump_hide_jumpers_count = 0;

void Jump_RecountHideJumpers()
{
	jump_hide_jumpers_count = 0;

	if (!Jump_Active())
		return;

	for (uint32_t i = 1; i <= game.maxclients; i++)
	{
		edict_t *viewer = g_edicts + i;
		if (!viewer->inuse || !viewer->client || !viewer->client->pers.connected)
			continue;

		jump_client_t *jc = Jump_ClientData(viewer);
		if (jc && !jc->show_jumpers)
			jump_hide_jumpers_count++;
	}
}

bool Jump_AnyHideJumpers()
{
	return jump_hide_jumpers_count > 0;
}

static bool Jump_IsFollowable(edict_t *ent)
{
	return ent && ent->inuse && ent->client && ent->client->pers.connected &&
		   !ent->client->resp.spectator;
}

static bool Jump_FirstPersonFollowing(edict_t *viewer, edict_t *target = nullptr)
{
	if (!viewer || !viewer->client)
		return false;

	jump_client_t *jc = Jump_ClientData(viewer);
	if (!jc || jc->team != jump_team_t::spectator || !jc->eyecam ||
		!viewer->client->chase_target)
		return false;

	return !target || viewer->client->chase_target == target;
}

static void Jump_ClearFollowPresentation(edict_t *ent)
{
	gclient_t *client = ent->client;

	client->ps.pmove.pm_flags &= ~(PMF_NO_POSITIONAL_PREDICTION | PMF_NO_ANGULAR_PREDICTION);
	client->ps.kick_angles = {};
	client->ps.gunangles = {};
	client->ps.gunoffset = {};
	client->ps.gunindex = 0;
	client->ps.gunskin = 0;
	client->ps.gunframe = 0;
	client->ps.gunrate = 0;
	client->ps.screen_blend = {};
	client->ps.damage_blend = {};
	client->ps.rdflags = RDF_NONE;
	ent->s.effects = EF_NONE;
	ent->s.renderfx &= ~RF_STAIR_STEP;
	ent->s.renderfx |= RF_IR_VISIBLE;
	ent->s.sound = 0;
	ent->s.loop_attenuation = 0;
	ent->s.loop_volume = 0;
}

void Jump_RefreshPlayerInstancing()
{
	for (uint32_t i = 1; i <= game.maxclients; i++)
	{
		edict_t *target = g_edicts + i;
		if (!target->inuse || !target->client)
			continue;

		bool needed = false;
		for (uint32_t j = 1; j <= game.maxclients; j++)
		{
			edict_t *viewer = g_edicts + j;
			if (!viewer->inuse || !viewer->client || !viewer->client->pers.connected)
				continue;

			if (Jump_FirstPersonFollowing(viewer, target))
			{
				needed = true;
				break;
			}

			if (viewer == target)
				continue;

			jump_client_t *vjc = Jump_ClientData(viewer);
			if (vjc && !vjc->show_jumpers)
			{
				needed = true;
				break;
			}
		}

		if (needed)
			target->svflags |= SVF_INSTANCED;
		else
			target->svflags &= ~SVF_INSTANCED;
	}
}

void Jump_FreeFollower(edict_t *ent)
{
	if (!ent || !ent->client)
		return;

	ent->client->chase_target = nullptr;
	Jump_ClearFollowPresentation(ent);
	Jump_RefreshPlayerInstancing();
}

void Jump_FreeClientFollowers(edict_t *target)
{
	if (!target)
		return;

	for (uint32_t i = 1; i <= game.maxclients; i++)
	{
		edict_t *viewer = g_edicts + i;
		if (viewer->inuse && viewer->client && viewer->client->chase_target == target)
			Jump_FreeFollower(viewer);
	}
}

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
	Jump_RefreshPlayerInstancing();
}

void Jump_EyecamOff(edict_t *ent)
{
	if (!ent || !ent->client)
		return;

	jump_client_t *jc = Jump_ClientData(ent);
	if (jc)
		jc->eyecam = false;

	Jump_ClearFollowPresentation(ent);
	Jump_RefreshPlayerInstancing();
	if (ent->client->chase_target)
		ent->client->update_chase = true;
}

void Jump_CmdEyecam(edict_t *ent)
{
	if (!Jump_Active())
		return;

	jump_client_t *jc = Jump_ClientData(ent);
	if (!jc || jc->team != jump_team_t::spectator)
	{
		gi.Client_Print(ent, PRINT_HIGH, "eyecam is only available to spectators.\n");
		return;
	}

	if (jc->eyecam)
		Jump_EyecamOff(ent);
	else
		Jump_EyecamOn(ent);

	gi.Client_Print(ent, PRINT_HIGH, G_Fmt("Follow view: {}\n", jc->eyecam ? "first-person" : "third-person").data());
	gi.local_sound(ent, CHAN_AUTO, gi.soundindex("misc/menu3.wav"), 1, ATTN_NONE, 0);
}

void Jump_CmdJumpers(edict_t *ent)
{
	if (!Jump_Active())
		return;

	jump_client_t *jc = Jump_ClientData(ent);
	if (!jc)
		return;

	jc->show_jumpers = !jc->show_jumpers;
	Jump_RecountHideJumpers();
	Jump_RefreshPlayerInstancing();

	gi.Client_Print(ent, PRINT_HIGH,
					G_Fmt("Player models/sounds are now {}.\n", jc->show_jumpers ? "ON" : "OFF").data());
	gi.local_sound(ent, CHAN_AUTO, gi.soundindex("misc/menu3.wav"), 1, ATTN_NONE, 0);
}

// Handles the same spectator controls as MuffMode:
// use toggles view, crouch goes back, attack toggles follow, jump goes forward.
bool Jump_HandleSpectatorControls(edict_t *ent, usercmd_t *ucmd)
{
	jump_client_t *jc = Jump_ClientData(ent);
	if (!Jump_Active() || !jc || jc->team != jump_team_t::spectator)
		return false;

	gclient_t *client = ent->client;

	if (!client->menu && client->chase_target && (client->latched_buttons & BUTTON_USE))
	{
		client->latched_buttons &= ~BUTTON_USE;
		Jump_CmdEyecam(ent);
	}

	if (!client->menu && client->chase_target && (client->latched_buttons & BUTTON_CROUCH))
	{
		client->latched_buttons &= ~BUTTON_CROUCH;
		ChasePrev(ent);
		Jump_RefreshPlayerInstancing();
	}

	if (!client->menu && (client->latched_buttons & BUTTON_ATTACK))
	{
		client->latched_buttons = BUTTON_NONE;
		if (client->chase_target)
			Jump_FreeFollower(ent);
		else
			GetChaseTarget(ent);
		Jump_RefreshPlayerInstancing();
	}

	if (!client->menu)
	{
		if (ucmd->buttons & BUTTON_JUMP)
		{
			if (!(client->ps.pmove.pm_flags & PMF_JUMP_HELD))
			{
				client->ps.pmove.pm_flags |= PMF_JUMP_HELD;
				if (client->chase_target)
					ChaseNext(ent);
				else
					GetChaseTarget(ent);
				Jump_RefreshPlayerInstancing();
			}
		}
		else
			client->ps.pmove.pm_flags &= ~PMF_JUMP_HELD;
	}

	return true;
}

// Called from the stock UpdateChaseCam after it validates/cycles the target.
// Returning true means first-person output replaced the vanilla orbit camera.
bool Jump_UpdateEyecam(edict_t *ent)
{
	if (!Jump_Active() || !Jump_FirstPersonFollowing(ent))
		return false;

	edict_t *target = ent->client->chase_target;
	if (!Jump_IsFollowable(target))
		return false;

	gclient_t *viewer = ent->client;
	gclient_t *followed = target->client;

	target->svflags |= SVF_INSTANCED;

	viewer->ps.viewangles = followed->ps.viewangles;
	viewer->ps.viewoffset = followed->ps.viewoffset;
	viewer->ps.kick_angles = followed->ps.kick_angles;
	viewer->ps.gunangles = followed->ps.gunangles;
	viewer->ps.gunoffset = followed->ps.gunoffset;
	viewer->ps.gunindex = followed->ps.gunindex;
	viewer->ps.gunskin = followed->ps.gunskin;
	viewer->ps.gunframe = followed->ps.gunframe;
	viewer->ps.gunrate = followed->ps.gunrate;
	viewer->ps.screen_blend = followed->ps.screen_blend;
	viewer->ps.damage_blend = followed->ps.damage_blend;
	// The older rerelease base clamps a missing userinfo "fov" to 1, while
	// MuffMode's newer base supplies a real default. Copying 1 produces an
	// extreme telescope view, so treat that sentinel as the normal 90 degrees.
	viewer->ps.fov = followed->ps.fov > 1.f ? followed->ps.fov : 90.f;
	viewer->ps.rdflags = followed->ps.rdflags;
	viewer->ps.pmove.origin = followed->ps.pmove.origin;
	viewer->ps.pmove.velocity = followed->ps.pmove.velocity;
	viewer->ps.pmove.pm_time = followed->ps.pmove.pm_time;
	viewer->ps.pmove.gravity = followed->ps.pmove.gravity;
	viewer->ps.pmove.delta_angles = {};
	viewer->ps.pmove.viewheight = followed->ps.pmove.viewheight;
	viewer->ps.pmove.pm_type = target->deadflag ? PM_DEAD : PM_FREEZE;
	viewer->ps.pmove.pm_flags &= ~(PMF_NO_POSITIONAL_PREDICTION | PMF_NO_ANGULAR_PREDICTION);
	viewer->pers.hand = followed->pers.hand;

	ent->s.origin = followed->ps.pmove.origin;
	ent->s.modelindex = 0;
	ent->s.modelindex2 = 0;
	ent->s.modelindex3 = 0;
	ent->client->v_angle = target->client->v_angle;
	AngleVectors(ent->client->v_angle, ent->client->v_forward, nullptr, nullptr);
	ent->viewheight = 0;

	Jump_RefreshPlayerInstancing();
	gi.linkentity(ent);
	return true;
}

void Jump_SyncFollowPresentation(edict_t *ent)
{
	if (!Jump_FirstPersonFollowing(ent))
		return;

	edict_t *target = ent->client->chase_target;
	if (!Jump_IsFollowable(target))
		return;

	ent->client->ps.screen_blend = target->client->ps.screen_blend;
	ent->client->ps.damage_blend = target->client->ps.damage_blend;
	ent->s.effects = target->s.effects;
	ent->s.renderfx = target->s.renderfx;
	ent->s.sound = target->s.sound;
	ent->s.loop_attenuation = target->s.loop_attenuation;
	ent->s.loop_volume = target->s.loop_volume;
}

bool Jump_EntityVisibility(edict_t *ent, edict_t *viewer, bool &visible)
{
	if (!Jump_Active() || !ent || !ent->client)
		return false;

	jump_client_t *vjc = Jump_ClientData(viewer);
	const bool show_jumpers = !vjc || vjc->show_jumpers;
	const bool eyecam_following = Jump_FirstPersonFollowing(viewer, ent);

	visible = jump::PlayerVisibleToViewer(show_jumpers, eyecam_following, ent == viewer);
	return true;
}
