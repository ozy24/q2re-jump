// [Jump] cgame-side HUD overlay.
//
// This is the only client-side part of the mod, and it is deliberately tiny.
// A stock client that connects to a jump server never runs it and still gets a
// complete HUD, because the timer, checkpoints, stores, team and personal best
// are all drawn by the server-authored statusbar (Jump_InitStatusbar) using
// only tokens the stock layout interpreter understands.
//
// So this file must NOT repeat anything the statusbar draws. It adds the one
// thing a layout script genuinely cannot express: a signed, coloured delta
// against your personal best once a run is banked.
//
// It is off by default, so out of the box everyone - including the host of a
// listen server - sees exactly what a stock client sees. `jump_hud 1` opts in.

#include "../cg_local.h"
#include "jump_stats.h"
#include "jump_hud_draw.h"

#include <cstdio>

static cvar_t *jump_hud;

void Jump_InitClientCvars()
{
	// Default off: the stock-client view is the one everybody shares, so it is
	// the honest thing to show by default.
	jump_hud = cgi.cvar("jump_hud", "0", CVAR_ARCHIVE);
}

void Jump_DrawHud(const player_state_t *ps, vrect_t hud_vrect, int32_t scale)
{
	if (!ps->stats[JUMP_STAT_ENABLED])
		return;

	if (!jump_hud || !jump_hud->integer)
		return;

	if (ps->stats[STAT_LAYOUTS] & LAYOUTS_HIDE_HUD)
		return;

	if (ps->stats[JUMP_STAT_RUN_STATE] != JUMP_RUN_FINISHED)
		return;

	const int32_t pb_sec = ps->stats[JUMP_STAT_PB_SEC];
	const int32_t pb_hun = ps->stats[JUMP_STAT_PB_MS];

	if (!pb_sec && !pb_hun)
		return; // first completion, nothing to compare against

	const int32_t delta = (ps->stats[JUMP_STAT_TIME_SEC] * 100 + ps->stats[JUMP_STAT_TIME_MS]) -
						  (pb_sec * 100 + pb_hun);
	const int32_t mag = delta < 0 ? -delta : delta;

	char buffer[32];
	snprintf(buffer, sizeof(buffer), "%c%d.%02d", delta < 0 ? '-' : '+', mag / 100, mag % 100);

	// Directly under the statusbar's PB line.
	cgi.SCR_DrawFontString(buffer, (hud_vrect.width - 16.f) * scale, 104.f * scale, scale,
						   delta <= 0 ? rgba_green : rgba_red, true, text_align_t::RIGHT);
}
