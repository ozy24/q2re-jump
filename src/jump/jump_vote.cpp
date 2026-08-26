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
	extend,
	nextmap
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

	needed = jump::VotesNeeded(voters, JUMP_VOTE_PASS_FRACTION);
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

// Called from Jump_InitLevel. The vote lives in a file static, so nothing else
// wipes it on a map change - and a vote that survives one is not merely stale,
// it is armed: `ends` is an absolute level.time from the old map, level.time
// restarts at 0, and the ballots are indexed by client slot. The first player to
// occupy the caller's old slot is read as a yes, and on a one-player server that
// is already the 75% the vote needs.
//
// Only the vote that changes the map clears itself before ending the level
// (Jump_PassVote), so every other way a level ends - the timelimit, a console
// `map`, an admin - used to leave one behind.
void Jump_ResetVote()
{
	Jump_ClearVote();
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

	// A chat line is easy to miss mid-run, so announce the call out loud. The
	// same positioned_sound-from-world pattern the finish uses: no falloff, so
	// everyone hears it wherever they are, and CHAN_RELIABLE because a vote you
	// never noticed being called is worse than one announced late.
	//
	// misc/pc_up.wav is what MuffMode falls back to for its `vote_now`
	// announcement when no voice pack is installed. It deliberately plays on the
	// call only: MuffMode's vote_passed/vote_failed entries have no backup sound,
	// so a stock install is silent on the outcome too.
	if (jump_level.sound_vote)
		gi.positioned_sound(world->s.origin, world, CHAN_AUTO | CHAN_RELIABLE, jump_level.sound_vote, 1, ATTN_NONE, 0);
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

	case jump_vote_type_t::nextmap:
		// No forcemap, which is the whole difference from the case above: with
		// it unset EndDMLevel falls through to the g_map_list rotation and picks
		// whatever follows this map. Clear before ending, same as the map vote -
		// EndDMLevel can reach intermission and back into another frame.
		gi.Broadcast_Print(PRINT_CHAT, "Vote passed: moving to the next map\n");
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

	// This runs from Jump_RunFrame, which keeps ticking through intermission -
	// G_RunFrame_ only stops for the fade and the exit, not for
	// level.intermissiontime. Jump_ClientCommand does gate on it, so nobody can
	// cast or cancel from the scoreboard; a vote left running there would resolve
	// with whatever ballots it happened to hold. Worse, a map or nextmap vote
	// passing here calls EndDMLevel a second time, and while BeginIntermission
	// early-returns, CreateTargetChangeLevel has already rewritten level.nextmap
	// by then - silently redirecting the exit that is mid-flight.
	//
	// So freeze rather than resolve: the vote resumes if the map somehow does
	// not change, and Jump_ResetVote clears it when the next one loads.
	if (level.intermissiontime)
		return;

	int yes = 0, no = 0, needed = 0;
	Jump_VoteTally(yes, no, needed);

	const int voters = Jump_VoterCount();

	switch (jump::ResolveVote(yes, no, voters, needed, level.time >= jump_vote.ends))
	{
	case jump::vote_result_t::passed:
		Jump_PassVote();
		return;

	case jump::vote_result_t::failed:
		gi.Broadcast_Print(PRINT_CHAT, G_Fmt("Vote failed ({} yes, {} no, {} needed).\n", yes, no, needed).data());
		Jump_ClearVote();
		return;

	case jump::vote_result_t::pending:
		break;
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

static void Jump_PrintVoteUsage(edict_t *ent)
{
	gi.Client_Print(ent, PRINT_HIGH,
					"Usage: callvote <command> [arg]\n"
					"  map <mapname>     change to the specified map\n"
					"  extend [minutes]  add time to the current map (default 15)\n"
					"  nextmap           move to the next map in the rotation\n"
					"Vote on one with yes or no. `callvote` on its own opens the menu.\n");
}

// `callvote <type> [arg]`. The subcommand is argv(1), so an argument is argv(2)
// rather than argv(1) as it is for the standalone verbs - which is why each type
// hands off to a Jump_Start*Vote rather than to its command handler.
void Jump_CmdCallVote(edict_t *ent)
{
	const char *type = gi.argv(1);

	if (!Q_strcasecmp(type, "map"))
	{
		// gi.argv() past argc returns "", which would reach the validator and
		// come back as "not a valid map name" - true, but not the problem.
		if (gi.argc() < 3)
		{
			gi.Client_Print(ent, PRINT_HIGH, "Usage: callvote map <mapname>   (or press TAB for the menu)\n");
			return;
		}

		Jump_StartMapVote(ent, gi.argv(2));
		return;
	}

	if (!Q_strcasecmp(type, "extend"))
	{
		Jump_StartExtendVote(ent, gi.argc() > 2 ? atoi(gi.argv(2)) : 15);
		return;
	}

	if (!Q_strcasecmp(type, "nextmap"))
	{
		Jump_StartNextMapVote(ent);
		return;
	}

	Jump_PrintVoteUsage(ent);
}

// Shared by the console command and the menu row. The minute count is a
// parameter rather than being read from argv here, because the two callers put
// it in different places - `timeextend 5` in argv(1), `callvote extend 5` in
// argv(2) - and the menu row has no arguments at all.
bool Jump_StartExtendVote(edict_t *ent, int minutes)
{
	if (Jump_VoteBusy(ent))
		return false;

	if (timelimit->value <= 0)
	{
		gi.Client_Print(ent, PRINT_HIGH, "There is no time limit to extend.\n");
		return false;
	}

	// Refused, not clamped: someone who typed 500 meant something, and silently
	// giving them 60 hides that the number was ignored.
	if (minutes <= 0 || minutes > 60)
	{
		gi.Client_Print(ent, PRINT_HIGH, "Extend by 1 to 60 minutes.\n");
		return false;
	}

	Jump_StartVote(ent, jump_vote_type_t::extend, G_Fmt("extend the map by {} minute(s)", minutes).data(), nullptr,
				   minutes);
	return true;
}

// Whether the rotation would actually move off this map.
//
// EndDMLevel only advances by finding the *current* map in g_map_list and taking
// the one after it (g_main.cpp), so an unlisted map is not a starting point: the
// walk finds nothing, falls through, and reloads where it already is. That is
// reachable in normal play - pass a map vote for something in g_map_pool, which
// is votable but deliberately never rotated, and you are standing on a map the
// list has never heard of.
//
// So this checks g_map_list alone, not Jump_CollectVotableMaps, which unions the
// pool in and would report a rotation that does not exist.
static bool Jump_CurrentMapInRotation()
{
	if (!g_map_list || !g_map_list->string || !g_map_list->string[0])
		return false;

	const char *cursor = g_map_list->string;

	while (true)
	{
		const char *token = COM_ParseEx(&cursor, " ");

		if (!token || !*token)
			break;

		if (!Q_strcasecmp(token, level.mapname))
			return true;
	}

	return false;
}

bool Jump_StartNextMapVote(edict_t *ent)
{
	if (Jump_VoteBusy(ent))
		return false;

	if (!g_map_list || !g_map_list->string || !g_map_list->string[0])
	{
		gi.Client_Print(ent, PRINT_HIGH, "There is no map rotation to move through (g_map_list is empty).\n");
		return false;
	}

	if (!Jump_CurrentMapInRotation())
	{
		gi.Client_Print(ent, PRINT_HIGH,
						G_Fmt("'{}' is not in the rotation, so there is no next map from here.\n"
							  "Use a map vote instead.\n",
							  level.mapname)
							.data());
		return false;
	}

	Jump_StartVote(ent, jump_vote_type_t::nextmap, "move to the next map in the rotation", nullptr, 0);
	return true;
}

void Jump_CmdTimeExtend(edict_t *ent)
{
	Jump_StartExtendVote(ent, gi.argc() > 1 ? atoi(gi.argv(1)) : 15);
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
