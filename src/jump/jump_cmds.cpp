// [Jump] Client command dispatch.
//
// One hook in ClientCommand calls this; everything jump-specific is routed
// from here so the upstream if/else chain stays untouched.

#include "../g_local.h"
#include "jump_local.h"

static void Jump_CmdHelp(edict_t *ent)
{
	gi.Client_Print(ent, PRINT_HIGH,
					"Jump commands:\n"
					"  store          save your position\n"
					"  recall [1-5]   return to a saved position (1 = most recent)\n"
					"  reset          discard all saved positions\n"
					"  kill           restart the run from the spawn\n"
					"  team <name>    easy (practice), hard (timed), or spectator\n"
					"  maptimes       best times on this map\n"
					"  playertimes    your completions and points\n"
					"  ranks          points for everyone connected\n"
					"  maplist        maps in the rotation\n"
					"  votemap <map>  call a vote to change map\n"
					"  timeextend [n] call a vote to add time (default 15 min)\n"
					"  yes / no       vote on the current call\n"
					"  idle           move yourself to spectator\n"
					"  jumphelp       this list\n"
					"Bind them, e.g: bind mouse4 store; bind mouse5 recall\n");
}

bool Jump_ClientCommand(edict_t *ent)
{
	if (!Jump_Active())
		return false;

	const char *cmd = gi.argv(0);

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

	if (!Q_strcasecmp(cmd, "jumphelp"))
	{
		Jump_CmdHelp(ent);
		return true;
	}

	return false;
}
