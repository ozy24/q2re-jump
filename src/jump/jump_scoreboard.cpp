// [Jump] The map times board (replaces the deathmatch scoreboard) and the
// records console commands.

#include "../g_local.h"
#include "jump_local.h"

#include <string>

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

bool Jump_ScoreboardMessage(edict_t *ent)
{
	if (!Jump_Active())
		return false;

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
