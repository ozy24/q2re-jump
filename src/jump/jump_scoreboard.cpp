// [Jump] The two scoreboard pages (they replace the deathmatch scoreboard) and
// the records console commands.
//
// The scoreboard key cycles players -> records -> closed. Two pages rather than
// one crowded board because each svc_layout send is capped at MAX_STRING_CHARS,
// and that budget is nowhere near enough for a high score table and a player
// list at once. Alternating pages gives each of them the whole buffer.

#include "../g_local.h"
#include "jump_local.h"

#include <string>
#include <vector>

// Declared locally in p_hud.cpp and g_ctf.cpp rather than in g_local.h.
void DeathmatchScoreboard(edict_t *ent);

// svc_layout / WriteString hard-crashes past MAX_STRING_CHARS. Keep every
// append under that budget; drop rows rather than truncate mid-token.
constexpr size_t JUMP_LAYOUT_MAX = MAX_STRING_CHARS - 1;

static bool Jump_AppendLayout(std::string &layout, const std::string &chunk)
{
	if (layout.size() + chunk.size() >= JUMP_LAYOUT_MAX)
		return false;

	layout += chunk;
	return true;
}

// Separate xv per column — the remaster layout font is not reliably monospace,
// so space-padded single strings cannot keep columns aligned.
constexpr int JUMP_REC_RANK = 16;
constexpr int JUMP_REC_NAME = 40;
constexpr int JUMP_REC_TIME = 200;
constexpr int JUMP_REC_DATE = 264;
constexpr int JUMP_REC_NAME_LEN = 16;

constexpr int JUMP_YOU_NAME = 16;
constexpr int JUMP_YOU_TIME = 176;
constexpr int JUMP_YOU_RANK = 248;
constexpr int JUMP_YOU_NAME_LEN = 16;

constexpr int JUMP_MAX_RECORD_ROWS = 10;

// Leave room for the viewer you/best/place block (+ optional time_limit).
constexpr size_t JUMP_VIEWER_RESERVE = 160;

// Players page. Same band and row pitch as the records page, so the two sit in
// the same place on screen as the key cycles between them.
constexpr int JUMP_PLR_NAME = 16;
constexpr int JUMP_PLR_SESSION = 150;
constexpr int JUMP_PLR_PB = 214;
constexpr int JUMP_PLR_MODE = 270;
constexpr int JUMP_PLR_NAME_LEN = 14;

// Rows are dropped once the budget is gone, so this cap is only a backstop -
// the byte guard below is what actually decides how many fit (around eight).
constexpr int JUMP_MAX_PLAYER_ROWS = 12;

// Leave room for the trailing "+N more" line and time_limit, which together
// cost 66 bytes at their longest. The spectators heading is not counted here:
// it is emitted with the row that triggers it, so the guard below already sees
// it. Every byte of over-reserve costs a row, so keep this tight.
constexpr size_t JUMP_PLAYERS_RESERVE = 80;

static bool Jump_RecordsBoard(edict_t *ent)
{
	const jump::map_records_t &records = Jump_Records();

	std::string layout;

	if (!records.times.empty())
		layout = G_Fmt("xv 0 yv 0 cstring2 \"{} - record {} by {}\" ", jump_level.mapname,
					   jump::FormatTime(records.times.front().time_ms).c_str(),
					   jump::SanitizeLayoutText(records.times.front().name, 14).c_str())
					 .data();
	else
		layout = G_Fmt("xv 0 yv 0 cstring2 \"{} - no times yet\" ", jump_level.mapname).data();

	if (!Jump_AppendLayout(layout,
						   G_Fmt("yv 16 xv {} string2 # xv {} string2 player xv {} string2 time xv {} string2 date ",
								 JUMP_REC_RANK, JUMP_REC_NAME, JUMP_REC_TIME, JUMP_REC_DATE)
							   .data()))
	{
		gi.WriteByte(svc_layout);
		gi.WriteString(layout.c_str());
		return true;
	}

	int y = 26;

	// Filled records first, then "-" padding — stop early enough that the
	// viewer block below still fits.
	for (int i = 0; i < JUMP_MAX_RECORD_ROWS; i++)
	{
		std::string row;

		if (i < (int) records.times.size())
		{
			const jump::record_t &rec = records.times[i];
			const std::string	  date = rec.date.size() >= 10 ? rec.date.substr(0, 10) : rec.date;

			// Rank/time/date stay unquoted (no spaces); names must be quoted.
			row = G_Fmt("yv {} xv {} string {} xv {} string \"{}\" xv {} string {} xv {} string {} ", y,
						JUMP_REC_RANK, i + 1, JUMP_REC_NAME,
						jump::SanitizeLayoutText(rec.name, JUMP_REC_NAME_LEN).c_str(), JUMP_REC_TIME,
						jump::FormatTime(rec.time_ms).c_str(), JUMP_REC_DATE, date.c_str())
					  .data();
		}
		else
		{
			row = G_Fmt("yv {} xv {} string {} xv {} string - xv {} string - xv {} string - ", y, JUMP_REC_RANK,
						i + 1, JUMP_REC_NAME, JUMP_REC_TIME, JUMP_REC_DATE)
					  .data();
		}

		if (layout.size() + row.size() + JUMP_VIEWER_RESERVE >= JUMP_LAYOUT_MAX)
			break;

		layout += row;
		y += 10;
	}

	// Viewing player only — not every connected client (layout budget).
	const std::string self_id = Jump_PlayerId(ent);
	const int64_t	  best = records.TimeOf(self_id);
	const int		  place = records.RankOf(self_id);

	y += 14;

	Jump_AppendLayout(layout, G_Fmt("yv {} xv {} string2 you xv {} string2 best xv {} string2 place ", y,
									JUMP_YOU_NAME, JUMP_YOU_TIME, JUMP_YOU_RANK)
								  .data());
	y += 10;

	Jump_AppendLayout(
		layout,
		G_Fmt("yv {} xv {} string2 \"{}\" xv {} string2 {} xv {} string2 {} ", y, JUMP_YOU_NAME,
			  jump::SanitizeLayoutText(Jump_DisplayName(ent), JUMP_YOU_NAME_LEN).c_str(), JUMP_YOU_TIME,
			  best ? jump::FormatTime(best).c_str() : "-", JUMP_YOU_RANK,
			  place ? std::to_string(place).c_str() : "-")
			.data());

	if (timelimit && timelimit->value && !level.intermissiontime)
	{
		const int32_t end_frame =
			(int32_t) (gi.ServerFrame() +
					   ((gtime_t::from_min(timelimit->value) - level.time)).milliseconds() / gi.frame_time_ms);
		Jump_AppendLayout(layout, G_Fmt("xv 340 yv -10 time_limit {} ", end_frame).data());
	}

	gi.WriteByte(svc_layout);
	gi.WriteString(layout.c_str());

	return true;
}

// ---------------------------------------------------------------------------
// Players page
// ---------------------------------------------------------------------------

static std::vector<jump::player_row_t> Jump_CollectPlayerRows()
{
	std::vector<jump::player_row_t> rows;

	for (auto player : active_players())
	{
		const jump_client_t *jc = Jump_ClientData(player);

		if (!jc)
			continue;

		jump::player_row_t row;

		row.name = jump::SanitizeLayoutText(Jump_DisplayName(player), JUMP_PLR_NAME_LEN);
		row.session_ms = jc->session_best_ms;
		row.pb_ms = jc->pb_time_ms;
		row.spectator = (jc->team == jump_team_t::spectator) || player->client->resp.spectator;
		row.practice = (jc->team == jump_team_t::practice);

		if (row.spectator && player->client->chase_target)
			row.chasing = jump::SanitizeLayoutText(Jump_DisplayName(player->client->chase_target), 12);

		rows.push_back(row);
	}

	jump::SortPlayerRows(rows);

	return rows;
}

static bool Jump_PlayersBoard(edict_t *ent)
{
	const std::vector<jump::player_row_t> rows = Jump_CollectPlayerRows();

	size_t playing = 0;

	for (const jump::player_row_t &row : rows)
	{
		if (!row.spectator)
			playing++;
	}

	std::string layout =
		G_Fmt("xv 0 yv 0 cstring2 \"{} - {} playing, {} spectating\" ", jump_level.mapname, playing,
			  rows.size() - playing)
			.data();

	if (!Jump_AppendLayout(layout, G_Fmt("yv 16 xv {} string2 player xv {} string2 session xv {} string2 pb "
										 "xv {} string2 mode ",
										 JUMP_PLR_NAME, JUMP_PLR_SESSION, JUMP_PLR_PB, JUMP_PLR_MODE)
									   .data()))
	{
		gi.WriteByte(svc_layout);
		gi.WriteString(layout.c_str());
		return true;
	}

	int	   y = 26;
	size_t shown = 0;
	bool   spectators_headed = false;

	for (const jump::player_row_t &row : rows)
	{
		if ((int) shown >= JUMP_MAX_PLAYER_ROWS)
			break;

		// The heading only exists once there is a spectator to head, and it has
		// to fit in the same breath as the row that triggered it.
		std::string chunk;

		if (row.spectator && !spectators_headed)
			chunk = G_Fmt("yv {} xv {} string2 spectators ", y + 4, JUMP_PLR_NAME).data();

		if (row.spectator)
		{
			const int spec_y = spectators_headed ? y : y + 14;

			// Spectators have no run of their own; the time column carries who
			// they are watching instead. A free-floating spectator is chasing
			// nobody, so it stays a bare dash rather than "watching -".
			const std::string watching = row.chasing.empty() ? "-" : ("watching " + row.chasing);

			chunk += G_Fmt("yv {} xv {} string \"{}\" xv {} string \"{}\" ", spec_y, JUMP_PLR_NAME,
						   row.name.c_str(), JUMP_PLR_SESSION, watching.c_str())
						 .data();
		}
		else
		{
			// Names need quoting; times and the mode word never contain spaces.
			chunk = G_Fmt("yv {} xv {} string \"{}\" xv {} string {} xv {} string {} xv {} {} {} ", y,
						  JUMP_PLR_NAME, row.name.c_str(), JUMP_PLR_SESSION,
						  row.session_ms ? jump::FormatTime(row.session_ms).c_str() : "-", JUMP_PLR_PB,
						  row.pb_ms ? jump::FormatTime(row.pb_ms).c_str() : "-", JUMP_PLR_MODE,
						  row.practice ? "string2" : "string", row.practice ? "practice" : "ranked")
					   .data();
		}

		if (layout.size() + chunk.size() + JUMP_PLAYERS_RESERVE >= JUMP_LAYOUT_MAX)
			break;

		layout += chunk;
		shown++;

		if (row.spectator && !spectators_headed)
		{
			spectators_headed = true;
			y += 14;
		}

		y += 10;
	}

	if (shown < rows.size())
		Jump_AppendLayout(layout, G_Fmt("yv {} xv {} string2 \"+{} more\" ", y + 4, JUMP_PLR_NAME,
										rows.size() - shown)
									  .data());

	if (timelimit && timelimit->value && !level.intermissiontime)
	{
		const int32_t end_frame =
			(int32_t) (gi.ServerFrame() +
					   ((gtime_t::from_min(timelimit->value) - level.time)).milliseconds() / gi.frame_time_ms);
		Jump_AppendLayout(layout, G_Fmt("xv 340 yv -10 time_limit {} ", end_frame).data());
	}

	gi.WriteByte(svc_layout);
	gi.WriteString(layout.c_str());

	return true;
}

// ---------------------------------------------------------------------------
// Page selection
// ---------------------------------------------------------------------------

bool Jump_ScoreboardMessage(edict_t *ent)
{
	if (!Jump_Active())
		return false;

	const jump_client_t *jc = Jump_ClientData(ent);

	// Intermission is the end-of-map summary, so it always gets the records.
	if (jc && jc->board == jump_board_t::players && !level.intermissiontime)
		return Jump_PlayersBoard(ent);

	return Jump_RecordsBoard(ent);
}

bool Jump_ScoreCycle(edict_t *ent)
{
	if (!Jump_Active())
		return false;

	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc)
		return false;

	// Cmd_Score_f closes any open menu before reaching here, and PMenu_Close
	// clears showscores - so a press that dismisses a menu lands on the first
	// page rather than resuming wherever the cycle had got to.
	if (!ent->client->showscores)
	{
		jc->board = jump_board_t::players;
		ent->client->showscores = true;
	}
	else if (jc->board == jump_board_t::players)
	{
		jc->board = jump_board_t::records;
	}
	else
	{
		jc->board = jump_board_t::players;
		ent->client->showscores = false;
		ent->client->update_chase = true;

		return true;
	}

	DeathmatchScoreboard(ent);

	return true;
}

// ---------------------------------------------------------------------------
// Console commands
// ---------------------------------------------------------------------------

void Jump_CmdMapTimes(edict_t *ent)
{
	const jump::map_records_t &records = Jump_Records();

	if (records.times.empty())
	{
		gi.Client_Print(ent, PRINT_HIGH, G_Fmt("No times recorded on {} yet.\n", jump_level.mapname).data());
		return;
	}

	gi.Client_Print(ent, PRINT_HIGH, G_Fmt("--- best times on {} ---\n", jump_level.mapname).data());

	const int max_rows = jump_records_max ? jump_records_max->integer : jump::MAX_HIGHSCORES;

	for (size_t i = 0; i < records.times.size() && (int) i < max_rows; i++)
	{
		const jump::record_t &rec = records.times[i];

		gi.Client_Print(ent, PRINT_HIGH,
						G_Fmt("{:>3}. {:<20.20} {:>10} {:>4} pts  {}\n", (int) i + 1, rec.name.c_str(),
							  jump::FormatTime(rec.time_ms).c_str(), jump::PointsForRank((int) i + 1),
							  rec.date.substr(0, 10).c_str())
							.data());
	}
}

void Jump_CmdPlayerTimes(edict_t *ent)
{
	const std::string id = Jump_PlayerId(ent);

	int points = 0, completions = 0, firsts = 0;
	Jump_PlayerTotals(id, points, completions, firsts);

	gi.Client_Print(ent, PRINT_HIGH,
					G_Fmt("{}: {} map(s) completed, {} first place(s), {} points\n", Jump_DisplayName(ent),
						  completions, firsts, points)
						.data());

	const int rank = Jump_Records().RankOf(id);

	if (rank > 0)
		gi.Client_Print(ent, PRINT_HIGH, G_Fmt("On {}: rank {} with {}\n", jump_level.mapname, rank,
											   jump::FormatTime(Jump_Records().TimeOf(id)).c_str())
											 .data());
	else
		gi.Client_Print(ent, PRINT_HIGH, G_Fmt("On {}: no time yet\n", jump_level.mapname).data());
}

void Jump_CmdRanks(edict_t *ent)
{
	gi.Client_Print(ent, PRINT_HIGH, "--- points for connected players ---\n");
	gi.Client_Print(ent, PRINT_HIGH, "Points per map: 1st-15th = 25 20 16 13 11 10 9 8 7 6 5 4 3 2 1\n");

	for (auto player : active_players())
	{
		if (!player->client->pers.connected)
			continue;

		int points = 0, completions = 0, firsts = 0;
		Jump_PlayerTotals(Jump_PlayerId(player), points, completions, firsts);

		gi.Client_Print(ent, PRINT_HIGH,
						G_Fmt("{:<20.20} {:>6} pts  {:>4} maps  {:>3} firsts\n", Jump_DisplayName(player), points,
							  completions, firsts)
							.data());
	}
}
