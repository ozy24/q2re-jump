// [Jump] cgame-side HUD overlay.
//
// This is the only client-side part of the mod, and it is purely additive: a
// stock client that connects to a jump server never runs it and still gets a
// working HUD, because the timer, checkpoints and stores are drawn by the
// server-authored statusbar (see Jump_InitStatusbar) using only tokens the
// stock layout interpreter understands.
//
// So this file must NOT repeat anything the statusbar already draws - it adds
// the things a statusbar script cannot express: the team you're on, your
// personal best, and the delta once you finish.

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

	const float right = (hud_vrect.width - 16.f) * scale;
	const float line = (float) cgi.SCR_FontLineHeight(scale);

	// Sits below the statusbar's timer block, which owns the top right corner.
	float y = 72.f * scale;

	char buffer[64];

	cgi.SCR_DrawFontString(Jump_TeamLabel(team), right, y, scale,
						   team == JUMP_TEAM_HARD ? rgba_blue : rgba_red, true, text_align_t::RIGHT);
	y += line;

	const int32_t pb_sec = ps->stats[JUMP_STAT_PB_SEC];
	const int32_t pb_hun = ps->stats[JUMP_STAT_PB_MS];

	if (!pb_sec && !pb_hun)
		return;

	snprintf(buffer, sizeof(buffer), "PB %d.%02d", pb_sec, pb_hun);
	cgi.SCR_DrawFontString(buffer, right, y, scale, rgba_white, true, text_align_t::RIGHT);
	y += line;

	if (run_state != JUMP_RUN_FINISHED)
		return;

	const int32_t seconds = ps->stats[JUMP_STAT_TIME_SEC];
	const int32_t hundredths = ps->stats[JUMP_STAT_TIME_MS];
	const int32_t delta = (seconds * 100 + hundredths) - (pb_sec * 100 + pb_hun);
	const int32_t mag = delta < 0 ? -delta : delta;

	snprintf(buffer, sizeof(buffer), "%c%d.%02d", delta < 0 ? '-' : '+', mag / 100, mag % 100);
	cgi.SCR_DrawFontString(buffer, right, y, scale, delta <= 0 ? rgba_green : rgba_red, true, text_align_t::RIGHT);
}
