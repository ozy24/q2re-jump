// [Jump] The map times board (replaces the deathmatch scoreboard) and the
// records console commands.

#include "../g_local.h"
#include "jump_local.h"

#include <string>

// The layout string is capped at MAX_STRING_CHARS. Truncating it mid-token is
// not merely ugly: the client's parser raises a fatal error on a malformed
// token stream, so rows are dropped rather than risking that. The reserve
// keeps room for the footer that always has to fit.
constexpr size_t JUMP_LAYOUT_FOOTER_RESERVE = 160;

static bool Jump_AppendRow(std::string &layout, const std::string &row)
{
	if (layout.size() + row.size() + JUMP_LAYOUT_FOOTER_RESERVE >= MAX_STRING_CHARS)
		return false;

	layout += row;
	return true;
}

// Rows are name / best time here / place / current mode.
//
// Mode goes last on purpose. Next to "best" it reads as the mode the time was
// set on, which it is not - a stored time is always from a ranked run, while
// the mode column is whatever the player happens to be doing right now.
constexpr int JUMP_COL_NAME = 16;
constexpr int JUMP_COL_TIME = 116;
constexpr int JUMP_COL_RANK = 178;
constexpr int JUMP_COL_MODE = 226;

constexpr int JUMP_MAX_PLAYER_ROWS = 10;
constexpr int JUMP_MAX_RECORD_ROWS = 5;

bool Jump_ScoreboardMessage(edict_t *ent)
{
	if (!Jump_Active())
		return false;

	const jump::map_records_t &records = Jump_Records();
	const std::string		   self_id = Jump_PlayerId(ent);

	std::string layout;

	// Header: the map and whoever holds it.
	if (!records.times.empty())
		layout += G_Fmt("xv 0 yv 0 cstring2 \"{} - record {} by {}\" ", jump_level.mapname,
						jump::FormatTime(records.times.front().time_ms).c_str(),
						jump::SanitizeLayoutText(records.times.front().name, 18).c_str())
					  .data();
	else
		layout += G_Fmt("xv 0 yv 0 cstring2 \"{} - no times yet\" ", jump_level.mapname).data();

	layout += G_Fmt("yv 20 xv {} string2 player xv {} string2 best xv {} string2 place xv {} string2 mode ",
					JUMP_COL_NAME, JUMP_COL_TIME, JUMP_COL_RANK, JUMP_COL_MODE)
				  .data();

	int y = 32;
	int rows = 0;
	int hidden = 0;

	for (auto player : active_players())
	{
		if (!player->client->pers.connected)
			continue;

		if (rows >= JUMP_MAX_PLAYER_ROWS)
		{
			hidden++;
			continue;
		}

		jump_client_t *jc = Jump_ClientData(player);

		if (!jc)
			continue;

		const std::string id = Jump_PlayerId(player);
		const int64_t	  best = records.TimeOf(id);
		const int		  place = records.RankOf(id);

		// Highlight the viewer's own row.
		const char *tok = (player == ent) ? "string2" : "string";

		// Names are player-controlled, so they are sanitised before being
		// embedded in a quoted token.
		const std::string row =
			G_Fmt("yv {} xv {} {} \"{}\" xv {} {} \"{}\" xv {} {} \"{}\" xv {} {} \"{}\" ", y, JUMP_COL_NAME, tok,
				  jump::SanitizeLayoutText(Jump_DisplayName(player), 16).c_str(), JUMP_COL_TIME, tok,
				  best ? jump::FormatTime(best).c_str() : "-", JUMP_COL_RANK, tok,
				  place ? std::to_string(place).c_str() : "-", JUMP_COL_MODE, tok, Jump_TeamName(jc->team))
				.data();

		if (!Jump_AppendRow(layout, row))
		{
			hidden++;
			break;
		}

		y += 10;
		rows++;
	}

	if (hidden > 0)
		layout += G_Fmt("xv {} yv {} string \"...and {} more\" ", JUMP_COL_NAME, y, hidden).data();

	// Top of the records table, for the times of players who aren't here.
	y += 18;

	int record_rows = 0;

	for (size_t i = 0; i < records.times.size() && record_rows < JUMP_MAX_RECORD_ROWS; i++)
	{
		const jump::record_t &rec = records.times[i];

		const std::string row = G_Fmt("yv {} xv {} string \"{}\" xv {} string \"{}\" xv {} string \"{}\" ", y,
									  JUMP_COL_NAME, (int) i + 1, JUMP_COL_NAME + 40,
									  jump::SanitizeLayoutText(rec.name, 16).c_str(), JUMP_COL_TIME,
									  jump::FormatTime(rec.time_ms).c_str())
									.data();

		if (record_rows == 0)
		{
			const std::string head =
				G_Fmt("yv {} xv {} string2 \"map records\" ", y - 12, JUMP_COL_NAME).data();

			if (!Jump_AppendRow(layout, head))
				break;
		}

		if (!Jump_AppendRow(layout, row))
			break;

		y += 10;
		record_rows++;
	}

	// The viewer's own standing. Spell out what the points mean - "25 points"
	// on its own says nothing until you know they come from where you placed.
	const int rank = records.RankOf(self_id);

	Jump_Log("scoreboard: self id=%s rank=%d of %d record(s)", self_id.c_str(), rank, (int) records.times.size());

	y += 16;

	if (rank > 0)
		layout += G_Fmt("xv {} yv {} string2 \"you are {} of {} here, worth {} points\" ", JUMP_COL_NAME, y, rank,
						(int) records.times.size(), jump::PointsForRank(rank))
					  .data();
	else
		layout += G_Fmt("xv {} yv {} string \"you have no time on this map yet\" ", JUMP_COL_NAME, y).data();

	gi.WriteByte(svc_layout);
	gi.WriteString(layout.c_str());

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
