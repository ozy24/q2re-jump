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

// Map records first (# / player / time / date), then only the viewing player.
// One quoted string per row keeps top-10 + empty "-" slots under the layout cap.
constexpr int JUMP_REC_NAME_LEN = 18;
constexpr int JUMP_MAX_RECORD_ROWS = 10;

bool Jump_ScoreboardMessage(edict_t *ent)
{
	if (!Jump_Active())
		return false;

	const jump::map_records_t &records = Jump_Records();

	std::string layout;

	if (!records.times.empty())
		layout = G_Fmt("xv 0 yv 0 cstring2 \"{} - record {} by {}\" ", jump_level.mapname,
					   jump::FormatTime(records.times.front().time_ms).c_str(),
					   jump::SanitizeLayoutText(records.times.front().name, 16).c_str())
					 .data();
	else
		layout = G_Fmt("xv 0 yv 0 cstring2 \"{} - no times yet\" ", jump_level.mapname).data();

	if (!Jump_AppendLayout(layout, "yv 16 xv 8 string2 \"#  player             time     date\" "))
	{
		gi.WriteByte(svc_layout);
		gi.WriteString(layout.c_str());
		return true;
	}

	int y = 26;

	for (int i = 0; i < JUMP_MAX_RECORD_ROWS; i++)
	{
		std::string row;

		if (i < (int) records.times.size())
		{
			const jump::record_t &rec = records.times[i];
			const std::string	  date = rec.date.size() >= 10 ? rec.date.substr(0, 10) : rec.date;

			row = G_Fmt("yv {} xv 8 string \"{:>2}  {:<18.18}  {:>7}  {}\" ", y, i + 1,
						jump::SanitizeLayoutText(rec.name, JUMP_REC_NAME_LEN).c_str(),
						jump::FormatTime(rec.time_ms).c_str(), date.c_str())
					  .data();
		}
		else
		{
			row = G_Fmt("yv {} xv 8 string \"{:>2}  {:<18}  {:>7}  {}\" ", y, i + 1, "-", "-", "-").data();
		}

		if (!Jump_AppendLayout(layout, row))
			break;

		y += 10;
	}

	// Viewer only — not the full connected-player list (layout budget).
	const std::string self_id = Jump_PlayerId(ent);
	const int64_t	  best = records.TimeOf(self_id);
	const int		  place = records.RankOf(self_id);

	y += 8;
	Jump_AppendLayout(
		layout,
		G_Fmt("yv {} xv 8 string2 \"you: {:<16.16}  best {}  place {}\" ", y,
			  jump::SanitizeLayoutText(Jump_DisplayName(ent), 16).c_str(),
			  best ? jump::FormatTime(best).c_str() : "-", place ? std::to_string(place).c_str() : "-")
			.data());

	// Stock puts map time remaining on the scoreboard layout, not CS_STATUSBAR.
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
