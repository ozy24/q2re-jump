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
	char			 description[64] = { 0 };
	bool			 ballots[MAX_CLIENTS] = { false };
	bool			 voted[MAX_CLIENTS] = { false };
};

static jump_vote_t				jump_vote;
static std::vector<std::string> jump_maplist;

constexpr float JUMP_VOTE_PASS_FRACTION = 0.75f; // matches Q2JumpRefresh
constexpr int	JUMP_VOTE_SECONDS = 30;

static int Jump_VoterCount();

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

// Every map that can be voted for: g_map_pool first, then g_map_list, the same
// order and meaning MuffMode uses. The pool is votable but never auto-rotated;
// the list is the rotation and is votable too. Both are plain whitespace
// separated cvar strings of map names.
std::vector<std::string> Jump_CollectVotableMaps()
{
	std::vector<std::string> maps;

	const cvar_t *sources[] = { jump_map_pool, g_map_list };

	for (const cvar_t *source : sources)
	{
		if (!source || !source->string || !source->string[0])
			continue;

		const char *cursor = source->string;

		while (true)
		{
			const char *token = COM_ParseEx(&cursor, " ");

			if (!token || !*token)
				break;

			if (!jump::IsSafeMapToken(token))
				continue;

			bool duplicate = false;

			for (const auto &existing : maps)
			{
				if (!Q_strcasecmp(existing.c_str(), token))
				{
					duplicate = true;
					break;
				}
			}

			if (!duplicate)
				maps.emplace_back(token);
		}
	}

	return maps;
}

static bool Jump_MapInList(const char *name)
{
	for (const auto &map : Jump_CollectVotableMaps())
		if (!Q_strcasecmp(map.c_str(), name))
			return true;

	return false;
}

bool Jump_VoteActive()
{
	return jump_vote.type != jump_vote_type_t::none;
}

const char *Jump_VoteDescription()
{
	return jump_vote.description;
}

const char *Jump_VoteCaller()
{
	return jump_vote.caller;
}

int Jump_VoteSecondsLeft()
{
	if (!Jump_VoteActive())
		return 0;

	const int64_t ms = (jump_vote.ends - level.time).milliseconds();

	return ms > 0 ? (int) ((ms + 999) / 1000) : 0;
}

void Jump_VoteTally(int &yes, int &no, int &needed)
{
	yes = no = 0;

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

	needed = (int) (voters * JUMP_VOTE_PASS_FRACTION + 0.999f);
}

bool Jump_HasVoted(edict_t *ent)
{
	const int index = (int) (ent->client - game.clients);

	return index >= 0 && index < (int) MAX_CLIENTS && jump_vote.voted[index];
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
	Q_strlcpy(jump_vote.caller, Jump_DisplayName(ent), sizeof(jump_vote.caller));
	Q_strlcpy(jump_vote.description, description, sizeof(jump_vote.description));

	// The caller is counted as a yes so a solo host can pass their own vote.
	const int index = (int) (ent->client - game.clients);

	jump_vote.voted[index] = true;
	jump_vote.ballots[index] = true;

	gi.Broadcast_Print(PRINT_CHAT, G_Fmt("{} called a vote: {}\nType yes or no ({} seconds).\n",
										 Jump_DisplayName(ent), description, JUMP_VOTE_SECONDS)
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

// Shared by the console command and the vote menu.
bool Jump_StartMapVote(edict_t *ent, const char *map)
{
	if (Jump_VoteBusy(ent))
		return false;

	// The name ends up in `gamemap "<map>"`, so validate rather than trust it.
	if (!jump::IsSafeMapToken(map))
	{
		gi.Client_Print(ent, PRINT_HIGH, "That is not a valid map name.\n");
		return false;
	}

	if (!Q_strcasecmp(map, level.mapname))
	{
		gi.Client_Print(ent, PRINT_HIGH, "That map is already running.\n");
		return false;
	}

	// Only configured maps, so a typo can't strand the server on a map nobody
	// has.
	if (!Jump_MapInList(map))
	{
		gi.Client_Print(ent, PRINT_HIGH,
						G_Fmt("'{}' is not in g_map_list or g_map_pool.\n", map).data());
		return false;
	}

	Jump_StartVote(ent, jump_vote_type_t::map, G_Fmt("change map to {}", map).data(), map, 0);
	return true;
}

void Jump_CmdVoteMap(edict_t *ent)
{
	if (gi.argc() < 2)
	{
		gi.Client_Print(ent, PRINT_HIGH, "Usage: votemap <map>   (or press TAB for the menu)\n");
		return;
	}

	Jump_StartMapVote(ent, gi.argv(1));
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

		// A player watching a replay sends no meaningful input by design -
		// that is the whole point of "frozen and moved through the saved
		// frames" - so idle time must not accrue while playback is active, or
		// a replay longer than jump_idle_time gets force-switched to
		// spectator mid-playback.
		if (jc->replay_mode != jump_replay_mode_t::none)
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
