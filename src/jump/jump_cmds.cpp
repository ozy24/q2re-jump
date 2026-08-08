// [Jump] Client command dispatch.
//
// One hook in ClientCommand calls this; everything jump-specific is routed
// from here so the upstream if/else chain stays untouched.

#include "../g_local.h"
#include "jump_local.h"
#include "jump_version.h"

static void Jump_CmdHelp(edict_t *ent)
{
	gi.Client_Print(ent, PRINT_HIGH,
					"q2re-jump v" JUMP_VERSION_STRING "\n"
					"Jump commands:\n"
					"  store          save your position\n"
					"  recall [1-5]   return to a saved position (1 = most recent)\n"
					"  reset          discard all saved positions\n"
					"  kill           go again: recalls on practice, restarts on ranked\n"
					"  team <name>    practice (stores allowed), ranked (timed), spectator\n"
					"  maptimes       best times on this map\n"
					"  playertimes    your completions and points\n"
					"  ranks          points for everyone connected\n"
					"  maplist        maps in the rotation\n"
					"  msets          settings in force on this map\n"
					"  strafebar      show/hide the server's strafe meter\n"
					"  speedo         show/hide the server's speedometer\n"
					"  votemap <map>  call a vote to change map\n"
					"  timeextend [n] call a vote to add time (default 15 min)\n"
					"  yes / no       vote on the current call\n"
				"  idle           move yourself to spectator\n"
				"  eyecam         toggle first-person follow while spectating\n"
				"  jumpers        hide/show other players' models and sounds\n"
				"  jumphelp       this list\n"
					"Bind them, e.g: bind mouse4 store; bind mouse5 recall\n");
}

bool Jump_ClientCommand(edict_t *ent)
{
	if (!Jump_Active())
		return false;

	// This hook runs ahead of the upstream intermission gate, so honour it
	// here too rather than letting players store and recall on the scoreboard.
	if (level.intermissiontime)
		return false;

	const char *cmd = gi.argv(0);

	// `inven` (conventionally TAB) opens the main options menu, or the cast UI.
	if (!Q_strcasecmp(cmd, "inven"))
	{
		Jump_CmdMenu(ent);
		return true;
	}

	// Vote-focused aliases skip the main menu and open the map list (or cast UI).
	if (!Q_strcasecmp(cmd, "callvote") || !Q_strcasecmp(cmd, "mapvote"))
	{
		if (ent->client->menu)
		{
			PMenu_Close(ent);
			ent->client->update_chase = true;
			return true;
		}

		Jump_OpenVoteMenu(ent);
		return true;
	}

	if (!Q_strcasecmp(cmd, "kill"))
	{
		Jump_CmdKill(ent);
		return true;
	}

	if (!Q_strcasecmp(cmd, "store"))
	{
		Jump_CmdStore(ent);
		return true;
	}

	if (!Q_strcasecmp(cmd, "recall"))
	{
		int which = 1;

		if (gi.argc() > 1)
			which = atoi(gi.argv(1));

		if (which < 1)
			which = 1;

		Jump_CmdRecall(ent, which);
		return true;
	}

	if (!Q_strcasecmp(cmd, "reset"))
	{
		Jump_CmdReset(ent);
		return true;
	}

	if (!Q_strcasecmp(cmd, "team"))
	{
		Jump_CmdTeam(ent);
		return true;
	}

	if (!Q_strcasecmp(cmd, "maptimes"))
	{
		Jump_CmdMapTimes(ent);
		return true;
	}

	if (!Q_strcasecmp(cmd, "playertimes"))
	{
		Jump_CmdPlayerTimes(ent);
		return true;
	}

	if (!Q_strcasecmp(cmd, "ranks") || !Q_strcasecmp(cmd, "playerscores"))
	{
		Jump_CmdRanks(ent);
		return true;
	}

	if (!Q_strcasecmp(cmd, "votemap"))
	{
		Jump_CmdVoteMap(ent);
		return true;
	}

	if (!Q_strcasecmp(cmd, "nominate"))
	{
		Jump_CmdNominate(ent);
		return true;
	}

	if (!Q_strcasecmp(cmd, "timeextend") || !Q_strcasecmp(cmd, "votetime"))
	{
		Jump_CmdTimeExtend(ent);
		return true;
	}

	if (!Q_strcasecmp(cmd, "yes"))
	{
		Jump_CmdVote(ent, true);
		return true;
	}

	if (!Q_strcasecmp(cmd, "no"))
	{
		Jump_CmdVote(ent, false);
		return true;
	}

	if (!Q_strcasecmp(cmd, "maplist"))
	{
		Jump_CmdMapList(ent);
		return true;
	}

	if (!Q_strcasecmp(cmd, "idle"))
	{
		Jump_CmdIdle(ent);
		return true;
	}

	if (!Q_strcasecmp(cmd, "eyecam"))
	{
		Jump_CmdEyecam(ent);
		return true;
	}

	if (!Q_strcasecmp(cmd, "jumpers"))
	{
		Jump_CmdJumpers(ent);
		return true;
	}

	// These two are sent automatically by the mod's own client when its overlay
	// cvars change, so a player running the DLL never gets the server's version
	// and the overlay's at once. Typeable too, which is both the fallback if a
	// send is ever missed and the way a stock-client player turns one off.
	//
	// Only announce an actual change. The client re-announces at every level
	// start, and a handshake that says so out loud would put a line in the notify
	// area on every map change for every player running the DLL.
	if (!Q_strcasecmp(cmd, "strafebar"))
	{
		jump_client_t *jc = Jump_ClientData(ent);

		if (!jc)
			return true;

		const bool want = gi.argc() > 1 ? atoi(gi.argv(1)) != 0 : !jc->server_strafebar;

		if (want != jc->server_strafebar)
		{
			jc->server_strafebar = want;
			gi.Client_Print(ent, PRINT_HIGH, want ? "Strafe bar on.\n" : "Strafe bar off.\n");
		}

		return true;
	}

	if (!Q_strcasecmp(cmd, "speedo"))
	{
		jump_client_t *jc = Jump_ClientData(ent);

		if (!jc)
			return true;

		const bool want = gi.argc() > 1 ? atoi(gi.argv(1)) != 0 : !jc->server_speedo;

		if (want != jc->server_speedo)
		{
			jc->server_speedo = want;
			gi.Client_Print(ent, PRINT_HIGH, want ? "Speedometer on.\n" : "Speedometer off.\n");
		}

		return true;
	}

	// `mset` is the settable command both upstream mods give players; this port
	// keeps setting on `sv jump_mset` (console/rcon), so answer the question the
	// player was actually asking rather than letting it fall through to the
	// engine's "invalid game command".
	if (!Q_strcasecmp(cmd, "msets") || !Q_strcasecmp(cmd, "mset"))
	{
		Jump_CmdMsets(ent, !Q_strcasecmp(cmd, "mset"));
		return true;
	}

	if (!Q_strcasecmp(cmd, "jumphelp"))
	{
		Jump_CmdHelp(ent);
		return true;
	}

	return false;
}
