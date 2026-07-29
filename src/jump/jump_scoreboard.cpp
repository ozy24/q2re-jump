// [Jump] The map times board (replaces the deathmatch scoreboard) and the
// records console commands.

#include "../g_local.h"
#include "jump_local.h"

#include <string>

constexpr size_t JUMP_LAYOUT_LIMIT = MAX_STRING_CHARS - 128;

// The layout string has a hard size limit; stop adding rows before we reach it
// rather than letting the engine truncate mid-token.
static bool Jump_AppendRow(std::string &layout, const std::string &row)
{
	if (layout.size() + row.size() >= JUMP_LAYOUT_LIMIT)
		return false;

	layout += row;
	return true;
}

bool Jump_ScoreboardMessage(edict_t *ent)
{
	if (!Jump_Active())
		return false;

	const jump::map_records_t &records = Jump_Records();
	const std::string		   self_id = Jump_PlayerId(ent);

	const int max_rows = jump_records_max ? jump_records_max->integer : jump::MAX_HIGHSCORES;

	std::string layout;

	// The client font is proportional by default (scr_usekfont), so columns
	// cannot be made to line up by padding with spaces - each cell needs its
	// own cursor. These are xv offsets in the 320-wide virtual layout space.
	constexpr int col_rank = 16;
	constexpr int col_name = 48;
	constexpr int col_time = 190;
	constexpr int col_date = 240;

	layout += G_Fmt("xv 0 yv 0 cstring2 \"{} - best times\" ", jump_level.mapname).data();

	layout += G_Fmt("yv 16 xv {} string2 rank xv {} string2 name xv {} string2 time xv {} string2 date ", col_rank,
					col_name, col_time, col_date)
				  .data();

	int y = 32;
	int shown = 0;

	for (size_t i = 0; i < records.times.size() && shown < max_rows; i++, shown++)
	{
		const jump::record_t &rec = records.times[i];

		// Highlight the viewer's own row.
		const char *tok = (rec.id == self_id) ? "string2" : "string";

		// Names can contain spaces, so every cell is quoted.
		const std::string row = G_Fmt("yv {} xv {} {} \"{}\" xv {} {} \"{:.18}\" xv {} {} \"{}\" xv {} {} \"{:.10}\" ",
									  y, col_rank, tok, (int) i + 1, col_name, tok, rec.name.c_str(), col_time, tok,
									  jump::FormatTime(rec.time_ms).c_str(), col_date, tok, rec.date.c_str())
									.data();

		if (!Jump_AppendRow(layout, row))
			break;

		y += 10;
	}

	if (records.times.empty())
		layout += G_Fmt("xv {} yv {} string \"no times recorded yet\" ", col_rank, y).data();

	// The viewer's own standing, always visible even if they're off the board.
	const int rank = records.RankOf(self_id);

	Jump_Log("scoreboard: self id=%s rank=%d of %d record(s)", self_id.c_str(), rank, (int) records.times.size());

	y += 16;

	if (rank > 0)
		layout += G_Fmt("xv {} yv {} string2 \"you: rank {} ({} points)\" ", col_rank, y, rank,
						 jump::PointsForRank(rank))
				  .data();
	else
		layout += G_Fmt("xv {} yv {} string \"you: no time on this map\" ", col_rank, y).data();

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
