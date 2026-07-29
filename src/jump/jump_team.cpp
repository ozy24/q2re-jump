// [Jump] Easy / Hard / Spectator teams.
//
// Easy is the practice team: recall teleports you and carries your elapsed
// time with it, and finishes are never recorded. Hard is the competitive team:
// there is no recall, so the only way back is a fresh run from the spawn.

#include "../g_local.h"
#include "jump_local.h"

const char *Jump_TeamName(jump_team_t team)
{
	switch (team)
	{
	case jump_team_t::easy:
		return "Easy";
	case jump_team_t::hard:
		return "Hard";
	default:
		return "Spectator";
	}
}

// Skins mirror the upstream mods so players read team at a glance.
static const char *Jump_TeamSkin(jump_team_t team)
{
	switch (team)
	{
	case jump_team_t::easy:
		return "female/ctf_r";
	case jump_team_t::hard:
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

	if (jc->team == team)
	{
		gi.Client_Print(ent, PRINT_HIGH, G_Fmt("You are already on {}.\n", Jump_TeamName(team)).data());
		return;
	}

	jc->team = team;

	// Switching teams always abandons the run in progress, and the stores go
	// with it: a Hard player must not inherit an Easy player's shortcuts.
	Jump_ResetRun(*jc);
	jc->stores.Clear();
	Jump_FreeStoreMarker(*jc);

	const bool spectator = (team == jump_team_t::spectator);

	ent->client->pers.spectator = spectator;
	ent->client->resp.spectator = spectator;

	PutClientInServer(ent);

	if (!spectator)
		G_PostRespawn(ent);

	Jump_AssignSkin(ent, nullptr);

	gi.Broadcast_Print(PRINT_HIGH,
					   G_Fmt("{} joined {}\n", Jump_DisplayName(ent), Jump_TeamName(team)).data());
}

void Jump_CmdTeam(edict_t *ent)
{
	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc)
		return;

	if (gi.argc() < 2)
	{
		gi.Client_Print(ent, PRINT_HIGH,
						G_Fmt("You are on {}. Use: team easy | hard | spectator\n", Jump_TeamName(jc->team)).data());
		return;
	}

	const char *name = gi.argv(1);

	if (!Q_strcasecmp(name, "easy") || !Q_strcasecmp(name, "e"))
		Jump_JoinTeam(ent, jump_team_t::easy);
	else if (!Q_strcasecmp(name, "hard") || !Q_strcasecmp(name, "h"))
		Jump_JoinTeam(ent, jump_team_t::hard);
	else if (!Q_strcasecmp(name, "spectator") || !Q_strcasecmp(name, "spec") || !Q_strcasecmp(name, "s") ||
			 !Q_strcasecmp(name, "observer"))
		Jump_JoinTeam(ent, jump_team_t::spectator);
	else
		gi.Client_Print(ent, PRINT_HIGH,
						G_Fmt("Team '{}' does not exist. Valid teams: easy, hard, spectator\n", name).data());
}
