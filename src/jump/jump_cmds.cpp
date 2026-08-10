// [Jump] Client command dispatch.
//
// One hook in ClientCommand calls this; everything jump-specific is routed
// from here so the upstream if/else chain stays untouched.

#include "../g_local.h"
#include "jump_local.h"
#include "jump_version.h"

// Who owns each readout. For a client running the mod's own half the reported
// cvars are authoritative and read live, so there is no stored flag to fall out
// of step with them; for a stock client the `speedo` / `strafebar` flags are all
// there is. The rule for the reported case is the one the client used to apply
// itself: a readout the overlay is drawing, or one muted outright, is one the
// server must not draw.
bool Jump_ServerDrawsSpeedo(const jump_client_t *jc)
{
	if (!jc)
		return false;

	if (jc->client_hud)
		return jc->client_speed != JUMP_READOUT_OFF && jc->client_hud_master == 0;

	return jc->server_speedo;
}

// Whether this player has a speedometer at all, drawn by either half.
//
// The takeoff mark is a reference FOR the speedometer - the frozen number above
// the moving one - so it has to follow it. On its own it is a number with
// nothing to be compared against.
bool Jump_PlayerHasSpeedo(const jump_client_t *jc)
{
	if (!jc)
		return false;

	if (jc->client_hud)
		return jc->client_speed != JUMP_READOUT_OFF;

	return jc->server_speedo;
}

bool Jump_ServerDrawsStrafeBar(const jump_client_t *jc)
{
	if (!jc)
		return false;

	if (jc->client_hud)
		return jc->client_strafe != JUMP_READOUT_OFF && jc->client_hud_master == 0;

	return jc->server_strafebar;
}

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
					"  takeoff        show/hide the takeoff mark\n"
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

	// Sent by the mod's own client whenever its overlay cvars change, carrying
	// jump_hud, jump_hud_speed and jump_hud_strafe verbatim. That tells the server
	// which readouts the client has taken over - so it stops drawing its own and
	// nobody sees the same reading twice - and also what the options menu should
	// say, which a plain on/off could not: "my overlay draws it" and "I want none
	// of it" both mean "do not draw yours".
	//
	// A stock client never sends this, so receiving it is also how the server
	// knows it is talking to one of ours. No print and no jumphelp entry: this is
	// machine-to-machine, and it fires on every map change.
	if (!Q_strcasecmp(cmd, "jumphud"))
	{
		jump_client_t *jc = Jump_ClientData(ent);

		// Ignore a hand-typed one. gi.argv() past argc returns "", which atoi
		// reads as 0, so a bare `jumphud` would otherwise register as one of our
		// clients with every readout muted - and take the readouts away from the
		// `speedo` command that player still needs.
		if (!jc || gi.argc() < 4)
		{
			Jump_Log("jumphud <- ignored, argc=%d", gi.argc());
			return true;
		}

		jc->client_hud = true;
		jc->client_hud_master = atoi(gi.argv(1));
		jc->client_speed = atoi(gi.argv(2));
		jc->client_strafe = atoi(gi.argv(3));

		// CGaz joined the report later, so a client built before it sends four
		// arguments rather than five. Read it when it is there and leave the last
		// known value alone when it is not, rather than refusing the whole report
		// and losing the client_hud flag with it.
		if (gi.argc() > 4)
			jc->client_cgaz = atoi(gi.argv(4));

		// Whatever the menu last asked for, the client has now told us where it
		// actually stands, so the request has done its job. Clearing it here is
		// what keeps it one-shot: a request left standing would be re-applied at
		// the next level start and quietly undo a cvar the player set by hand.
		jc->hud_request = 0;

		Jump_Log("jumphud <- %s: hud=%d speed=%d strafe=%d", Jump_DisplayName(ent), jc->client_hud_master,
				 jc->client_speed, jc->client_strafe);
		return true;
	}

	// How a player on a stock client turns a readout off. For one of ours the
	// cvars are authoritative (Jump_ServerDrawsSpeedo), so rather than set a flag
	// that would have no effect, say where the setting actually lives.
	//
	// Only announce an actual change, or `speedo 0` twice would say so twice.
	if (!Q_strcasecmp(cmd, "strafebar"))
	{
		jump_client_t *jc = Jump_ClientData(ent);

		if (!jc)
			return true;

		if (jc->client_hud)
		{
			gi.Client_Print(ent, PRINT_HIGH,
							"Your client draws the strafe meter. Use the menu (Options) or jump_hud_strafe.\n");
			return true;
		}

		const bool want = gi.argc() > 1 ? atoi(gi.argv(1)) != 0 : !jc->server_strafebar;

		if (want != jc->server_strafebar)
		{
			jc->server_strafebar = want;
			gi.Client_Print(ent, PRINT_HIGH, want ? "Strafe bar on.\n" : "Strafe bar off.\n");
		}

		return true;
	}

	// No client copy of this one, so no redirect and no handshake: the bar is the
	// only thing that draws the takeoff mark, for stock and DLL clients alike.
	if (!Q_strcasecmp(cmd, "takeoff"))
	{
		jump_client_t *jc = Jump_ClientData(ent);

		if (!jc)
			return true;

		const bool want = gi.argc() > 1 ? atoi(gi.argv(1)) != 0 : !jc->server_takeoff;

		if (want != jc->server_takeoff)
		{
			jc->server_takeoff = want;
			gi.Client_Print(ent, PRINT_HIGH, want ? "Takeoff mark on.\n" : "Takeoff mark off.\n");
		}

		return true;
	}

	if (!Q_strcasecmp(cmd, "speedo"))
	{
		jump_client_t *jc = Jump_ClientData(ent);

		if (!jc)
			return true;

		if (jc->client_hud)
		{
			gi.Client_Print(ent, PRINT_HIGH,
							"Your client draws the speedometer. Use the menu (Options) or jump_hud_speed.\n");
			return true;
		}

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
