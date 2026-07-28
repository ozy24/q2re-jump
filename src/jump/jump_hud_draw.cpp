// [Jump] cgame-side HUD overlay: run timer, team, stores, checkpoints.
//
// Drawn every render frame from stats the server refreshes each tick, so the
// timer reads smoothly without any per-frame configstring traffic.

#include "../cg_local.h"
#include "jump_stats.h"
#include "jump_hud_draw.h"

#include <cstdio>

static const char *Jump_TeamLabel(int16_t team)
{
	switch (team)
	{
	case JUMP_TEAM_EASY:
		return "EASY";
	case JUMP_TEAM_HARD:
		return "HARD";
	default:
		return "SPECTATOR";
	}
}

void Jump_DrawHud(const player_state_t *ps, vrect_t hud_vrect, int32_t scale)
{
	if (!ps->stats[JUMP_STAT_ENABLED])
		return;

	if (ps->stats[STAT_LAYOUTS] & LAYOUTS_HIDE_HUD)
		return;

	const int16_t team = ps->stats[JUMP_STAT_TEAM];
	const int16_t run_state = ps->stats[JUMP_STAT_RUN_STATE];
	const int32_t seconds = ps->stats[JUMP_STAT_TIME_SEC];
	const int32_t hundredths = ps->stats[JUMP_STAT_TIME_MS];

	const float right = (hud_vrect.width - 16.f) * scale;
	float		y = (hud_vrect.height * 0.25f) * scale;
	const float line = (float) cgi.SCR_FontLineHeight(scale);

	char buffer[64];

	// Run timer. Green once the run is banked, so a finish is unmistakable.
	snprintf(buffer, sizeof(buffer), "%d.%02d", seconds, hundredths);

	const rgba_t timer_color = (run_state == JUMP_RUN_FINISHED)  ? rgba_green
							   : (run_state == JUMP_RUN_RUNNING) ? rgba_white
																 : rgba_yellow;

	cgi.SCR_DrawFontString(buffer, right, y, scale * 2, timer_color, true, text_align_t::RIGHT);
	y += line * 2.f;

	// Team.
	cgi.SCR_DrawFontString(Jump_TeamLabel(team), right, y, scale,
						   team == JUMP_TEAM_HARD ? rgba_blue : rgba_red, true, text_align_t::RIGHT);
	y += line;

	// Checkpoints, only on maps that use them.
	const int32_t cp_total = ps->stats[JUMP_STAT_CHECKPOINT_TOTAL];

	if (cp_total > 0)
	{
		snprintf(buffer, sizeof(buffer), "checkpoints %d/%d", ps->stats[JUMP_STAT_CHECKPOINTS], cp_total);
		cgi.SCR_DrawFontString(buffer, right, y, scale,
							   ps->stats[JUMP_STAT_CHECKPOINTS] >= cp_total ? rgba_green : rgba_white, true,
							   text_align_t::RIGHT);
		y += line;
	}

	// Stores held (Easy only - Hard never has any).
	const int32_t stores = ps->stats[JUMP_STAT_STORES];

	if (stores > 0)
	{
		snprintf(buffer, sizeof(buffer), "stores %d", stores);
		cgi.SCR_DrawFontString(buffer, right, y, scale, rgba_white, true, text_align_t::RIGHT);
		y += line;
	}

	// Personal best for this map, with the delta once the run is done.
	const int32_t pb_sec = ps->stats[JUMP_STAT_PB_SEC];
	const int32_t pb_hun = ps->stats[JUMP_STAT_PB_MS];

	if (pb_sec || pb_hun)
	{
		snprintf(buffer, sizeof(buffer), "PB %d.%02d", pb_sec, pb_hun);
		cgi.SCR_DrawFontString(buffer, right, y, scale, rgba_white, true, text_align_t::RIGHT);
		y += line;

		if (run_state == JUMP_RUN_FINISHED)
		{
			const int32_t delta = (seconds * 100 + hundredths) - (pb_sec * 100 + pb_hun);
			const int32_t mag = delta < 0 ? -delta : delta;

			snprintf(buffer, sizeof(buffer), "%c%d.%02d", delta < 0 ? '-' : '+', mag / 100, mag % 100);
			cgi.SCR_DrawFontString(buffer, right, y, scale, delta <= 0 ? rgba_green : rgba_red, true,
								   text_align_t::RIGHT);
		}
	}
}
