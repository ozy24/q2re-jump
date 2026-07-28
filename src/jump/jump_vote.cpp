// [Jump] Map list, voting and idle handling.
//
// Rotation reuses the upstream g_map_list machinery: the maplist file is
// loaded into that cvar, and a passed map vote just sets level.forcemap and
// ends the level, which EndDMLevel() already honours ahead of the list.

#include "../g_local.h"
#include "jump_local.h"

#include <string>
#include <vector>

enum class jump_vote_type_t
{
	none,
	map,
	extend
};

struct jump_vote_t
{
	jump_vote_type_t type = jump_vote_type_t::none;
	gtime_t			 ends = 0_ms;
	char			 target[MAX_QPATH] = { 0 };
	int32_t			 minutes = 0;
	char			 caller[MAX_NETNAME] = { 0 };
	bool			 ballots[MAX_CLIENTS] = { false };
	bool			 voted[MAX_CLIENTS] = { false };
};

static jump_vote_t				jump_vote;
static std::vector<std::string> jump_maplist;

constexpr float JUMP_VOTE_PASS_FRACTION = 0.75f; // matches Q2JumpRefresh
constexpr int	JUMP_VOTE_SECONDS = 30;

// ---------------------------------------------------------------------------
// Map list
// ---------------------------------------------------------------------------

void Jump_LoadMapList()
{
	jump_maplist.clear();

	std::string text;

	if (!Jump_ReadFile(Jump_DataRoot() / "maplist.txt", text))
		return;

	size_t start = 0;

	while (start <= text.size())
	{
		size_t end = text.find('\n', start);

		if (end == std::string::npos)
			end = text.size();

		std::string line = text.substr(start, end - start);
		start = end + 1;

		const size_t comment = line.find('#');

		if (comment != std::string::npos)
			line.erase(comment);

		const size_t first = line.find_first_not_of(" \t\r");
		const size_t last = line.find_last_not_of(" \t\r");

		if (first != std::string::npos)
			jump_maplist.push_back(line.substr(first, last - first + 1));

		if (end == text.size())
			break;
	}

	if (jump_maplist.empty())
		return;

	// Hand the list to the engine's own rotation rather than running a second
	// one alongside it.
	std::string joined;

	for (const auto &map : jump_maplist)
	{
		if (!joined.empty())
			joined += ' ';
		joined += map;
	}

	gi.cvar_set("g_map_list", joined.c_str());
	gi.Com_PrintFmt("[jump] loaded {} map(s) into the rotation\n", jump_maplist.size());
}

static bool Jump_MapInList(const char *name)
{
	for (const auto &map : jump_maplist)
		if (!Q_strcasecmp(map.c_str(), name))
			return true;

	return false;
}

void Jump_CmdMapList(edict_t *ent)
{
	if (jump_maplist.empty())
	{
		gi.Client_Print(ent, PRINT_HIGH, "No maplist loaded (create jump/maplist.txt).\n");
		return;
	}

	gi.Client_Print(ent, PRINT_HIGH, G_Fmt("--- {} map(s) in rotation ---\n", jump_maplist.size()).data());

	std::string line;

	for (size_t i = 0; i < jump_maplist.size(); i++)
	{
		line += jump_maplist[i];
		line += ' ';

		if ((i % 6) == 5 || i + 1 == jump_maplist.size())
		{
			gi.Client_Print(ent, PRINT_HIGH, G_Fmt("{}\n", line.c_str()).data());
			line.clear();
		}
	}
}

// ---------------------------------------------------------------------------
// Voting
// ---------------------------------------------------------------------------

static int Jump_VoterCount()
{
	int count = 0;

	for (auto player : active_players())
		if (player->client->pers.connected)
			count++;

	return count;
}

static void Jump_ClearVote()
{
	jump_vote = {};
}

// target/minutes are set here rather than by the caller, because starting a
// vote resets the whole struct.
static void Jump_StartVote(edict_t *ent, jump_vote_type_t type, const char *description, const char *target,
						   int32_t minutes)
{
	Jump_ClearVote();

	jump_vote.type = type;

	if (target)
		Q_strlcpy(jump_vote.target, target, sizeof(jump_vote.target));

	jump_vote.minutes = minutes;
	jump_vote.ends = level.time + gtime_t::from_sec(JUMP_VOTE_SECONDS);
	Q_strlcpy(jump_vote.caller, ent->client->pers.netname, sizeof(jump_vote.caller));

	// The caller is counted as a yes so a solo host can pass their own vote.
	const int index = (int) (ent->client - game.clients);

	jump_vote.voted[index] = true;
	jump_vote.ballots[index] = true;

	gi.Broadcast_Print(PRINT_CHAT, G_Fmt("{} called a vote: {}\nType yes or no ({} seconds).\n",
										 ent->client->pers.netname, description, JUMP_VOTE_SECONDS)
									   .data());
}

static void Jump_PassVote()
{
	switch (jump_vote.type)
	{
	case jump_vote_type_t::map:
		gi.Broadcast_Print(PRINT_CHAT, G_Fmt("Vote passed: changing to {}\n", jump_vote.target).data());
		Q_strlcpy(level.forcemap, jump_vote.target, sizeof(level.forcemap));
		Jump_ClearVote();
		EndDMLevel();
		return;

	case jump_vote_type_t::extend:
		gi.Broadcast_Print(PRINT_CHAT, G_Fmt("Vote passed: {} more minute(s)\n", jump_vote.minutes).data());
		gi.cvar_set("timelimit", G_Fmt("{}", timelimit->value + jump_vote.minutes).data());
		break;

	default:
		break;
	}

	Jump_ClearVote();
}

void Jump_VoteFrame()
{
	if (jump_vote.type == jump_vote_type_t::none)
		return;

	int yes = 0, no = 0;
	const int voters = Jump_VoterCount();

	for (auto player : active_players())
	{
		const int index = (int) (player->client - game.clients);

		if (!jump_vote.voted[index])
			continue;

		if (jump_vote.ballots[index])
			yes++;
		else
			no++;
	}

	const int needed = (int) (voters * JUMP_VOTE_PASS_FRACTION + 0.999f);

	if (yes >= needed && voters > 0)
	{
		Jump_PassVote();
		return;
	}

	// Once enough players have said no the vote can't pass, so don't make
	// everyone wait out the clock.
	if (no > voters - needed || level.time >= jump_vote.ends)
	{
		gi.Broadcast_Print(PRINT_CHAT, G_Fmt("Vote failed ({} yes, {} no, {} needed).\n", yes, no, needed).data());
		Jump_ClearVote();
	}
}

void Jump_CmdVote(edict_t *ent, bool yes)
{
	if (jump_vote.type == jump_vote_type_t::none)
	{
		gi.Client_Print(ent, PRINT_HIGH, "There is no vote in progress.\n");
		return;
	}

	const int index = (int) (ent->client - game.clients);

	jump_vote.voted[index] = true;
	jump_vote.ballots[index] = yes;

	gi.Client_Print(ent, PRINT_HIGH, yes ? "You voted yes.\n" : "You voted no.\n");
}

static bool Jump_VoteBusy(edict_t *ent)
{
	if (jump_vote.type != jump_vote_type_t::none)
	{
		gi.Client_Print(ent, PRINT_HIGH, "A vote is already in progress.\n");
		return true;
	}

	return false;
}

void Jump_CmdVoteMap(edict_t *ent)
{
	if (Jump_VoteBusy(ent))
		return;

	if (gi.argc() < 2)
	{
		gi.Client_Print(ent, PRINT_HIGH, "Usage: votemap <map>\n");
		return;
	}

	const char *map = gi.argv(1);

	if (!Q_strcasecmp(map, level.mapname))
	{
		gi.Client_Print(ent, PRINT_HIGH, "That map is already running.\n");
		return;
	}

	// Only maps in the rotation, so a typo can't strand the server on a map
	// nobody has.
	if (!jump_maplist.empty() && !Jump_MapInList(map))
	{
		gi.Client_Print(ent, PRINT_HIGH, G_Fmt("'{}' is not in the maplist.\n", map).data());
		return;
	}

	Jump_StartVote(ent, jump_vote_type_t::map, G_Fmt("change map to {}", map).data(), map, 0);
}

void Jump_CmdNominate(edict_t *ent)
{
	// Nominating is a map vote by another name; keep the familiar verb.
	Jump_CmdVoteMap(ent);
}

void Jump_CmdTimeExtend(edict_t *ent)
{
	if (Jump_VoteBusy(ent))
		return;

	if (timelimit->value <= 0)
	{
		gi.Client_Print(ent, PRINT_HIGH, "There is no time limit to extend.\n");
		return;
	}

	int minutes = 15;

	if (gi.argc() > 1)
		minutes = atoi(gi.argv(1));

	if (minutes <= 0 || minutes > 60)
	{
		gi.Client_Print(ent, PRINT_HIGH, "Extend by 1 to 60 minutes.\n");
		return;
	}

	Jump_StartVote(ent, jump_vote_type_t::extend, G_Fmt("extend the map by {} minute(s)", minutes).data(), nullptr,
				   minutes);
}

void Jump_VoteClientDisconnect(edict_t *ent)
{
	if (!ent->client)
		return;

	const int index = (int) (ent->client - game.clients);

	if (index >= 0 && index < (int) MAX_CLIENTS)
	{
		jump_vote.voted[index] = false;
		jump_vote.ballots[index] = false;
	}
}

// ---------------------------------------------------------------------------
// Idle
// ---------------------------------------------------------------------------

void Jump_TrackInput(edict_t *ent, usercmd_t *ucmd)
{
	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc)
		return;

	if (ucmd->forwardmove || ucmd->sidemove || ucmd->buttons)
		jc->last_input_time = level.time;
}

void Jump_IdleFrame()
{
	const int seconds = jump_idle_time ? jump_idle_time->integer : 0;

	if (seconds <= 0)
		return;

	const gtime_t limit = gtime_t::from_sec(seconds);

	// Collect first: moving a player to spectator respawns them, which is too
	// much churn to do while walking the player list.
	edict_t *sweep[MAX_CLIENTS];
	size_t	 count = 0;

	for (auto player : active_players())
	{
		jump_client_t *jc = Jump_ClientData(player);

		if (!jc || jc->team == jump_team_t::spectator)
			continue;

		if (!player->client->pers.connected)
			continue;

		// A player mid-run is often standing still on purpose - routing, or
		// waiting on a mover - so only sweep runs that haven't started or have
		// already finished.
		if (jc->state == jump_run_state_t::running)
			continue;

		if (jc->last_input_time == 0_ms)
		{
			jc->last_input_time = level.time;
			continue;
		}

		if (level.time - jc->last_input_time < limit)
			continue;

		if (count < MAX_CLIENTS)
			sweep[count++] = player;
	}

	for (size_t i = 0; i < count; i++)
	{
		gi.Client_Print(sweep[i], PRINT_HIGH, "Moved to spectator for being inactive.\n");
		Jump_JoinTeam(sweep[i], jump_team_t::spectator);
	}
}

void Jump_CmdIdle(edict_t *ent)
{
	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc)
		return;

	Jump_JoinTeam(ent, jump_team_t::spectator);
}
