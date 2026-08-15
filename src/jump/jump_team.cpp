// [Jump] Practice / Ranked / Spectator teams.
//
// Practice lets you store and recall, carrying your elapsed time with you, and
// its finishes are never recorded. Ranked has no recall at all, so the only
// way to a time is one clean run from the spawn - which is what makes ranked
// times comparable.

#include "../g_local.h"
#include "jump_local.h"

const char *Jump_TeamName(jump_team_t team)
{
	switch (team)
	{
	case jump_team_t::practice:
		return "Practice";
	case jump_team_t::ranked:
		return "Ranked";
	default:
		return "Spectator";
	}
}

// Skins mirror the upstream mods so players read team at a glance.
static const char *Jump_TeamSkin(jump_team_t team)
{
	switch (team)
	{
	case jump_team_t::practice:
		return "female/ctf_r";
	case jump_team_t::ranked:
		return "female/ctf_b";
	default:
		return "female/invis";
	}
}

bool Jump_AssignSkin(edict_t *ent, const char *skin)
{
	if (!Jump_Active())
		return false;

	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc)
		return false;

	const int playernum = ent - g_edicts - 1;

	gi.configstring(CS_PLAYERSKINS + playernum,
					G_Fmt("{}\\{}\\default", Jump_DisplayName(ent), Jump_TeamSkin(jc->team)).data());

	(void) skin;
	return true;
}

void Jump_JoinTeam(edict_t *ent, jump_team_t team)
{
	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc)
		return;

	// Every route into a team lands here - the three menu rows, the `team`
	// command, `idle`, the inactivity sweep - so this is the one place the join
	// gate needs latching.
	const bool answering_prompt = !jc->team_chosen;

	jc->team_chosen = true;

	if (jc->team == team)
	{
		// Answering the prompt with Spectator is a real choice, not a mistake,
		// so it should not be told it is already there.
		if (!answering_prompt)
			gi.Client_Print(ent, PRINT_HIGH, G_Fmt("You are already on {}.\n", Jump_TeamName(team)).data());
		return;
	}

	jc->team = team;

	// Switching teams abandons the run in progress, but the stores survive it for
	// the current map - upstream q2jump keeps them too, and dumping a player back
	// at the spawn just because they looked at Ranked for a moment is what the
	// wipe felt like in practice. Nothing can leak into a ranked time: Ranked
	// refuses `store` outright and turns `recall` into a restart.
	//
	// The marker entity does go, though. It is a plain visible model, so leaving
	// it up would park a translucent commander head in the map behind a ranked
	// runner; the recall below puts it back on the way in.
	//
	// Only this player's own follow is dropped here. Anyone watching *them* is
	// dealt with after the flip, below - switching Practice to Ranked leaves you
	// a perfectly good thing to watch, and kicking spectators out for it was a
	// reported bug.
	Jump_FreeFollower(ent);
	Jump_ResetRun(*jc);
	Jump_FreeStoreMarker(*jc);

	// A replay/race belongs to the team it was started on (race only arms on
	// Ranked; replay is refused nowhere but Spectator) - a team switch is
	// also the idle-kick sweep's only way to interrupt a long-running replay,
	// so this is the backstop that makes that safe rather than leaving a
	// frozen pm_type fighting the respawn PutClientInServer is about to do.
	Jump_CancelReplay(ent);
	Jump_FreeRaceTrail(*jc);

	// Practice to Ranked while hanging from the hook: the grapple has to go
	// before PutClientInServer memsets gclient_t, or the only pointer to it is
	// lost and it keeps pulling a player who is no longer allowed one.
	CTFPlayerResetGrapple(ent);

	const bool spectator = (team == jump_team_t::spectator);

	ent->client->pers.spectator = spectator;
	ent->client->resp.spectator = spectator;

	PutClientInServer(ent);

	if (!spectator)
		G_PostRespawn(ent);

	// After the respawn, so the followable test and ChaseNext both see the team
	// this player just landed on rather than the one they left.
	Jump_RetargetClientFollowers(ent);

	Jump_AssignSkin(ent, nullptr);

	gi.Broadcast_Print(PRINT_HIGH,
					   G_Fmt("{} joined {}\n", Jump_DisplayName(ent), Jump_TeamName(team)).data());

	// Land back on the most recent store rather than the map spawn, the way
	// joining Easy does upstream. Must run after PutClientInServer, which spawns
	// the player at the map spawn point.
	if (team == jump_team_t::practice && !jc->stores.Empty())
		Jump_CmdRecall(ent, 1);

	// The menu rows close the menu before calling in here, so this is for the
	// routes that do not: `team` typed at the console, and the inactivity sweep
	// moving someone who walked away with the menu still up. Their team just
	// changed, so the other main menu is now the right one.
	Jump_RefreshMainMenu(ent);
}

void Jump_CmdTeam(edict_t *ent)
{
	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc)
		return;

	if (gi.argc() < 2)
	{
		gi.Client_Print(ent, PRINT_HIGH,
						G_Fmt("You are on {}. Use: team practice | ranked | spectator\n"
							  "The old q2jump names still work: team easy = practice, team hard = ranked.\n",
							  Jump_TeamName(jc->team))
							.data());
		return;
	}

	const char *name = gi.argv(1);

	// "easy" and "hard" stay as aliases: that is what these are called on every
	// other jump server, and muscle memory shouldn't be punished.
	if (!Q_strcasecmp(name, "practice") || !Q_strcasecmp(name, "p") || !Q_strcasecmp(name, "easy") ||
		!Q_strcasecmp(name, "e"))
		Jump_JoinTeam(ent, jump_team_t::practice);
	else if (!Q_strcasecmp(name, "ranked") || !Q_strcasecmp(name, "r") || !Q_strcasecmp(name, "hard") ||
			 !Q_strcasecmp(name, "h"))
		Jump_JoinTeam(ent, jump_team_t::ranked);
	else if (!Q_strcasecmp(name, "spectator") || !Q_strcasecmp(name, "spec") || !Q_strcasecmp(name, "s") ||
			 !Q_strcasecmp(name, "observer"))
		Jump_JoinTeam(ent, jump_team_t::spectator);
	else
		gi.Client_Print(
			ent, PRINT_HIGH,
			G_Fmt("Team '{}' does not exist. Valid teams: practice (easy), ranked (hard), spectator\n", name)
				.data());
}
